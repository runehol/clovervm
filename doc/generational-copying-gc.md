# Generational Copying GC Design Notes

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | Not started |
| Scope | Generational moving collection, root and slot rewriting, stable placement and exports, and CloverVM native handles |
| Owning layers | The memory manager owns collection; object layouts, root publication, and native APIs provide trace/update boundaries |
| Validated against | `d99f5e99` (2026-08-12) |
| Supersedes | Earlier non-moving generational mark-sweep direction |

This document sketches an alternative garbage-collection direction for CloverVM:
a generational collector that copies ordinary VM objects and handles native
boundaries through updateable roots and a separate permanently non-moving heap.

The [implementation plan](generational-copying-gc-implementation-plan.md)
stages scanning, mutable roots, and write barriers alongside deferred
refcounting, followed by the tracing lifetime cutover and production movement.
It also defines the boundary for an isolated collector algorithm and Lean model.
The concrete template/heap-adapter design is kept separately in
[Isolated Collector and Heap Adapter](gc-collector-heap-adapter.md).

This design supersedes the earlier non-moving generational mark-sweep direction.
It allows ordinary VM objects to move, so it is incompatible with a collector
strategy that preserves raw addresses for all managed objects. Raw-address
exposure beyond a relocation-exclusion scope instead requires permanent
placement in the non-moving heap.

The collector goal is a stop-the-world generational copying runtime:

- normal VM objects start in a moving nursery and are promoted on survival;
- the movement policy for the ordinary old generation remains open;
- a separate permanently non-moving heap holds extension-owned objects and
  objects or backing storage whose raw addresses must be exposed;
- roots and heap references are updated after object movement.

Native API support follows from that collector choice:

- VM-internal intrinsic functions may use raw `Value` directly, but cannot let
  unrooted values survive safepoints;
- native code that uses CloverVM's own C extension API sees opaque
  `clover_handle` values that can survive collections through VM-managed
  indirection, but do not guarantee stable handle identity;
- CloverVM C API operations that expose raw object or backing-storage addresses
  ensure stable placement and export/lifetime protection, or use the restricted
  scoped string-access option described below;
- non-moving objects still participate in tracing and remembered-set policy.

The proposed CPython Limited API wrapper system is specified separately in
[CPython Limited API Stable Wrappers](cpython-limited-api-stable-wrappers.md).
It is a prospective client of the collector's stable-storage and trace/update
contracts, not part of the collector itself.

The sections below separate the collector's movement/tracing rules from native
root publication, stable buffer exports, and unresolved identity choices.
Stable export shadows are the preferred buffer direction; whole-object stable
relocation remains a separate option with unresolved old-generation repair.

## Collector Model

The collector assumed by this proposal is generational, moving, and
stop-the-world. It is not concurrent or incremental in the initial design.

During a collection, mutator threads and native extension execution are stopped
at safepoints. The collector may copy movable VM objects, update roots, repair
managed heap references, and trace non-moving participants before mutators
resume.

## Heap Spaces and Movement Policy

The first policy should be a stop-the-world generational collector:

- young generation for newly allocated movable objects;
- old generation for objects that survive a minor collection;
- a separate permanently non-moving stable heap for extension-owned objects
  and objects or backing storage that must expose stable raw addresses.

The stable heap is separate from the ordinary old generation. Objects may be
allocated there directly or promoted there before raw-address exposure. Stable
placement is represented by a per-object NonMoving header bit, independent of
generation and native layout. Layout/storage policy determines its initial
value; a normally movable object may later become permanently non-moving.
Promotion to NonMoving is one-way. Full pinning support, including any pin
bits/counts, is deferred until reference mapping is in place.

Stable placement is permanent for the allocation, including after its last export is
released; stable objects remain collectible and participate in tracing and
remembered-set policy.

Major collections may collect young, ordinary old, and stable objects. The
ordinary old-generation policy remains open: old objects may move during major
collection, or the old generation may use a non-moving policy. Stable-heap
objects never move under either choice. The exact reclamation policy for stable
participants remains to be specified. Native handles remain updateable. Raw
addresses exposed beyond a relocation-exclusion scope require stable storage.

## Object and Storage Categories

### Movable VM Objects

These are ordinary Clover/Python objects used by the interpreter and managed
code. They start in the moving nursery and normally survive into the ordinary
old generation, whose movement policy remains open. A stop-the-world
young-generation collection copies nursery survivors and updates managed
references before execution resumes.

Interpreter and VM-internal code may use direct managed object references while
the object is protected by ordinary managed-root rules. A direct address exposed
outside managed execution requires permanent stable placement before exposure
and lifetime protection, except for the restricted scoped string-access option
below.

### Stable GC Participants

Objects that require stable addresses are not outside GC. They remain trace
sources and targets, may require remembered-set barriers for nursery references,
and may be collectible by a non-moving policy. Examples include native handle
storage, promoted objects, and permanently stable backing storage. CPython
extension-owned bodies are different: their managed indirection terminates at
stable `PyObject *` wrappers, as specified in the separate wrapper design.

## Copying Collection Shape

The ordinary managed heap should be designed around evacuation: live objects in
the collected generation are copied to new storage, and every root or heap slot
that referenced the old address is rewritten to the new address before
execution resumes.

A collection has three broad phases:

1. Stop mutators at safepoints and publish all VM-controlled roots.
2. Evacuate reachable movable objects from the collected generation.
3. Rewrite roots, handle slots, and heap references to point at copied objects.

The evacuation algorithm uses one explicit double-ended work queue. Roots and
remembered references copy their collected targets into destination storage,
install source forwarding, and enqueue every newly copied object. Pop each
object before scanning its strong slots, copying/forwarding children and
repairing references as they are visited. A later encounter with a forwarded
source only repairs the incoming slot; it does not copy or enqueue again.

Layout code also enumerates weak-containing objects/embedded structs as
[weak repair units](gc-weak-repair-units.md). Resolve uncollected or already
forwarded targets immediately; remember only exact units with unresolved
collected targets, not whole owners. After strong closure, repair surviving
targets or invalidate the unit, including its payload and owned resources.
Never discover strong liveness through a weak reference. Old/stable units that
are not scanned by copying still require separate weak-remembered coverage.

Ordinary old-generation destinations and direct stable promotions enter the
same queue. Stable placement never exempts a copied object from scanning.
Continue until the queue is empty after processing the active object. Only
then is strong evacuation closure complete; finish weak/reference repair before
reclaiming the nursery or resuming mutators. In particular, a nursery child
reachable only through a newly stable object must be copied and its incoming
slot repaired too.

This is worklist copying, not a Cheney allocation-frontier scan. Scanning does
not require walking slabs, finding arbitrary object starts, or using the current
valid-object bitmap. Removing bitmap work from young allocation is an eventual
allocator goal, not an instruction to remove bookkeeping still needed by
current RC reclamation or stable storage. The queue holds pending objects, not
processed objects; a wide live graph can still create a large pending frontier.

Destination storage grows by acquiring slabs without relocating earlier copies.
Each object is copied and forwarded independently. Overflow storage is reached
through the owner's ordinary strong reference, with no adjacency guarantee.
Co-placement of owner and backing storage is deferred until production
collection works and its locality benefit can be measured. Young-only and full
moving-source selections use the same single-object evacuation contract.
See the [adapter design](gc-collector-heap-adapter.md) for the concrete contract.

Collector-only allocation during slot enumeration is allowed provided it cannot
trigger collection or mutator execution and cannot invalidate the object/slots
being scanned. Failure to allocate destination slabs, work-queue storage, or
required copying metadata is fatal in the initial design: terminate, do not
attempt rollback or resume with a partially rewritten heap. Slab rollover is
normal growth, not exhaustion. No whole-heap reservation or live-size prepass
is required by this policy.

The collector must be able to enumerate and update precise slots, not just
discover object identities:

- managed stack slots and accumulators;
- VM/runtime roots;
- ordinary object fields and backing arrays;
- remembered old-to-young references for minor collections;
- CloverVM C API handle storage;
- stable objects that expose trace/update hooks.

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
copy byte extents plus an optional contiguous strong owned `Value` span. Raw
backings copy their full byte extent without scanning their payload as pointers;
lazy instances copy only their initialized prefix. Custom scanners remain
separate from an absent or empty ordinary span. Those ordinary strong
slots are traced and rewritten in one pass and, while deferred refcounting
remains, released as the same ownership set. Weak references are enumerated
separately after strong closure; an ordinary weak object still retains its
strong `shape` slot.

`Instance` retains lazy inline-slot initialization. Its allocated-size query
uses the current shape's inline capacity, while the existing auxiliary
header count continues to describe only the initialized inline prefix.
Shape-derived queries require forwarding-safe metadata access before
ordinary slot repair; see the layout document's
[extent source contract](native-layout-descriptors.md#extent-source-invariant).

A later optimization may shrink a class's generous initial inline capacity to
the maximum inline-slot frontier reached by its instances at the shrink point.
Existing inline locations remain unchanged, and subsequent growth beyond the
reduced capacity uses overflow storage. Evacuation can reclaim excess reserved
bytes from surviving young instances by allocating the reduced extent and
copying their initialized prefix. Profiling policy and shape-transition updates
are future work, detailed under
[instance capacity shrinking](native-layout-descriptors.md#future-instance-capacity-shrinking).

The GC-specific object state needed for ordinary copied objects is much smaller:

- normal versus forwarded state;
- per-object NonMoving state, orthogonal to generation;
- forwarding target when the object has been evacuated.

The current eight-byte header is occupied by deferred-refcount and native-layout
state. Collector migration must not remove that state before its replacement is
working. The transition keeps eight bytes by retaining narrow native-layout/aux
fields and packing generation, NonMoving, and RC lifecycle into one byte, alongside the
four-byte refcount. Once tracing is lifetime authority and the ZCT is retired,
RC and lifecycle can be removed and the final header repacked with wider fields.

The accepted layout and encoding are specified in
[GC Object Header](gc-object-header.md): an eight-byte RC transition and a final
eight-byte header with byte-sized GC state, a reserved byte, a 16-bit native
layout ID, and a 32-bit aux count. The final header is destructively replaced by
an encoded forwarding pointer that preserves source generation in bits 0--1.
In the accepted next encoding, bit 2 is NonMoving; pointer-tag bits `0x18`
identify forwarding and must be zero in normal headers. Bit 5 is spare and
normal-state RC lifecycle occupies bits 6--7. This header protocol is implemented;
central initial NonMoving construction policy remains pending, and current
constructors leave the bit clear.
Generation reads use a byte load and low-two-bit mask in either header form.
Forwarding-target reads clear only bits 0--1, preserving every address bit above
them. Forwarding overwrites RC and lifecycle, so this header slice does not
enable production forwarding. Any future overlap
with RC requires a separate forwarding-aware reclamation/metadata protocol;
the accepted staging still transfers lifetime authority before evacuation.

The GC state must represent generation/remembered state and any state required
by the eventual old-generation and stable-heap collection policies. Allocation
space and the per-object NonMoving bit must agree: objects in stable storage
never move. A layout's initial policy is not the lifetime policy of every
instance; a movable string/backing can later be promoted to stable storage and
marked NonMoving before its address is exposed. Do not mark a nursery source in
place and then discard its storage. Reference repair completes before exposure;
the old source remains movable/forwarded and the destination is NonMoving.
Future pin bits/counts are separate, deferred work, not implemented by this bit.
Export accounting still protects lifetime and prevents address-invalidating
operations; its representation remains a separate storage/API concern.

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

The grey/black distinction is represented by the work queue and the active scan,
not an allocation frontier or additional header bits. Forwarded destinations
awaiting or undergoing scanning are grey; completed objects are black. The same
rule applies to ordinary and stable destinations. Popping a queue entry starts
its scan; it is not complete until all of its strong slots have been processed.

The first implementation should use object-level generation state in the header:

```cpp
enum class HeapGenerationState : uint8_t {
    Young         = 0x01,
    Old           = 0x02,
    OldRemembered = 0x03,
};
```

`OldRemembered` means the object is old and may contain young references, so
minor GC must scan it. The state is conservative: an object may remain
`OldRemembered` after a young reference is overwritten until a minor collection
rescans it and clears the state.

Forwarding is a temporary evacuation representation, not a fourth generation.
It replaces normal metadata but preserves source generation, even when the
destination belongs to a different generation. `OldRemembered` remains an
object-level barrier fact; a forwarded source retains its pre-copy generation.
Layout inspection, source selection, and weak repair must follow the
[destructive-forwarding contract](gc-object-header.md#destructive-forwarding-contract).

## Generational Barriers and Minor Collection

Young collections evacuate live young objects and update references from roots
and remembered old objects. They must not scan the entire old generation.
Old-to-young stores therefore need a remembered-set barrier, even though the
collector itself is stop-the-world.

The simple first policy is immediate promotion: every live young object that
survives a minor collection is copied out of the nursery and becomes old. The
normal destination is the ordinary old generation; objects selected for direct
stable promotion go to the stable heap and are treated as old for generational
barriers. Export shadows can instead provide stable data without relocating the
original. Dead young objects are not copied. After the minor collection, the
nursery can be reused wholesale.

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

Remembered sets store old objects that may contain references into the nursery,
including stable-heap participants. They never store nursery objects.

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

## Python Object Identity

`id(obj)` must remain constant throughout the object's lifetime and be unique
among simultaneously live objects, including across nursery evacuation,
promotion to stable storage, and any old-generation relocation. Relocation must
not be observable as a change in `id(obj)`. Returning the current memory address
is therefore insufficient for movable objects. The identity representation and
how it survives relocation remain open; this requirement does not select an
incoming-reference index or general reference indirection scheme. See the
[Python identity contract](https://docs.python.org/3/library/functions.html#id).

### Hashing and Float Identity Decision

Relocation must also preserve identity-derived hashes used by containers. The
current NaN hash uses the object address and needs a relocation-safe policy
before objects can move. One open alternative is PyPy-style NaN bit-pattern
hashing; finite-float hashing must still agree with equal integers. Adopting
NaN bit-pattern hashing alone would let distinct NaNs with identical bits share
a hash, potentially creating collision-heavy dictionaries. PyPy also treats
builtin floats with matching bit patterns as identical under `is`, changing
`id()` and container behavior. Whether to change hashing alone, adopt those
identity semantics too, or preserve per-object identity hashing is undecided.
These are separate decisions; no float identity change is implied here. See
[PyPy's float identity semantics](https://doc.pypy.org/en/latest/cpython_differences.html#object-identity-of-primitive-values-is-and-id).

### PyPy Shadow Reservation Option

One option to consider is PyPy's `incminimark` shadow approach. When an identity
or identity hash is requested for a nursery object, the collector reserves a
same-sized destination outside the nursery and records it as that object's
shadow. The reported identity uses the reserved destination's address; the
object stays in the nursery until a later minor collection copies it into that
destination. This avoids forcing immediate evacuation just to obtain an
identity, and ordinary references still point directly to the object rather
than through the shadow. See
[PyPy's shadow and identity implementation](https://github.com/pypy/pypy/blob/main/rpython/memory/gc/incminimark.py#L2694-L2724).

This option relies on objects outside the nursery remaining non-moving, so the
reserved address remains valid as an identity after promotion. Clover's
old-generation movement policy is still open, and even non-moving ordinary old
storage would not by itself cover a later old-to-stable relocation. Reserving
permanently stable destinations or preserving identity independently of later
addresses would require further design. The shadow approach is an alternative
to evaluate, not the selected identity mechanism or an implementation plan.

### Sharing Identity and Export Shadows

The export-shadow direction below suggests a related `id()` option: associate
an object with a permanently stable shadow, use the shadow address as its
identity, and leave ordinary references pointing to the original until a
collection can repair them together. Identity lookup could reuse a compatible
export shadow rather than introduce general reference indirection. If identity
was observed before export acquisition, that existing identity must not change;
export and identity shadows therefore need a coherent association, not two
independently chosen addresses. Reservation size/layout, lookup repair after
movement, lifetime/reclamation, and whether these shadows can actually be shared
remain open. Reserving a permanently stable identity destination could avoid
depending on non-moving ordinary old storage, but is not yet a selected scheme.

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

### CloverVM C API Handle Storage

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

## Stable Buffer Exports

Some APIs expose raw memory rather than just object identity. Examples include:

- buffer protocol exports;
- raw bytes or string data;
- typed array storage;
- memoryviews;
- internal native fast paths that temporarily need a direct storage address.

Exports use explicit storage export and stable-placement rules. Temporary string
access through the CloverVM C API may instead use the scoped callback described
below:

```text
object
  -> backing storage
       export_count
       permanently non-moving before the address is exposed
```

Before an export exposes an address, the allocation actually supplying that
address must reside in stable storage. It need not be the original managed
object: an immutable export may use a stable shadow. Stabilizing an object does
not stabilize a separately allocated backing store. An active export keeps the
exposed allocation alive and prevents operations that would invalidate the
address until the export is released.

### Lifetime and Address Stability

A `clover_handle` alone does not pin its target because the handle denotes an
updateable slot rather than the target address. When CloverVM's C API, the
CPython C API, or an internal native operation needs a raw address into an object
or backing store beyond a relocation-exclusion scope, it must establish
permanent stable placement before exposing that address. The CloverVM C API
string callback described below is the restricted temporary-access exception;
querying size and copying into a caller buffer exposes no managed address.
There are no pinned objects in the nursery or any other moving space.

Stable placement persists after export release, so later collections cannot
invalidate an exposed address by moving the allocation. It does not by itself
keep the allocation alive or prevent backing-store replacement. Exports must
therefore retain the exposed allocation, count active exports where needed, and
prevent resizing or other address-invalidating operations for the promised
exposure lifetime. Acquisition and release remain scoped and nestable. These
are export/lifetime rules, not GC movement-pin state.

Stable shadows and backing stores that contain managed references expose
precise trace/update slots. Their references into moving spaces are repaired
during collection, and nursery references obey the remembered-set barrier
contract.

### Preferred Buffer Export Direction: Stable Shadows and Backing Stores

For immutable inline data, create or reuse a stable shadow containing a copy of
the exported contents. Keep ordinary managed references pointing to the original
object and use an identity-style lookup to find its shadow for subsequent
exports. The copy is not published as a second canonical Python object, so
acquiring an export does not split Python identity or require immediate incoming
reference repair. This works for nursery and ordinary old originals alike.
Only immutable contents may be duplicated this way; copying an object's mutable
attributes or metadata does not make those copies authoritative.

A data-only shadow is not automatically a valid full object destination. Only
a compatible full allocation with current object metadata could become the
canonical destination at a later collection that can repair all aliases. That
transition is optional and must preserve any previously observed identity and
the existing exported address. A minor collection alone cannot repair arbitrary
incoming references to an old original.
Export lookup must remain valid when the original moves; its metadata must not
retain dead originals forever. Exports retain the stable shadow for their
promised lifetime. The lookup and reclamation mechanisms remain open.

Strings and bytes illustrate immutable byte storage. Tuple-like inline storage
contains managed references: any live shadow slots must participate in tracing,
reference repair, and remembered-set policy even though the sequence itself is
immutable. This is an internal storage distinction, not a claim that tuples
implement the Python buffer protocol.

For mutable out-of-line storage, keep one authoritative backing allocation.
At a safepoint, copy/move it into stable storage and update the owner's backing
reference and every other accounted-for alias before resuming access. This path
is valid only when all such aliases are known and can be repaired; outstanding
raw pointers cannot silently be invalidated. It preserves the managed owner's
identity and needs no tracing collection merely to perform that transfer.
Exports prevent subsequent resizing/replacement for their lifetime. Exact alias
accounting and synchronization remain storage-specific design work.

Neither preferred acquisition path inherently forces a minor or whole-heap
collection. Allocation may still trigger collection under normal policy, and
mutable transfer still pays safepoint coordination and copying costs. The
shadow/lookup, ownership, and storage contracts need concrete design before
implementation.

### CloverVM C API String Access Options

The CloverVM C API may offer two alternatives to exporting a stable pointer for
string access. These options do not require promotion of the string or its
backing store merely to access its contents.

**Scoped callback access.** The API roots the string and invokes a native
callback with a read-only data pointer and byte length inside a
relocation-exclusion scope. The pointer is valid only during that callback and
must not escape it. The callback must not re-enter the VM or allocate through
the VM, including indirectly through other API calls or Python callbacks.
Native work that cannot re-enter the VM is allowed. The C surface uses a callback
and caller context; a C++ convenience wrapper may accept a lambda.

Any encoding conversion or other preparation that needs VM allocation finishes
before entering the scope. The API derives the pointer only after entering the
scope and re-reading the rooted reference. The allocation remains alive and
cannot move until the callback returns. Exclusion must prevent relocation by
other threads as well as collection initiated by the current thread. The runtime
must enforce the no-re-entry/no-VM-allocation restriction rather than permit
allocation and silently defer collection. How exclusion and enforcement are
implemented remains open.

**Query size and copy.** The caller queries the required byte count for a chosen
representation, allocates its own buffer, and asks the API to copy into that
buffer with an explicit capacity. No pointer into managed storage escapes the
operation. The caller owns the copied bytes and may retain them across VM calls
and collections. The copy operation must check capacity before writing; the
size query and copy must agree on encoding and whether any terminator is included
in the required capacity. Embedded NUL bytes must be handled through explicit
lengths. Exact API signatures and insufficient-capacity reporting remain to be
specified.

The string handle remains subject to the ordinary active-native-call lifetime
rules across the size query and copy. Each operation roots any managed state it
needs across safepoints. An internal copy loop must likewise protect its source
address from relocation while reading it.

### Whole-Object Stable Relocation

The preferred buffer acquisition paths are the stable-shadow and backing-store
paths above. Whole-object relocation is a separate option for APIs that truly
require the original allocation to become stable; it is not required merely to
expose compatible buffer data. If used, its acquisition path is:

1. Publish updateable strong roots for the allocation to be exposed and any
   managed values needed across the safepoint.
2. If that allocation is in the nursery, reach a safepoint and run a full minor
   collection. Establish its stable destination before root scanning so any
   first encounter selects that destination. Promote it directly into the stable
   heap, mark the destination NonMoving, and enqueue it in the unified copying
   work queue; other nursery survivors
   follow the ordinary promotion policy and enter the same queue. Drain it to
   closure, repairing outgoing references. The nursery is fully
   evacuated and can be reused wholesale only after that work completes.
3. If the allocation is already in the ordinary old generation, relocate it into
   the stable heap at a safepoint before exposure. This path must repair all
   references to it, including incoming heap references, and initialize the
   stable destination as NonMoving; a minor collection's
   roots and remembered sets alone do not suffice for an old target. The exact
   mechanism for this old-to-stable relocation remains open.
4. Complete reference repair before mutators resume. Re-read the updated rooted
   reference and only then derive and return the raw address. Never retain an
   address derived before relocation across the safepoint.

Allocations already in permanently non-moving storage need no relocation.
CPython extension-owned bodies are allocated in stable storage from the start,
as specified in the separate wrapper design. Frequently exported managed storage
may also be allocated directly in the stable heap.

## Write Barrier Implementation Lessons

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

## Open Questions

- How is Python object identity represented so `id(obj)` remains unique among
  live objects and stable across every relocation?
- Should NaNs use relocation-stable per-object identity hashes or bit-pattern
  hashes, and should float identity semantics change independently? Account for
  same-payload NaN collisions without implicitly changing `is` or `id()`.
- How are export shadows indexed, rooted, repaired after original movement, and
  reclaimed, and can their storage/lookup be shared with `id()` shadows?
- What alias and synchronization contracts permit mutable backing-store
  transfer at a safepoint without a collection?
- If whole-object nursery-to-stable relocation is needed, can it avoid the full
  minor collection required by the baseline relocation path?
- How is ordinary old-to-stable relocation implemented, including discovery and
  repair of all incoming references?
- Which storage classes support raw exports, and when should they allocate
  directly in stable storage rather than promote on first exposure?
- What major-collection policy applies to the ordinary old generation, including
  whether it moves?
- What non-moving reclamation policy applies to stable participants?
