# Generational Copying GC Implementation Plan

This document describes staging for the collector direction in
[Generational Copying GC Design Notes](generational-copying-gc.md). It is an
implementation plan, not a second collector design.

The implementation order should follow the codebase that exists now. CloverVM
already has native-module and Clover C API work; it does not yet have a CPython
Limited API surface. The first steps should therefore make the existing native
boundary compatible with movable objects before adding compatibility layers that
no current caller uses.

## Stage 1: Switchable Clover Native API Indirect Handles

**Status: implemented.** Indirect mode is enabled, while the direct
representation remains available through the compile-time switch. Persistent
handles remain explicitly deferred.

Refactor the existing Clover native API boundary before moving ordinary VM
objects. The authoritative storage and switching design is in
[Switchable Indirect Native Handles](indirect-native-handles.md).

- Keep `clover_handle` an opaque machine word and add the compile-time
  `native_handle_detail::cl_indirect_handles` switch. Keep the representation
  helpers inline in an internal header so direct mode compiles down to the
  current raw `Value` conversion without context, allocation, or validation
  overhead. In indirect mode the handle is internally a `Value *` to a rooted
  slot.
- Point incoming argument handles directly at their existing stable `Value`
  slots in the managed native thunk frame; do not copy or materialize arguments
  into separate handle storage.
- Reserve the first fixed-size API-storage chunk in that managed frame for
  handles created by C API operations. Use its final cell as the managed link to
  overflow storage rather than as an allocatable handle slot.
- Add fixed-size, stable `HandleChunk` heap objects as a singly linked list.
  Every cell is initialized to `not_present`; the final cell is the managed link
  to the next chunk, and the layout descriptor scans and updates every cell.
- Give `clover_context` the shared `handle_chunk_next` and `handle_chunk_end`
  allocation pointers. They address either fixed frame storage or the current
  overflow chunk; add a boolean selecting frame versus overflow refcount store
  policy. The managed link chain, not the context, retains the chunks.
- Convert extension-call thunks to pass pointers to existing argument slots and
  resolve returned handles through the active frame or its overflow chain.
- Treat a valid active `clover_context *` and valid opaque handles as C API
  preconditions. Handle-source validation is not part of the first design.
- Keep argument slots and fixed API storage in ordinary managed-frame root
  scanning. Scan and update every `HandleChunk` cell through its layout
  descriptor; unused cells contain `not_present`, and the final cell is the
  managed next-chunk link.
- Publish managed-frame boundaries that keep the entire fixed API-storage region
  and link cell live throughout native execution and nested managed re-entry.
- Store a produced value into the final allocatable cell before eagerly
  allocating and publishing the next overflow chunk, so the value is rooted if
  allocation reaches a safepoint.
- Preserve deferred-refcount ownership explicitly: frame handle slots do not
  retain values, while overflow value and link cells retain on store and release
  through the `HandleChunk` layout descriptor.
- Keep module-builder values rooted while the module initializer runs.
- Give native module initialization an RAII Clover-stack root region below the
  current published frontier. Initialize the context's fixed handle chunk from
  that region, publish it for both GC scanning and nested managed-entry
  allocation, and restore the previous frontier on exit.
- Ensure C API helpers resolve handles before storing managed values into VM
  objects.

This stage does not require removing deferred refcounting. Refcounting remains
the lifetime authority while the native boundary stops exposing raw movable
`Value` storage.

The implemented stage has coverage for C API constructors, exceptional returns,
frame-versus-overflow refcount ownership, and transition through multiple
overflow chunks. Its remaining confidence tests should prove that argument
handles alias their original managed slots and that reclamation keeps frame and
overflow handle targets live while the native root region is published. Slot
rewriting belongs to the trace/update and relocation-simulation stages below.
Nested native-to-managed re-entry should be tested when the Clover C API gains
such an entry point; there is no current API operation that can exercise it.

Persistent native handles are a separate follow-up. They are needed for native
state that intentionally outlives one API entry, but their API and storage
should be designed only when an existing native-module use requires cross-entry
retention.

## Stage 2: Precise Managed Root Publication

Add precise managed-root publication as an early, independently useful
safepoint capability.

The current deferred-refcounting scanner may build a temporary set of pointer
identities and use it only as a ZCT filter. A precise root publisher can improve
that path before moving GC exists by identifying the live managed slots for the
current safepoint instead of treating the whole conservative scan range as equally
live.

This stage should cover:

- managed frame slots known live at function-entry, normal-return, and future
  loop-back safepoints;
- accumulator/register roots published through explicit safepoint records;
- native-to-managed boundary frame roots;
- transitory Clover native handle slots once Stage 1 handle frames exist.

The first consumer can still be the existing ZCT reclamation path: publish exact
root identities from exact slots, then use those identities as the safepoint root
filter. The important design choice is that the publisher records slots or slot
provenance, not only object identities, so the same machinery can later become
the moving collector's root-update path.

This stage should not depend on generation state, write barriers, or physical
copying.

## Stage 3: Generation State And Remembered Sets

Add the generational metadata that later barrier call sites can update.

- Add ordinary object generation state, initially enough to distinguish
  `Young`, `Old`, and `OldRemembered`.
- Add per-thread remembered-set storage and debug counters.
- Define allocation policy for young ordinary objects versus direct-old metadata
  objects.
- Add debug validation for generation-state transitions and duplicate remembered
  entries.

This stage should not instrument every store yet. Its purpose is to make the
state model concrete so barrier call sites have something real to update and
tests can assert the intended transitions.

## Stage 4: Barrier Call Sites While Refcounting Remains

Introduce the generational write-barrier API while refcounting still keeps
objects alive. The first barrier implementation can be no-op or diagnostic-only,
but the call sites should be the real owner-aware heap stores.

Do not hide barriers inside `incref` or `decref`: those helpers know the child
but not the heap owner. The minor-GC invariant is about an old owner storing a
young child, so the barrier belongs where the owner is known.

Initial owner-aware store targets include:

- `Object::write_storage_location`;
- `Object::write_empty_storage_location`;
- `OverflowSlots::set`;
- `ValueArray<T>` and `HeapPtrArray<T>` element and backing replacement paths;
- dict and list storage paths;
- module/global store helpers.

The early barrier shape should preserve hot-path factoring:

- reject non-refcounted `Value`s before computing owner/backing state;
- use known-refcounted entry points when the caller has already proven the
  child is a heap reference;
- pass `ThreadState *` directly when it is already available;
- call `active_thread()` only after proving a store really needs to record an
  old-to-young edge.

This stage should include a whole-heap debug checker that proves no unremembered
old object points into the nursery after barrier-capable workloads. That checker
uses descriptor-based tracing once Stage 6 exists; before then, it can be limited
to layouts already covered by explicit probes.

## Stage 5: Class And Shape Metadata Policy

Class and shape metadata should bypass the nursery and be allocated directly in
old movable storage. They are not immortal: major GC traces and may reclaim or
move them. Short-lived inner classes and functions can still die when their
class and shape graphs become unreachable, but they do not need to die in a
minor collection.

The intended invariant is:

```text
ClassObject and Shape are class metadata objects. They are never nursery-young.
Object::shape stores and Shape <-> ClassObject metadata links do not need the
ordinary minor-GC write barrier.
```

This keeps `Object::set_shape` and object initialization out of the ordinary
old-to-young barrier path.

Descriptor and transition names are different. Shape-owned name slots may store
ordinary `String` objects. If the stored name is nursery-young, shape
construction or transition creation must either promote the name or record the
shape in a cold metadata remembered set. The cold metadata barrier belongs in
shape construction and transition code, not in `Object::set_shape`.

This stage should be split internally:

1. allocate `ClassObject` and `Shape` directly in old storage;
2. keep `Object::shape` stores out of the ordinary minor-GC barrier path;
3. add the cold metadata policy for shape-owned descriptor names and transition
   edges.

Do not treat the entire shape graph as solved by direct-old allocation alone.
`Shape` owns descriptor names, transition records, `previous_shape`, and class
metadata links; the minor collector still needs a precise way to find any
nursery object reachable from that graph.

## Stage 6: Trace And Update Descriptors

Add native-layout trace/update descriptors and use them in non-moving validation
passes before implementing physical copying.

Trace descriptors answer which outgoing managed references an object contains.
Update descriptors answer which slots can be rewritten when a referenced object
moves. They are related but not identical: a conservative root identity is enough
for deferred refcounting, while a moving collector must enumerate precise,
mutable slots.

The first implementation should support:

- ordinary static and dynamic `Value` spans;
- `HeapPtrArray` backing records;
- custom visitors for layouts with C++ containers, such as `Shape` transitions
  and `CodeObject` owned constants/caches;
- VM/runtime roots;
- Clover native handle frames.

Do not use a raw object-size-plus-`memcpy` path as a substitute for trace/update
coverage. Layouts with C++ ownership, custom deallocation, native payloads, or
internal vectors need explicit visitor policy before they are eligible for
physical copying.

## Stage 7: Relocation Simulation

Use the precise root publication and trace/update descriptors to simulate the
slot-repair work required by a moving collector.

At safepoints, the relocation simulation pass should:

1. publish managed roots as mutable slots where possible;
2. publish accumulator/register roots through explicit safepoint records;
3. publish transitory native handle slots, plus persistent handle slots once
   that API exists;
4. visit heap references through trace/update descriptors;
5. rewrite selected test references to equivalent same-address values or
   synthetic forwarding targets;
6. debug-check that no stale old address remains in any published root or visited
   heap slot.

This stage is non-moving. It proves that the runtime can find and update the
places a moving collector must repair without also changing allocation and object
identity in the same patch.

## Stage 8: Logical Promotion

At safepoints, the validation pass should:

1. publish managed roots;
2. scan roots and remembered old objects;
3. logically promote reachable young objects to old;
4. clear remembered state after each remembered old object is scanned;
5. debug-check that no unremembered old object points into the nursery.

This stage validates root publication, trace descriptors, remembered sets, and
barrier coverage while deferred refcounting still provides object lifetime.

## Stage 9: Copyability Classification

Classify each native layout before physical copying.

Each layout should be assigned one initial policy:

- nursery-copyable: safe to evacuate with the copying nursery;
- direct-old movable: not nursery allocated, but may move during a later major
  collection after its custom copy/update policy exists;
- stable/non-moving: must not be copied by the ordinary evacuation path.

Start conservatively. Layouts with C++ containers, native-owned storage, custom
deallocation, or pointer fields outside descriptor-covered slots should bypass
the nursery until their copy policy is explicit. This keeps `Shape`, `CodeObject`,
native wrapper storage, and extension-owned records from accidentally entering a
`memcpy` evacuation path.

The classification should be checked at allocation time in debug builds: a
nursery allocation for a non-nursery-copyable layout is a bug.

## Stage 10: Copying Nursery

Implement the copying nursery using the same roots, trace descriptors, update
descriptors, and remembered sets validated earlier.

The first nursery policy should remain simple:

- stop the world at safepoints;
- evacuate reachable young objects;
- promote every young survivor en masse;
- update roots, Clover native handles, and remembered old objects;
- clear remembered state and rebuild it through future write barriers.

Deferred refcounting can still remain during this stage as a compatibility and
debugging scaffold. It should be removed or narrowed only after the moving
collector is carrying ordinary managed-object lifetime.

## Deferred Work

Defer these until the native-handle and managed-collector pieces have been
validated:

- CPython Limited API wrapper identity machinery;
- CPython Limited API wrapper target updates;
- persistent native handles, unless an existing native-module use needs them
  earlier;
- remembered module slots;
- survivor spaces;
- promotion heuristics;
- old-generation tuning;
- card tables;
- region-style remembered sets;
- concurrent copying;
- load barriers.

## Open Questions

### Non-Moving GC Participants

Stable/non-moving storage does not mean outside GC. CPython extension-owned
objects are the important unresolved case: they may be small, they must keep a
stable `PyObject *` address for native code, and they may still reference or be
referenced by movable VM objects.

The plan needs a separate policy for non-moving GC participants before real
CPython extension-owned objects are implemented. Open questions include:

- What is the exact stable object header shape for extension-owned objects?
- Is native refcount alone a liveness source, or does the tracing collector also
  own reachability for these objects?
- What does refcount-zero mean: immediate native deallocation, enqueue for a GC
  finalization pass, or different behavior for different object categories?
- How do `tp_traverse`-style hooks expose references from extension-owned objects
  into movable VM objects?
- How do `tp_clear`, weakrefs, finalizers, resurrection, and native
  `tp_dealloc` semantics fit into the collector's stop-the-world phases?
- How are extension-owned-object-to-young-object references remembered for minor
  GC?
- Are cycles that cross extension-owned objects and movable VM objects supported
  in the first implementation, deliberately leaked, or rejected by the supported
  API subset?

Until these questions are answered, extension-owned objects should be treated as
deferred stable-heap participants: never nursery allocated, never evacuated by
minor GC, and not assumed to be collectible merely because ordinary movable VM
objects are collectible.

### Large Objects And Older Generations

The nursery plan does not settle how large objects, direct-old objects, stable
objects, and older generations are collected. These policies should be decided
before a major collection is implemented.

Large objects need their own allocation and collection policy. They may be too
large to copy cheaply, may be poor fits for nursery evacuation, and may be root
containers whose outgoing references are expensive to rescan on every minor GC.
Open questions include:

- What size or layout threshold sends an object to large-object storage instead
  of the nursery?
- Are large objects always non-moving, or can some large layouts become
  explicitly movable after custom copy/update policy exists?
- How are large-object-to-young-object references remembered for minor GC:
  object-level remembered state, slot/range metadata, card marking, or a
  large-object-specific remembered set?
- Are large objects scanned during every minor GC when remembered, or only the
  remembered slots/ranges?
- Are large objects allocated in direct-old movable storage, stable storage, or a
  separate large-object space?

Older generations also need an explicit major-collection policy. The first
copying nursery can promote every survivor into old storage, but that does not
decide whether old storage later moves.

Open questions include:

- Is the initial major collector non-moving mark-sweep over old, stable, and
  large-object storage?
- If old objects eventually move, which layouts are eligible for old-generation
  compaction and which remain non-moving forever?
- Does the old generation use one space, multiple spaces by copyability class,
  or separate spaces for movable-old, stable, large, and extension-owned records?
- How are references among old, large, stable, and extension-owned objects traced
  during major GC?
- What heap metadata records whether an object is nursery-copyable,
  direct-old-movable, large, stable, or extension-owned?
- Can a major collection reclaim cycles that include non-moving participants, or
  is that deferred until the extension-owned-object policy is settled?

Until these questions are answered, the safe initial assumption is that minor GC
only copies nursery objects, while old, large, stable, and extension-owned
objects are non-evacuated trace sources and targets. That assumption should not
be mistaken for a final major-GC design.
