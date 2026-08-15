# Generational Copying GC Design Notes

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | Not started |
| Scope | Generational moving collection, root and slot rewriting, pinning, stable GC participants, and CloverVM native handles |
| Owning layers | The memory manager owns collection; object layouts, root publication, and native APIs provide trace/update boundaries |
| Validated against | `d99f5e99` (2026-08-12) |
| Supersedes | Earlier non-moving generational mark-sweep direction |

This document sketches an alternative garbage-collection direction for CloverVM:
a generational, moving collector that copies ordinary VM objects and handles
native boundaries through updateable roots, pinning, and stable storage.

This design supersedes the earlier non-moving generational mark-sweep direction.
It allows ordinary VM objects to move, so it is incompatible with a collector
strategy that preserves raw managed-object addresses for native code.

The collector goal is a stop-the-world generational copying runtime:

- normal VM objects are allocated in moving generations and may be copied during
  stop-the-world collection;
- old or stable storage may use a non-moving policy where copying is not
  appropriate, such as extension-owned objects or exported storage;
- roots and heap references are updated after object movement.

Native API support follows from that collector choice:

- VM-internal intrinsic functions may use raw `Value` directly, but cannot let
  unrooted values survive safepoints;
- native code that uses CloverVM's own C extension API sees opaque
  `clover_handle` values that can survive collections through VM-managed
  indirection, but do not guarantee stable handle identity;
- CloverVM C API operations that expose raw object or backing-storage addresses
  establish scoped pins or use stable storage;
- non-moving objects still participate in tracing and remembered-set policy.

The proposed CPython Limited API wrapper system is specified separately in
[CPython Limited API Stable Wrappers](cpython-limited-api-stable-wrappers.md).
It is a prospective client of the collector's stable-storage and trace/update
contracts, not part of the collector itself.

## Collector Model

The collector assumed by this proposal is generational, moving, and
stop-the-world. It is not concurrent or incremental in the initial design.

During a collection, mutator threads and native extension execution are stopped
at safepoints. The collector may copy movable VM objects, update roots, repair
managed heap references, and trace non-moving participants before mutators
resume.

## Copying Collection Shape

The ordinary managed heap should be designed around evacuation: live objects in
the collected generation are copied to new storage, and every root or heap slot
that referenced the old address is rewritten to the new address before
execution resumes.

A collection has three broad phases:

1. Stop mutators at safepoints and publish all VM-controlled roots.
2. Evacuate reachable movable objects from the collected generation.
3. Rewrite roots, handle slots, and heap references to point at copied objects.

The evacuation algorithm should use a Cheney scan with breadth-first
traversal. Roots and remembered old references copy their young targets into
to-space and append those copied objects to the evacuation worklist. The
collector then advances a scan frontier through to-space: scanning each copied
object's outgoing references, copying any not-yet-forwarded collected objects,
and appending those copies after the frontier. The worklist is complete when the
scan frontier catches up with the allocation frontier.

The collector must be able to enumerate and update precise slots, not just
discover object identities:

- managed stack slots and accumulators;
- VM/runtime roots;
- ordinary object fields and backing arrays;
- remembered old-to-young references for minor collections;
- CloverVM C API handle storage;
- pinned and stable objects that expose trace/update hooks.

Objects in stable storage are not copied by this ordinary evacuation path. They
are still part of the traced object graph if they can reference or be referenced
by movable VM objects.

## Heap Object Metadata

The collector keeps object-layout metadata separate from GC state and extends
the existing native-layout descriptor system rather than introducing a second
GC descriptor framework. The authoritative descriptor query, size, contiguous
slot-span, weak-reference, teardown, and copy-policy design is in
[Native Layout Descriptors](native-layout-descriptors.md).

In summary, one descriptor lookup and per-object query report allocated and
initialized byte extents plus one contiguous strong owned `Value` span. Those
slots are traced and rewritten in one pass and, while deferred refcounting
remains, released as the same ownership set. Weak references are enumerated
separately after strong closure; an ordinary weak object still retains its
strong `shape` slot.

The GC-specific object state needed for ordinary copied objects is much smaller:

- normal versus forwarded state;
- forwarding target when the object has been evacuated.

The current eight-byte header is occupied by deferred-refcount and native-layout
state. Collector migration must not remove that state before its replacement is
working. The transition therefore accepts an oversized header carrying both the
existing refcount/lifecycle fields and the new GC fields. Once tracing is the
lifetime authority and the ZCT is retired, the refcount fields can be removed
and the final header repacked.

The GC header must represent at least generation/remembered state and whether an
otherwise movable object is pinned. The exact pin-count or pin-flag encoding is
not yet selected. Allocation space supplies the normal movement policy, but is
not sufficient by itself: a pinned object in a normally moving space must not be
evacuated until its pin is released.

The forwarding target can be written into the from-space object itself after
the object has been copied. A reference repair path can then recognize a
forwarded object and replace the old address with the forwarding target.

For copied generations, classic white/grey/black mark bits do not need to live
in every object header. The states are implicit:

```text
not forwarded in from-space = white
forwarded but not scanned = grey
forwarded and scan complete = black
```

The grey/black distinction can be represented by the evacuation scan queue and
its scan frontier rather than by header bits. With the Cheney scan, copied
objects before the scan frontier are black, copied objects between the scan
frontier and the to-space allocation frontier are grey, and not-yet-forwarded
objects in from-space are white.

The first implementation should use object-level generation state in the header:

```cpp
enum class HeapGenerationState : uint8_t {
    Young,
    Old,
    OldRemembered,
};
```

`OldRemembered` means the object is old and may contain young references, so
minor GC must scan it. The state is conservative: an object may remain
`OldRemembered` after a young reference is overwritten until a minor collection
rescans it and clears the state.

Forwarding state is separate from generation state. `Forwarded` is a temporary
evacuation state that changes how from-space memory is interpreted.
`OldRemembered` is a persistent generational barrier state.

## Stable JIT Metadata and Compiled Constants

Shapes and validity cells are compiler-facing identity metadata and are
allocated from dedicated non-moving stable pools. Generated machine code may
embed their addresses directly. Every compiled code object also records those
addresses in a stable-metadata array; the collector treats that array as the
authoritative lifetime and tracing boundary for metadata referenced by code.
Pool entries may be reclaimed and their addresses reused only after no runtime
object, IC, compilation session, or executable compiled code references them.

Managed Python constants remain movable. Each compiled code object owns a
separate constant array whose slots have stable addresses but whose `Value`
contents are traced and rewritten by the collector. Machine code reaches these
slots through PC-relative loads and must not embed the current managed-object
pointer as an immediate. Collection therefore updates constant slots without
decoding, rewriting, or making instruction pages writable.

The JIT backend records both kinds of reference during emission, and compiled
code verification checks that every embedded stable-metadata pointer is listed
and every managed constant use names a traced slot. Compiled code and both
arrays remain alive as one lifecycle unit. Stable-pool entries that themselves
own managed references participate in ordinary precise tracing even though the
entries are not relocated.

## Generations

The first policy should be a stop-the-world generational collector:

- young generation for newly allocated movable objects;
- old generation for objects that survive a minor collection;
- stable storage for extension-owned objects and storage that must not move.

Young collections evacuate live young objects and update references from roots
and remembered old objects. They must not scan the entire old generation.
Old-to-young stores therefore need a remembered-set barrier, even though the
collector itself is stop-the-world.

The simple first policy is immediate promotion: every live young object that
survives a minor collection is copied out of the nursery and becomes old. Dead
young objects are not copied. After the minor collection, the nursery can be
reused wholesale.

Minor collection should evacuate the full young generation reachable from roots
and remembered old objects. Promoted objects should normally end the collection
with old-to-old references to other promoted young survivors. New old-to-young
references after the collection are created by later heap stores and recorded by
the write barrier.

The baseline write barrier is object-level:

```cpp
if(owner->generation_state == HeapGenerationState::Old &&
   child_is_young(child)) {
    owner->generation_state = HeapGenerationState::OldRemembered;
    thread->remembered_set.push_back(owner);
}
```

If the owner is already `OldRemembered`, the barrier does nothing. The
`Old -> OldRemembered` transition is the duplicate-entry guard.

Remembered sets store old objects that may contain references into the nursery.
They never store nursery objects.

Each `ThreadState` should own a remembered set. That keeps ordinary barriers
lock-free in the common case: a mutator records old-to-young stores in its own
thread-local vector. In a future no-GIL implementation where multiple threads
can write the same old object, the `Old -> OldRemembered` transition must become
atomic so only one thread records the object.

During stop-the-world minor GC, the collector scans the remembered sets for all
threads. For each remembered old object, it scans and updates references using
the layout descriptors. With the initial en-masse promotion policy, every live
young target found by roots or remembered old objects is promoted during the
minor collection. After the remembered object has been scanned and repaired, the
collector clears its remembered state back to `Old`; future old-to-young stores
rebuild remembered-set entries through the write barrier.

This object-level remembered policy is intentionally coarse. Large backing
stores, module globals, or other high-churn structures can later receive more
precise slot/range tracking if measurements show that rescanning the whole
object is too expensive.

The write-barrier contract is:

- every heap store that can create an old-to-young edge must execute exactly one
  appropriate write-barrier path;
- the barrier observes the final stored child value;
- the barrier may conservatively over-report old objects;
- the barrier must never under-report old objects that can point into the
  nursery.

Construction paths, young-owner stores, immediate values, and caller-proven
old-child stores may use specialized no-op paths, but the proof must be local to
the store path and must not hide a possible old-to-young edge.

### Write Barrier Implementation Lessons

A minimal remembered-set experiment showed that the barrier is not free, but it
is not a deal breaker if the hot-path entry points are shaped carefully.

The correctness policy can be shared:

```text
old owner stores young child -> remember old owner
```

The hot implementation should not force every caller through one generic helper.
Different stores know different facts, and the barrier API should expose those
facts so irrelevant checks disappear from common paths:

- a `Value` store should reject non-refcounted immediates before computing the
  owner or checking generation state;
- a caller that already proved the child is refcounted should use a
  known-refcounted entry point and skip repeated tag/pointer validation;
- a caller that already has `ThreadState *` should pass it to the barrier;
- a caller without `ThreadState *` may lazily call `active_thread()`, but only
  after proving that the store really needs to record an old-to-young edge;
- `ValueArray<Value>` should use a single-cell fast path;
- composite value arrays, such as dictionary entries, should scan their value
  cells, compute the backing owner only if at least one cell is refcounted, and
  then reuse the same owner for all remembered checks.

This matters for dense container writes. A list item benchmark that writes only
small integers should not pay for remembered-set insertion or thread-local state
lookup. Profiling confirmed that the hot cost was not TLS lookup: the benchmark
never reached the actual record path because the stored values were immediates.
The cost came from entering a generic element barrier and doing owner/backing
and generation-shaped work before rejecting the immediate value.

Factoring the helper so `ValueArray<Value>` rejects non-refcounted values first
recovered a meaningful part of the regression, but the list item write path
still remained visibly slower than the no-barrier baseline. The lesson is that
shared barrier tests and shared correctness policy are good; a single shared
hot-path implementation is not. Barrier entry points should be small and
specialized by caller knowledge, with the generic helper reserved for callers
that really do not know more.

Module globals are the first likely refinement. If module slot storage can keep
a cheap remembered bit close to each slot, then a global write of a young value
into an old module should record the exact slot instead of remembering the whole
module object:

```text
remembered module slot entry:
  module object
  slot index
```

The per-slot remembered bit is the duplicate-entry guard for that slot. Minor GC
can scan only the recorded module slots, update copied young targets, and clear
the slot bit when the slot no longer contains a young reference. This keeps
module global churn from forcing every minor GC to rescan all module storage.

That refinement should not be part of the first collector. The initial collector
should remember whole module objects or use a dirty-module list, and it should
not make modules permanent roots or scan all modules on every minor collection.
More precise module-slot entries should wait until profiling shows module
scanning is a material minor-GC cost.

Major collections may collect both young and old movable generations. The exact
old-generation policy is still open: old objects may continue to move during
major collection, or the old generation may use a non-moving mark-sweep policy
for objects that are expensive or risky to copy. That choice is independent from
the C API handle contract: native code still cannot observe raw addresses for
ordinary movable objects.

## Native API Layers

There are two current native-facing APIs with different contracts. They should not be
collapsed into one handle model.

### Intrinsic API

The intrinsic API is VM-internal. Intrinsic functions expose raw `Value`
directly and are meant for trusted runtime helpers and builtins that participate
in CloverVM's internal calling convention.

This API has the weakest boundary guarantees:

- raw `Value` may contain direct pointers to movable VM objects;
- intrinsic code must not keep unrooted values across safepoints;
- intrinsic code must not store `Value` outside VM-owned rooted storage unless
  the storage participates in managed tracing/update;
- intrinsic calls should be treated as part of the VM implementation, not as an
  external extension ABI.

If an intrinsic can allocate, call back into Python, trigger collection, or
otherwise reach a safepoint, its live values must be visible through ordinary VM
roots, managed frames, or explicit VM-rooted handles before the safepoint.

### CloverVM C Extension API

The CloverVM C extension API uses `clover_handle`. This is the preferred native
extension surface for Clover-specific modules. This name is intentional: in a
moving collector the API value is a handle to VM-owned storage, not the VM value
representation itself.

Unlike the intrinsic API, this API may call helpers that allocate and trigger
collection. Therefore `clover_handle` cannot remain a raw copy of movable
`Value` bits in a moving collector. It must become an opaque VM-managed token or
handle that the active `clover_context` can trace and update.

The CloverVM C API does not need to preserve handle identity. If the same VM
object is exposed twice as `clover_handle`, the two `clover_handle` values do not
need to compare equal unless a specific API function promises that. Extension
code should treat `clover_handle` as an opaque value handle, not as an identity
pointer.

This weaker contract avoids the cost of a global canonical wrapper table for
ordinary Clover-native extensions. `clover_handle` can be an opaque pointer to
VM-owned handle storage. The handle storage is scanned and updated by the GC,
but it does not need to be canonical for object identity.

Ordinary `clover_handle` values are transitory handles, valid only for the
active API entry that produced or received them. Persistent native roots are
explicitly deferred and are not specified by this design.

### Prospective CPython Limited API

The collector must support the following requirements without owning the full
interop design:

- a C-visible `PyObject *` has stable identity for its promised native lifetime;
- a stable wrapper for a VM object contains an updateable reference to the
  movable managed target;
- positive native wrapper references act as strong collector roots;
- extension-owned bodies contain stable `PyObject *` references rather than
  direct pointers to movable managed objects;
- the collector traces and updates wrapper target slots, without scanning or
  applying Clover write barriers inside opaque extension-owned bodies.

The wrapper ABI, canonical identity table, native refcount mechanics, and call
adapters live in
[CPython Limited API Stable Wrappers](cpython-limited-api-stable-wrappers.md).

## Object And Storage Categories

### Movable VM Objects

These are ordinary Clover/Python objects used by the interpreter and managed
code. They are allocated in generational moving heaps. A stop-the-world
young-generation collection may copy them and update all managed references
before execution resumes.

Interpreter and VM-internal code may use direct managed object references while
the object is protected by ordinary managed-root rules. A direct address exposed
outside managed execution requires a pin or stable placement for the complete
exposure lifetime.

### Stable GC Participants

Objects that require stable addresses are not outside GC. They remain trace
sources and targets, may require remembered-set barriers for nursery references,
and may be collectible by a non-moving policy. Examples include native handle
storage and pinned or permanently stable backing storage. CPython
extension-owned bodies are different: their managed indirection terminates at
stable `PyObject *` wrappers, as specified in the separate wrapper design.

### Exported Storage

Some APIs expose raw memory rather than just object identity. Examples include:

- buffer protocol exports;
- raw bytes or string data;
- typed array storage;
- memoryviews;
- internal native fast paths that temporarily need a direct storage address.

These cases use explicit storage export and pinning rules:

```text
object
  -> backing storage
       export_count
       movable normally, but non-moving or stable while exported
```

An active export may pin the backing storage or object, allocate it directly in
stable storage, and prevent resizing until the export is released. Which unit is
pinned follows the address actually exposed by the C API.

## CloverVM C API Handles

The compile-time-switchable representation and storage design is specified in
[Switchable Indirect Native Handles](indirect-native-handles.md). The public
`clover_handle` remains an opaque machine word in both modes.

With indirect handles enabled, a handle is internally a pointer to a rooted
`Value` slot. Incoming arguments use their existing managed frame slots. Values
created through the C API use fixed storage in that frame and then stable,
managed `HandleChunk` objects linked through the final cell of each chunk. The
collector updates the slot when its target moves; the handle pointer itself
remains unchanged.

Transitory `clover_handle` values are owned by the active native call and
`clover_context`. Native code must not retain them in globals, module state,
native heap allocations, callbacks, or any other storage that outlives that
call. A future persistent-root API will be designed separately when an existing
native-module use requires it.

```text
clover_handle
  -> rooted Value slot
       storage -> managed native frame or stable managed HandleChunk
       value -> movable VM object or immediate Value
       lifetime -> active native call
```

Every CloverVM C API function resolves the opaque handle and then reads or
updates its value slot:

```text
clover_* API(ctx, handle)
  slot = resolve_handle(handle)
  read or update *slot
```

A valid active `clover_context *` and handles produced for that context are API
preconditions. Null, fabricated, foreign, stale, or otherwise invalid handles
are extension misuse; runtime validation is not part of the contract.

When a CloverVM C API operation stores a value into VM-owned storage, it stores
the underlying managed value, not the `clover_handle` itself. For example, a
future `clover_list_set_item(ctx, list, index, item)` would resolve `item`
through its handle and then perform a normal managed list store
with the collector's write barrier. The list would not retain the transitory
handle, and the native extension could not keep using `item` after the call
returns.

The same object exposed twice may receive two distinct `clover_handle` values.
That is allowed unless a specific CloverVM C API function documents identity
semantics. Extension code must use API operations for equality, hashing, and
object behavior rather than comparing handle pointer values.

Native code must not rely on transitory handles surviving after the current API
entry returns. Persistent native roots remain deferred.

## Cross-Space References

CloverVM C API handle to movable object:

```text
clover_handle -> VM-owned handle -> movable VM object
```

Live CloverVM C API handles are scanned as native roots. If a target moves, the
handle's value slot is updated. The handle itself does not need to be canonical
for the target object.

Stable participant to movable object:

```text
stable object field -> movable VM object
```

Stable objects expose updateable trace slots so the collector can discover and
repair references into moving spaces. Writes from stable objects into the
nursery obey the remembered-set/barrier policy.

Movable object to stable object:

```text
movable object field -> stable object
```

Managed tracing must understand stable objects as heap references. Stable
objects may be non-moving, but their liveness is still part of the object graph
unless they are immortal or otherwise explicitly outside collection.

Cycles that cross movable and stable participants remain part of one tracing
graph unless a particular stable category defines a different lifetime policy.

## Pinning Policy

Pinning is required when CloverVM's C API or an internal native operation exposes
a raw address into an otherwise movable object or backing store. A
`clover_handle` alone does not pin its target because the handle denotes an
updateable slot rather than the target address.

The policies are:

- expose raw storage through explicit export objects;
- count active exports;
- prevent movement or resizing of exported storage while the export is live;
- allocate frequently exported storage in a stable storage class if needed;
- make pin acquisition and release scoped and nestable;
- keep long-lived pins uncommon and move frequently exported storage into a
  stable class when that is cheaper.

Pinning overrides the normal movement policy implied by generation and
allocation space. A collection must retain a pinned object at its current
address, while still tracing and updating its outgoing references. The header
must contain enough state to answer whether the object may move; the precise
pin-count or flag representation remains open.

## Open Questions

- What is the transitional and final packed GC header layout?
- Is pinning represented by an in-header count, an in-header flag with external
  accounting, or another scheme?
- How are pinned nursery objects handled when the rest of the nursery is
  evacuated?
- Which storage classes support raw exports, and when do they pin versus move
  to stable storage?
- What major-collection policy applies to old and stable participants?
