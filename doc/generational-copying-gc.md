# Generational Copying GC Design Notes

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | Not started |
| Scope | Generational moving collection, root and slot rewriting, stable native objects, and native API compatibility |
| Owning layers | The memory manager owns collection; object layouts, root publication, and native APIs provide trace/update boundaries |
| Validated against | `ad0a158` (2026-07-18) |
| Supersedes | Earlier non-moving generational mark-sweep direction |

This document sketches an alternative garbage-collection direction for CloverVM:
a generational, moving collector that copies ordinary VM objects and handles
native API boundaries through explicit handles or stable wrappers.

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
- native code that uses a CPython Limited API / Stable ABI style surface sees
  stable, opaque `PyObject *` values with CPython-style reference lifetime
  rules;
- the stable `PyObject *` values are VM-owned native wrappers, not raw pointers
  to movable VM object bodies;
- extension-owned object bodies live on a stable heap and participate in the
  same opaque-pointer interface;
- raw storage exports such as buffers and string/bytes data use specialized
  pinning or stable-storage rules rather than making arbitrary objects
  non-moving.

The native API sections are part of the GC design because a moving collector
must know every root and must be able to update every reference to moved
objects. They are not the main collector policy.

Implementation staging belongs in
[Generational Copying GC Implementation Plan](generational-copying-gc-implementation-plan.md).

## Collector Model

The collector assumed by this proposal is generational, moving, and
stop-the-world. It is not concurrent or incremental in the initial design.

During a collection, mutator threads and native extension execution are stopped
at safepoints. The collector may copy movable VM objects, update managed roots,
repair managed heap references, update native wrapper target slots, and clean
dead zero-refcount wrappers before mutators resume.

This pause model is important for the stable-wrapper design. Native wrappers do
not need concurrent forwarding logic or read barriers merely to preserve
`PyObject *` stability. While native code is running, the wrapper's address is
stable. While the collector is running, native code is stopped and the VM can
rewrite wrapper target slots directly.

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
- CPython Limited API wrapper target slots;
- extension-owned stable objects that expose tracing hooks.

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

There are three native-facing APIs with different contracts. They should not be
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

### CPython Limited C API

The CPython Limited API / Stable ABI compatibility layer is the heaviest native
surface. It uses stable, opaque `PyObject *` values and CPython-style refcount
lifetime rules.

This layer needs stable wrapper identity for C-visible `PyObject *` values.
Unlike `clover_handle`, a `PyObject *` may be compared, increfed, decrefed, and
held according to CPython's reference rules. That requires a canonical stable
wrapper or stable extension object while native references exist.

The CPython Limited API layer remains restricted: downcasts to CPython internal
object layouts and direct field access are unsupported.

CloverVM presents a CPython Limited API with a compatible `PyObject` header, but
for VM-owned objects that header is only a shell. The authoritative object state
lives in the Clover object reached through the wrapper. An explicit wrapper flag
distinguishes VM-object proxies from real extension-owned objects, and CPython
type information for proxies is computed only on demand. The CPython C API
remains correct but intentionally slower; native Clover interfaces are the fast
path.

## Compatibility Target

The intended native compatibility target is closer to CPython's Limited API /
Stable ABI than to the unrestricted CPython C API.

Supported direction:

```text
extension code
  holds PyObject *
  uses supported refcount and type API over a compatible PyObject header
  calls other supported API functions/macros
  never relies on object body layout
```

Unsupported direction:

```text
extension code
  casts PyObject * to PyLongObject *, PyTupleObject *, ...
  reads or writes CPython object fields
  assumes PyObject * points at a CPython-shaped object body
```

The important invariant is:

```text
A PyObject * value observed by Limited-API native code remains stable for the
lifetime promised by refcount and borrowed-reference rules, but it does not
imply that the underlying VM object has a stable address or CPython-compatible
layout.
```

The external pointer is opaque. Internally, it may be an indirection cell whose
target is updated by the collector.

## Object Categories

This design separates four object/storage categories.

### Movable VM Objects

These are ordinary Clover/Python objects used by the interpreter and managed
code. They are allocated in generational moving heaps. A stop-the-world
young-generation collection may copy them and update all managed references
before execution resumes.

Interpreter and VM-internal code may use direct managed object references while
the object is protected by ordinary managed-root rules. Those direct addresses
must not escape to Limited-API native code as `PyObject *`.

### Stable Native Wrappers

A stable native wrapper is the C-visible representation of a managed VM object.
It is allocated in VM-controlled stable memory and has a stable address. Native
Limited-API code sees a pointer to this wrapper as `PyObject *`.

Conceptually:

```text
PyObject * observed by native code
  -> stable native wrapper
       target -> movable VM object
```

The wrapper behaves like an implementation-private cell: the wrapper identity is
stable, while the target slot may be rewritten when the target object moves.
This is similar to CPython closure cells as an indirection pattern, but the
wrapper is not a Python-visible `cell` object and must not expose cell semantics.

Every C-visible `PyObject *` allocation starts with a CPython-compatible object
header. For VM-object proxies, that header is the prefix of the stable native
wrapper. For extension-owned objects, it is the prefix of the extension-owned
allocation. The header should contain:

- a native-visible refcount, with the exact width still TBD; 64 bits may be
  necessary to match CPython's practical ABI expectations;
- a flags field. The initial required flag distinguishes VM-object proxies from
  extension-owned objects;
- a pointer to a Python type object. For VM-object proxies this pointer remains
  null permanently. For extension-owned objects it points at the real Python
  type object.

For VM-object proxies, `Py_TYPE` must not fill or cache the header type pointer.
It should dereference the wrapped Clover value each time, inspect the value's
current shape, read the class from that shape, and materialize a CPython type
wrapper for that Clover class object as needed. The class observed through a
shape can change, so caching the result in the proxy header would create stale
type information and require invalidation machinery.

Materialized CPython type wrappers still need stable identity. The VM should
keep a canonical mapping from Clover class objects to their CPython type wrapper
objects, so repeated materialization of the same Clover class produces the same
C-visible type object while that wrapper identity is live.

### Extension-Owned Objects

Objects whose body layout is controlled by a native extension cannot move,
because the extension may store fields at fixed offsets inside its own object
allocation. These objects live on a stable heap.

They still participate in the same external opaque-pointer interface: native
code receives a stable `PyObject *`, and VM API calls understand that this
pointer denotes an extension-owned object. Such objects need tracing hooks for
references back into movable VM objects, and refcount/lifetime rules for their
extension-controlled memory.

### Exported Storage

Some APIs expose raw memory rather than just object identity. Examples include:

- buffer protocol exports;
- raw bytes or string data;
- typed array storage;
- memoryviews;
- internal native fast paths that temporarily need a direct storage address.

These cases should not use general-purpose "pin any object" semantics. They
should use explicit storage export rules:

```text
object
  -> backing storage
       export_count
       movable normally, but non-moving or stable while exported
```

An active export may pin the backing storage, allocate it directly in stable
storage, or prevent resizing until the export is released. Pinning the entire
Python object should be a rare implementation detail, not the default native
interop mechanism.

## CPython Stable Wrapper Table

For the CPython Limited API layer, the VM maintains a canonical table from
managed object identity to stable native wrapper:

```text
VM object identity -> NativeWrapper *
```

This table is for ordinary VM objects exposed as `PyObject *`. It is distinct
from the Clover-class to CPython-type-wrapper identity map used by `Py_TYPE` for
VM-object proxies.

Looking up the same VM object for CPython Limited API exposure must return the
same wrapper while a live native reference to that wrapper exists. This
preserves `PyObject *` identity observations such as pointer equality for
simultaneously exposed values.

The table entry is an identity cache, not a permanent root. A wrapper with a
positive refcount is externally live and traces its target strongly. A wrapper
with refcount zero may remain in the table until a GC cleanup pass, but it does
not by itself keep the target object alive.

Wrapper states:

```text
refcount > 0:
  externally live
  trace target strongly
  update target if the object moves

refcount == 0:
  not externally live
  may remain cached temporarily
  does not by itself keep target alive
  removable during GC table cleanup
```

During collection, the VM should:

1. Treat positive-refcount native wrappers as roots.
2. Copy or mark their targets according to the managed heap policy.
3. Update wrapper target slots after object movement.
4. Remove zero-refcount wrappers from the identity table.
5. Free or recycle removed wrapper memory.

If a VM object remains alive after its zero-refcount wrapper is removed, a later
CPython Limited API exposure may create a different `PyObject *`. That is
acceptable because no native code may legally retain the old pointer after its
refcount or borrowed-reference lifetime has ended.

This table is not required for the CloverVM C API. `clover_handle` values do
not promise stable identity, so the VM can use cheaper per-context or
per-native-call handle storage.

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

## CPython Limited API Call Frames

Boundary bookkeeping should be centralized in a native-call adapter frame rather
than spread across individual call sites.

When managed code calls native Limited-API code, the adapter frame owns:

- the temporary `PyObject *` argument array or slots;
- temporary increfs that keep borrowed call arguments valid for the duration of
  the call;
- roots for any managed values needed while converting arguments or return
  values;
- cleanup records for decref on return or failure.

Conceptually:

```text
VM Value[] args
  -> NativeCallFrame
       expose each Value as canonical PyObject *
       incref wrappers needed for call lifetime
       call native function
       convert return value
       decref temporary wrappers in reverse order
```

Even when the native API says an argument is borrowed, the VM may internally
hold a temporary wrapper reference for the duration of the call. From the
extension's point of view the argument is borrowed; from the VM's point of view
the wrapper and target are protected across the native boundary.

This adapter also preserves identity for duplicate arguments. If two managed
arguments are the same object, exposing both through the frame must produce the
same stable wrapper pointer.

## CPython Wrapper Refcounts

Native wrapper refcounts describe the lifetime of the C-visible opaque pointer.
They do not imply that the movable target object itself is refcounted in the
same way.

The target's managed lifetime is determined by tracing:

```text
positive wrapper refcount
  -> wrapper is a native root
  -> wrapper traces target
  -> target remains live
```

`Py_INCREF` and `Py_DECREF` operate on the wrapper or stable extension object
seen by native code. If a `Py_DECREF` drops a wrapper to zero, the wrapper may be
eligible for identity-table cleanup at a later collection.

The design should avoid making every wrapper ever created a permanent strong
root. Otherwise repeated native exposure of short-lived values would leak object
graphs through the wrapper table.

`clover_handle` values do not use this refcount contract. Their lifetime is
owned by the active CloverVM C API context. A future persistent-root mechanism
is separate work.

## Cross-Heap References

This hybrid design has several important cross-boundary reference directions.

CloverVM C API handle to movable object:

```text
clover_handle -> VM-owned handle -> movable VM object
```

Live CloverVM C API handles are scanned as native roots. If a target moves, the
handle's value slot is updated. The handle itself does not need to be canonical
for the target object.

Stable wrapper to movable object:

```text
wrapper.target -> movable VM object
```

Positive-refcount wrappers are scanned as roots. If the target moves, the target
slot is updated.

Extension-owned object to movable object:

```text
stable extension object field -> VM object
```

Extension-owned objects need tracing hooks, equivalent in spirit to
`tp_traverse`, so the collector can discover references into moving heaps.
Writes from stable extension objects into young/movable generations also need
whatever remembered-set or barrier policy the chosen collector requires.

Movable object to stable object:

```text
VM object field -> stable extension object or wrapper-visible object
```

Managed tracing must understand stable objects as heap references. Stable
objects may be non-moving, but their liveness is still part of the object graph
unless they are immortal or otherwise explicitly outside collection.

Cycles that cross movable VM objects and extension-owned stable objects require
a deliberate policy. Refcounts alone are not enough for cycles that include
both heaps.

## Pinning Policy

Pinning is not the mechanism that makes `PyObject *` stable. Stable native
wrappers provide that property.

Pinning is only needed when native code has a raw address into an object body or
backing storage. The preferred policies are:

- expose raw storage through explicit export objects;
- count active exports;
- prevent movement or resizing of exported storage while the export is live;
- allocate frequently exported storage in a stable storage class if needed;
- keep arbitrary object-body pinning internal, scoped, and rare.

The design should avoid a public API that lets extensions request and retain
raw addresses for arbitrary object bodies. That would reintroduce the
unrestricted CPython C API's layout and address-stability assumptions.

## Open Questions

- What exact Limited API / Stable ABI version or subset is targeted first?
- Which macros are supported, and which are rejected because they imply layout
  access?
- What is the concrete CPython-compatible wrapper header layout: refcount width
  and flags packing?
- What lifetime and cleanup rules should the Clover-class to CPython-type-wrapper
  identity map use?
- How does the identity table key movable objects across copying collections:
  direct old address with forwarding repair, stable object ID, or side identity
  cell?
- When are zero-refcount wrappers swept: every collection, major collections
  only, or on wrapper-table pressure?
- How do extension-owned stable objects participate in cycle collection?
- Which storage classes support raw exports, and when do they pin versus move
  to stable storage?
- How are native-call adapter frames represented so GC can scan in-progress
  argument conversion and return conversion safely?
