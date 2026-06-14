# Generational Mark-Sweep GC Design Notes

This document sketches a possible replacement for CloverVM's current deferred
reference-counting reclamation system. It is design guidance only; the
implemented runtime still uses heap reference counts, zero-count tables, and
safepoint reclamation as described in
[Refcounting and Reclamation](refcounting-and-reclamation.md).

The goal is a generational, non-moving, stop-the-world mark-and-sweep collector
that preserves object address stability and leaves room for Python C extension
APIs such as `Py_INCREF` and `Py_DECREF`.

## Starting Point

CloverVM currently has several pieces that are useful for a future collector:

- safepoints and published stack scan records;
- root scanning over managed Clover stack slots and the accumulator;
- slab allocation and valid-object bitmaps;
- native layout descriptors for owned-value traversal and release;
- explicit heap object lifecycle metadata;
- centralized heap storage helpers for many object, slot, and array writes.

The current ownership model is still reference counting:

```cpp
slot = incref(new_value);
decref(old_value);
```

Stack and frame values are borrowed. Heap storage performs ownership
maintenance. Objects whose heap refcount reaches zero enter a zero-count table
and are reclaimed later at safepoints after root validation.

A tracing collector would replace ordinary heap-store ownership maintenance with
a store plus write barrier:

```cpp
slot = value;
write_barrier(owner, value);
```

Native extension pinning is a separate compatibility layer. It should not keep
the ordinary managed object graph on deferred reference-counting semantics.

## Non-Goals For The First Pass

The first implementation should not attempt to solve every policy problem:

- no moving or compacting collection;
- no remembered-slot implementation;
- no special promotion-on-global-store policy;
- no concurrent or incremental marking;
- no no-GIL atomic barrier design;
- no final C API contract beyond reserving a pinning/rooting mechanism.

Those features can be considered after a simple generational collector is
correct and measurable.

## Heap Metadata

Each reclaimable heap object needs at least:

- generation: young or old;
- mark state for the current collection;
- remembered state for old objects that may contain young references;
- pin/native-root state for C API references or other external handles.

This can be stored in the object header, in side metadata, or split across both.
The current 8-byte `HeapObject` header is already dense, so an implementation
should evaluate whether widening the header is acceptable before adding fields
directly. Side metadata keeps object size stable, but makes barriers and
marking more indirect.

The design should keep the following concepts separate:

- generation: collection policy;
- mark state: liveness during a collection;
- remembered state: whether an old object is in the minor-GC remembered set;
- pin state: external/native reachability;
- pointer storage class: reclaimable versus immortal/non-reclaimable storage.

Do not overload a single bit to mean more than one of these.

## Generations

The first tracing design should use two generations:

- young generation for newly allocated reclaimable objects;
- old generation for objects that survived enough minor collections or were
  explicitly promoted by policy.

Because the collector is non-moving, promotion does not need to copy an object.
It may be as simple as changing generation metadata. However, in-place promotion
has slab-level consequences: old objects scattered through young allocation
slabs prevent whole-slab recovery if young slabs are expected to become empty in
large chunks.

There are two viable directions:

- tolerate mixed-generation slabs and reclaim individual dead young objects
  through valid-object bitmaps;
- keep young slabs mostly young and delay or constrain in-place promotion until
  slab recovery policy can handle fragmentation.

The first implementation should make this policy explicit. It should not rely
on wholesale nursery slab recovery if it also promotes objects in-place inside
those slabs.

## Write Barrier And Remembered Set

A dirty bit alone is not enough for minor collection. Minor GC must enumerate
old objects that may contain young references. Therefore old-to-young stores
need both:

- a remembered bit or state on the old object to avoid duplicate entries;
- an entry in a remembered set that minor GC can scan.

The baseline barrier is:

```cpp
if(owner is old &&
   child is young &&
   owner is not remembered) {
    remember(owner);
}
```

The barrier should ignore:

- non-pointer values;
- immortal/non-reclaimable values;
- young owners;
- old children;
- old owners that are already remembered.

Deletion and stores of non-young values do not need to remove the object from
the remembered set immediately. Minor GC can clear the remembered state after
scanning an old object if it no longer contains young references.

## Minor Collection

A minor collection marks young objects reachable from:

- managed stack roots and published accumulator values;
- VM/runtime roots that may point to young objects;
- pinned/native roots;
- old objects in the remembered set.

Minor GC should not scan every old object. The remembered set is the boundary
that makes minor collection sublinear in old-heap size.

For each remembered old object, the collector scans its managed reference fields
using the same native-layout traversal model used by release descriptors or a
new tracing descriptor derived from that model. If the scan finds young
children, those children are marked. If the old object no longer contains young
children after scanning, its remembered state can be cleared and it can be left
out of the next remembered set.

Dead young objects are reclaimed by clearing their valid-object bits and running
the appropriate teardown for native resources. If the collector no longer uses
reference-counted ownership, teardown must clear object fields without
`DECREF`-driven transitive destruction. Tracing determines liveness; release
descriptors release native resources and make object memory non-live.

## Major Collection

A major collection marks both young and old objects from the full root set:

- managed stack roots;
- VM/runtime roots;
- pinned/native roots;
- immortal-root references if those objects can point into reclaimable heaps.

After marking, the collector sweeps all reclaimable slabs, clearing valid-object
bits for unmarked objects and releasing fully empty, unpinned slabs through the
existing global heap machinery.

Major collection does not need remembered-set entries for correctness, but it
must repair remembered metadata afterward. Old objects that still contain young
references should remain or become remembered; old objects without young
references should not remain remembered.

## Module Globals

Modules are ordinary `ModuleObject` instances. Their global namespace is stored
in shape-backed object slots, including 256 inline slots plus overflow storage.
Top-level global assignment is therefore a heap write into the module object.

This creates an apparent problem for a generational collector:

```python
x = object()
```

If the module is old and the new object is young, the store is an old-to-young
reference. The normal barrier remembers the module.

The first implementation should use the normal object-level remembered-set
policy for modules. Do not treat modules as roots, do not pre-add all modules to
the remembered set, and do not promote every object stored into a global.

Remembering a module object means minor GC scans that module's managed fields
while it remains remembered. This is acceptable for the first pass because:

- it preserves the ordinary old-to-young invariant;
- it keeps module storage semantics identical to object attribute storage;
- it avoids a separate global-slot tracking mechanism;
- only modules that actually receive old-to-young stores become remembered.

The key invariant is that the remembered set contains dirty old modules, not all
modules in `sys.modules`. Scanning all loaded modules every minor GC would scale
with application size and should be avoided.

After scanning a remembered module during minor GC, the collector should keep it
remembered only if it still contains young references. If all referenced
children are old, immortal, or non-reclaimable, the module's remembered state can
be cleared.

## Rejected Module Policies

### Treat Modules As Roots

Scanning all modules during every minor collection is proportional to the number
and size of loaded modules. Large applications keep most code loaded in
`sys.modules`, so this makes minor GC scale with application size.

### Pre-Remember All Modules

Pre-adding all modules to the remembered set after each minor collection has the
same scaling problem as treating modules as roots. It only changes the data
structure through which the full module set is scanned.

### Promote Objects Stored Into Globals

Immediate promotion avoids old-to-young references from modules, but it conflicts
with slab recovery if promotion is in-place and young slabs are expected to be
recoverable in large chunks. A nursery slab containing a few promoted old
objects cannot be released wholesale.

This policy may be revisited only if the allocator explicitly tolerates
mixed-generation slabs or provides a copying/evacuation strategy for promotion.

### Remember Individual Module Slots

Fine-grained remembered slots would remember `(module, slot)` or a direct slot
address instead of the whole module. Minor GC would scan only dirty global
slots.

This is more precise, but it adds:

- per-slot dirty tracking;
- slot remembered-set insertion and duplicate suppression;
- cleanup when module shapes delete or move slots;
- overflow-slot lifetime hazards if slot addresses are remembered directly.

This should remain a later optimization, justified by measurements showing that
remembered module scanning dominates minor-GC time.

## Store Sites

The GC transition should identify and centralize every managed heap store before
changing semantics. Important store families include:

- `Object::set_shape`;
- `Object::write_storage_location`;
- `Object::write_empty_storage_location`;
- `SlotObject::ensure_overflow_slot`;
- `OverflowSlots` storage writes;
- list, tuple, dict, and array backing storage writes;
- `Member<T>` and `MemberHeapPtr<T>` assignments;
- VM root fields such as builtins, `sys`, and imported module storage;
- code object and cache side arrays that hold managed values.

For object slot writes, the barrier owner is the containing object. For backing
arrays, the owner should be the backing heap object, not the logical Python
object, unless the collector deliberately treats backing objects as interior
storage owned by their parent. The owner choice must match what the tracing
descriptor scans.

## Tracing Descriptors

Release descriptors currently describe how to release owned values during
refcounted teardown. A tracing collector needs a similar way to enumerate
managed references without releasing them.

The descriptor model should support:

- static value spans;
- dynamic value spans using SMI counts or auxiliary counts;
- custom tracing for layouts such as dictionaries, shapes, code objects, and
  other non-contiguous structures.

Do not assume release descriptors can be reused unchanged. Release order,
clearing behavior, and custom deallocation are teardown concerns. Tracing needs
read-only enumeration of child references.

That said, tracing and release descriptors should be declared from shared layout
facts where practical, so object layout changes do not require two unrelated
metadata updates.

## Native Extension Pinning

Python C extension compatibility needs stable object addresses and some mapping
from `Py_INCREF` / `Py_DECREF` to collector-visible reachability.

For the tracing design, native references should pin or root objects rather than
participate in ordinary managed heap ownership:

- `Py_INCREF` increments a native pin count or creates a native root handle;
- `Py_DECREF` decrements that pin count or releases the native root handle;
- pinned objects are treated as roots by both minor and major collection;
- pinned young objects may remain young, promote by policy, or be allocated in a
  separate pinned region, but that is a policy decision.

The design should avoid making `Py_DECREF` perform immediate recursive
destruction. In a tracing collector, object reclamation occurs during collection.

## Open Questions

- Should generation, mark, remembered, and pin metadata live in the header or in
  side tables?
- Should minor collection sweep only young slabs, or sweep all slabs looking for
  young-generation objects?
- Will in-place promotion be allowed in young slabs, and if so how will slab
  recovery handle mixed-generation occupancy?
- What is the exact root set for VM-owned fields that are not on managed stacks?
- How should immortal objects that point to reclaimable objects participate in
  tracing?
- Should pinned young objects be promoted immediately, kept young, or allocated
  from a separate pinned allocation path?
- What benchmarks will decide whether object-level remembered modules are good
  enough?

## First Implementation Checkpoints

1. Add tracing descriptors or a tracing visitor API without changing ownership
   semantics.
2. Add generation and mark metadata behind helper APIs.
3. Add remembered-set infrastructure and barrier helper APIs.
4. Convert a small, well-scoped store family to call the barrier while still
   preserving current reference-counting behavior for validation.
5. Implement a stop-the-world full mark-sweep collector in a test-only or
   opt-in mode.
6. Add minor collection with object-level remembered sets.
7. Only after correctness is established, remove ordinary heap-store
   `INCREF`/`DECREF` maintenance.
8. Benchmark module/global write workloads and remembered-set scanning before
   considering remembered slots.

Correctness should come before performance claims. A minor-GC benchmark win is
not meaningful until tracing handles object layout, module globals, VM roots,
pinned native references, and slab sweeping correctly.
