# CloverVM Copying Adapter Readiness

| Field | Value |
|---|---|
| Document type | Investigation |
| Status | Proposed |
| Implementation | Partial; isolated copying and live layout queries implemented; production copying remains unimplemented |
| Scope | Production adapter prerequisites and bounded preparatory slices, not permission to move runtime objects |
| Owning layers | Memory owns header and allocation; layouts own extents and copy admission; runtime owns roots and lifetime mode; `src/gc/` owns traversal |
| Validated against | Single-object simplification based on `a7c007bf` (2026-11-01); runtime findings remain source inspection |
| Supersedes | N/A; proposals below require their own implementation reviews |

The [main GC design](generational-copying-gc.md),
[implementation plan](generational-copying-gc-implementation-plan.md), and
[layout contract](native-layout-descriptors.md) remain authoritative. This
investigation does not change generation policy, lazy initialization, stable
placement, RC lifetime authority, or the paused frame-state work.

## Bottom Line

The generic collector is ahead of the runtime adapter. Strong/weak slot repair
is implemented. Real source classification, forwarding, admitted copying, and
integrated destination ownership are not. Adding those methods to
`CloverHeapAdapter` now would hide missing invariants behind an apparently
complete interface.

Several useful prerequisites can still be implemented while RC remains fully
operational. Source selection is exercised in the isolated test heap;
production copying is not enabled.
Retired backing contents are not a prerequisite: after the tracing cutover
they are unreachable garbage, not objects the copying traversal must inspect.
These preparatory changes do not require a nursery or physical relocation.

## What Exists, and What It Does Not Establish

| Current code | Established | Still missing for copying |
|---|---|---|
| [HeapObject](../src/object_model/heap_object.h) | Contains standalone eight-byte HeapHeader with generation/lifecycle/native-ID/aux/RC and tested forwarding encoding | Real source classification, remembered coverage, and admitted production forwarding |
| [Layout descriptors](../src/memory/native_layout_descriptor.h) | Unified allocation/copy query, ordinary strong spans, custom strong and weak hooks | Explicit relocation admission and forwarding-safe metadata contract |
| [Instance](../src/object_model/instance.cpp) | Shape-derived capacity, aux-counted lazy initialized prefix | Forwarding-safe metadata contract during real copying |
| [Array backings](../src/object_model/vm_array_backing.h) | Live reference backings have local capacity counts; raw backings copy full byte storage; old-count zeroing is RC bookkeeping | Relocation admission; no retired-backing copy requirement |
| [SlabAllocator](../src/memory/slab_allocator.h) | Stable mappings, bump reservation, valid-object bitmap | Collector-only ownership/publication protocol |
| [HeapSpace](../src/memory/heap_space.h) | Owns a singly linked slab list and supplies checked single-object raw allocation | Runtime ownership transfer, typed relocation, and publication |
| [ThreadLocalHeap](../src/memory/thread_local_heap.cpp) | Mutator allocation and RC epoch bookkeeping | Destination allocation independent of RC reclamation scheduling |
| [ReclamationRootSet](../src/memory/heap_reclamation.h) | Conservative object identities used to protect RC reclamation | Complete precise mutable root locations |
| [Reclamation](../src/memory/heap_reclamation.cpp) | Reference release, native destruction, ZCT and bitmap retirement | Tracing lifetime authority and safe source abandonment |

The refcounted/interned address tags are representation classes, not generation
or movability. Stable refcounted objects must retain the refcounted address
encoding. Stable is not synonymous with immortal or interned.

## Proposed Transitional Header

The accepted representation is now specified in
[GC Object Header](gc-object-header.md). The RC transition stays eight bytes:
one byte of GC state, one byte of native-layout ID, two bytes of aux count, and
four bytes of RC. Generation occupies bits 0--1, bit 2 marks forwarding,
bits 3--5 are spare in normal headers, and bits 6--7 hold normal RC lifecycle.
All current allocations initialize as Old/Normal; nursery/copying remain disabled.

A forwarding store replaces the header with the encoded target address plus
the source generation and forwarded flag. Target alignment leaves bits 0--2
available. Reading the target masks only those bits; generation remains a byte
load and low-two-bit mask in both forms. The source may be Young while its
destination is Old; callers must not substitute the destination generation.

RC, lifecycle, native ID, aux count, and normal spare state are still overwritten.
Reclamation and forwarding cannot operate on the same RC-managed source without
a separately approved transition protocol. The accepted tracing-cutover staging
is unchanged.

After RC retirement, the final eight-byte normal header has a one-byte GC state,
one reserved byte, a 16-bit native-layout ID, and a 32-bit aux count. Forwarding
overwrites that entire header. The final wider fields require an explicit later
migration; the first RC transition preserves the native-ID/aux widths but moves
their offsets and RC's offset to fit the new state byte.
Source selection, layout inspection, and weak resolution must be compatible
with loss of the normal source metadata after forwarding. This does not approve
production forwarding or settle the remaining relocation/lifetime gates.

Verify the eight-byte size and alignment, RC offset four, and descriptor/JIT
metadata offsets. Float remains 32 bytes with a 32-byte stride. Measure
construction and representative record/slab occupancy before and after rather
than assuming all derived layouts are unaffected by the alignment change.
Marking can continue using its existing temporary side set.
No movement-pin or lock protocol is introduced; stable placement and slab
reclamation pins retain their separate contracts.

### Why This Helps Shape-Derived Queries

`Instance::object_size_in_bytes()` reads `get_shape()->get_inline_slot_capacity()`.
The transitional and final forwarding representations overwrite normal header metadata. Layout
queries must resolve forwarded shapes before requiring their normal header;
they must not depend on native-ID/aux fields remaining available in the source.
Source generation is the explicit exception and remains readable. No recursive tracing of the shape's outgoing references is needed
just to obtain scalar capacity. Initially Shape is stable anyway, because its
custom destruction excludes it from ordinary moving storage. The final
forwarding-aware query contract must be established before that changes.

## Extents: Do Not Copy the Allocation Blindly

Implement the existing `NativeLayoutInfo` design in the layout subsystem, not
a second GC-specific descriptor registry. It reports allocated bytes,
copy bytes, and the optional ordinary strong span; custom scanners remain
independent. Keep allocator-rounded reservation bytes distinct from logical
layout extents. Future shrinking may choose a smaller destination reservation
without claiming the source reservation was already smaller.

The live-data cases are:

1. **Lazy Instance slots.** Keep shape capacity as allocated extent and aux
   count as the initialized prefix. Do not eagerly clear all reserved slots.
2. **Live reference backings.** ValueArray and HeapPtrArray exclusively own
   their backings. A live backing's count describes its allocated cell capacity,
   and growth initializes that capacity. In the tracing model, replacing the
   backing reference leaves the old backing unreachable: do not query, copy,
   normalize, or preserve its old cells. There is no retired-backing shrink
   policy to prove and no new immutable-capacity field required for that purpose.
   The current `set_value_cell_count(0)` writes prevent duplicate RC release and
   remain while RC is active. Remove them after the cutover, as recorded in
   [stage 10](generational-copying-gc-implementation-plan.md#10-expand-coverage-and-retire-transitional-machinery).
3. **Raw backing spare capacity.** `RawArrayBacking::storage_bytes` records
   its complete extent. Copy the backing's full extent opaquely, including spare
   capacity. It has no ordinary strong cells: copying raw bytes does not
   interpret elements or scan them as pointers. No initialized frontier or
   clearing policy is needed for this query. Relocation admission remains a
   separate review, as for every other layout.

Zero-length Tuple and OverflowSlots allocations reserve at least one element,
but constructors initialize zero trailing elements when their count/capacity is
zero. Their copy extent stops before that placeholder. A query
cannot simply return allocated size as copy size for all ordinary
layouts. Use the existing custom-query escape hatch where a zero-length clamp
does not fit the common formulas; do not silently extend the formula language.

The new copy extent is a byte-copy bound, not permission to copy-construct
ownership wrappers. `Member`/`MemberHeapPtr` copy constructors retain references.
No generic C++ copy constructor may be used as a relocation primitive. The
layout admission slice must settle typed object lifetime/relocation mechanics;
scan completeness and a trivial destructor alone do not establish copy safety.

## Single-Object Evacuation and Collection Selection

Overflow storage is reached through an ordinary strong slot. Source selection,
copying, forwarding, and queueing apply independently to the owner and backing,
regardless of root order. An old/stable owner can refer to young overflow; its
actual remembered slot must be supplied to repair that reference. An already
forwarded or unselected backing is not recopied.

The test adapter supports YoungOnly and Full moving-source selection. Full
selects young and old movable source records; neither mode selects fresh
destinations or stable storage. Source membership remains true after forwarding.
The source-disposal helper uses the same selection and preserves excluded old
and stable records. Generation and collection-mode checks remain adapter-owned.

Owner/backing adjacency is deferred. It is not represented by a group type,
special ownership backlink, or generic multi-object copying operation.

## Collector-Only Destination Allocation

Reuse slab reservation mechanics, not the mutator construction path.
`construct_object()` invokes a C++ constructor and immediately marks a valid
object. `ThreadLocalHeap::allocate_slow()` records RC epoch slabs and can request
reclamation. Neither is the destination-copy protocol as written.

A dedicated collection-local destination allocator should:

- grow stable-addressed chunks on demand, without a bitmap walk or Cheney scan;
- allocate one object at a time, with checked size/tag/alignment accounting;
- distinguish unpublished reservation from a fully initialized record;
- initialize/copy each object before exposing its forwarding association;
- record ordinary and stable destination ownership for later reclamation;
- never enter a safepoint, recursively collect, or run mutator callbacks;
- treat allocation failure as fatal, including native bookkeeping failure.

This does not require removing existing bitmaps. Keep RC/stable-heap bitmap
bookkeeping where the current reclamation owner requires it; review any removal
separately. The new nursery/copy worklist must not depend on those bitmaps.
Do not disguise global mutator allocation as collector allocation merely
because both eventually call a bump allocator.

### Raw HeapSpace Allocation

`HeapSpace` in `src/memory/heap_space.h` reuses SlabAllocator mappings and bump
allocation, not ThreadLocalHeap. `allocate(size_t allocated_bytes)` accepts a
positive allocated extent and returns one byte address. The extent is rounded
to the current 32-byte stride. The address retains the refcounted encoding,
including for any future stable destination. This does not derive generation
from addresses.

The space owns its slabs until destruction through a singly linked list of
`SlabListNode` entries. The head is the current allocation slab; there is no
duplicate current-slab pointer. A reservation that does not fit causes a new
slab; an oversized object receives a sufficiently large mapping. Earlier
mappings and returned addresses never move. Rounding, mapping overhead, and
pointer-difference bounds are checked before allocation. Sizing/mapping/native
bookkeeping failure is fatal. Nodes use nothrow allocation because the runtime
disables C++ exceptions, and list destruction is iterative.

These are raw, unpublished bytes, not HeapObjects. There are no managed
constructors, bitmap marks/walks, RC epoch registrations, source writes,
forwarding, or GlobalHeap adoption. Oversized byte reservations do not grant
large-object movement admission. Publication, ownership transfer into a runtime
heap, typed relocation, and RC/source retirement remain separate reviews.

The intended runtime owner is the memory manager, with nursery, old, and stable
spaces surviving individual collections. Thread-local allocation cursors will
borrow nursery slabs; a minor collection covers the whole VM nursery, not just
one thread's slabs. That ownership and cursor integration is future work: the
current space is an unsynchronized raw allocator, used exclusively by its caller.
Tracing, forwarding, construction, and safepoint policy remain outside this class.

The single-object simplification passed 50 focused GC/storage tests and full
Debug/Release checks. These checks do not enable production evacuation.

## Source Classification and Initial Admission

`is_collected_source()` must identify the current collection's moving source
space and
remain true after forwarding. Use allocator-owned source membership together
with object generation; generation is not inferred solely from address tags.
Validate that these facts agree. Interned, stable, large, and uncollected old
objects are not nursery sources. A full collection can additionally select
movable old source spaces; it still does not copy stable objects. Space
classification is an allocator concern; the algorithm does not enumerate
arbitrary heaps to classify a pointer.
This is a classification contract, not a requirement for a page-map lookup on
every edge. If allocation guarantees that every Young object belongs to the
active collection's nursery, a header generation test can be the fast path and
source-range checks can validate that invariant. Selecting only some nurseries
would require distinguishing their membership explicitly.

Initial admission should be an explicit conservative routing policy using the
native layout descriptors/audit, not a new registry that equates "scanable"
with "movable". Unknown/unapproved layouts bypass the nursery.

| Candidate | Evidence and remaining gate |
|---|---|
| Exact builtin Float, excluding NaN | Fixed native payload and ordinary strong fields; still needs relocation/root/admission tests; subclasses are not implicitly admitted |
| Float NaN | Address-derived hash is a blocker; choose stable placement from construction or a stable hash policy before admission |
| Exact builtin Tuple | Dynamic ordinary values; query excludes unused zero-length placeholder; needs relocation contract; subclasses need separate admission |
| OverflowSlots | Capacity/aux metadata are local; needs ordinary strong-edge coverage and exact zero-capacity extent |
| Instance | Shape-derived capacity plus lazy prefix; defer until extent and forwarding-safe metadata tests exist |
| ValueArrayBacking / HeapPtrArrayBacking | Query live capacity/initialized cells from existing counts; no retired-backing work; copying still needs layout admission |
| RawArrayBacking | Query copies its full byte extent with no ordinary pointers; still needs relocation admission |
| CodeObject, Shape, JitCodeObject | Custom destruction: stable allocation, never ordinary source copying |
| Remaining layouts, resource owners, exports, large objects | No implicit admission; retain their reviewed stable/bypass policy |

The NaN issue is concrete: [float hashing](../src/builtin_types/float.cpp)
uses the Float address for NaNs, while dicts store hashes with their entries.
Rewriting a dict key pointer would not repair a changed hash. Keeping all Float
objects stable until that policy is settled is also a valid smaller first step;
this investigation does not select new Python-visible hash/identity semantics.
The exclusions must be enforced before allocating into the nursery, not found
only after an ineligible object needs evacuation.

## Lifetime and Roots Still Block Production Activation

The current collector reclaims through RC: it decrements children, enqueues ZCT
entries, marks lifecycle state Dead, clears valid bits, and releases slabs.
Copying is not a second ordinary owner. Running release/destruction on a copied
source would release references that the destination still owns; bulk-discarding
unreachable sources under live RC would skip releases of their old children.
Raw ZCT entries would also retain stale source addresses.

Follow the accepted staging: establish tracing lifetime authority with stable
addresses first, then add nursery evacuation. Keeping the RC fields while
building prerequisites is not permission to interleave RC reclamation and
source-space discard. The tracing-mode review must define ownership-wrapper
operations, ZCT retirement, native reset/destruction, and non-moving sweep.
Do not retain all positive-refcount objects as fake tracing roots.

`ReclamationRootSet` stores conservative object identities, not mutable root
locations. The current broad frame scan can see metadata/non-Value words and
must not be converted to copying roots just by wrapping every word in
`HeapReferenceSlot`. Published accumulator/native/persistent root APIs expose
real cells, but exact managed-frame state and native-borrow coverage remain
separate gates. Supplying root addresses is necessary, not proof of completeness.
Do not reopen the paused exception/frame-map design implicitly in this slice.

Activation additionally needs strong remembered coverage, exact weak-remembered
units in uncollected owners, mutation/barrier lifetime rules, and the existing
native/JIT no-safepoint contracts. Stable promotion joins the same copying scan
queue as ordinary promotion; it is not a terminal leaf allocation.

Full collections also need liveness and scanning for non-moving participants.
The current isolated evacuation phase does not supply that: strong discovery
skips outside-source objects and its weak resolver treats outside-source targets
as surviving. That is appropriate for uncollected objects in a minor collection,
not for dead stable objects participating in full GC. The full-collector review
must integrate marking/scanning and stable-target weak liveness, distinguishing
"not copied" from "not collected". Isolated tests cover full moving
source selection, not complete full-heap tracing or reclamation.

## Follow-Up Slices Proposed for Review

1. **Collection selection in the isolated heap (implemented).** Exercise old
   owners with young backing, young owners with old backing, repeated references,
   both root orders, and independent destination choices. Preserve one copy/scan
   per selected source in YoungOnly and Full modes; fresh destinations are
   excluded in both. Old weak-only targets survive YoungOnly but are cleared
   under Full selection. No stable-heap marking/sweeping or runtime movement.
2. **Unified descriptor lookup (implemented).** The separate release/object-size
   LUTs are replaced with one NativeLayoutId-indexed table
   whose entries contain the existing ReleaseDescriptor and ObjectSizeDescriptor,
   exposed by `native_layout_descriptor_for(id)`. All in-repo users are migrated;
   there are no duplicate compatibility lookup APIs. The current formulas,
   callbacks, validation, ownership and object layouts are preserved. This is
   the first bounded step toward the unified-query design, not its final formula encoding.
   It deliberately does not add an initialized-size field or fabricate a default.
3. **Live copy-extent query (implemented).** Add the existing NativeLayoutInfo design
   with allocated bytes, copy bytes and the optional ordinary strong span, using
   the unified lookup. Cover lazy Instance, live reference backings, and empty
   trailing allocations. Keep custom scanning/release distinct from an ordinary
   empty span. Raw backings report their full extent for both sizes and no
   ordinary pointers. Neither size is optional. No retired-backing audit or new
   historical-reservation field is part of this work.
4. **Transitional header (implemented).** The accepted eight-byte
   [header encoding](gc-object-header.md) preserves RC and initializes current
   allocations as Old. Independent state tests and before/after benchmarks are
   complete. HeapObject contains HeapHeader, with fully inline accessors and
   memcpy forwarding; production forwarding remains disabled.
5. **Collector-only reservation and admission tests.** The raw-reservation
   sub-slice implements dedicated space ownership and tag-compatible object
   allocation with no mutator reentry. C++ relocation lifetime, per-layout
   admission, publication, and runtime slab ownership transfer remain pending
   before real adapter methods are exposed. Isolated byte-reservation tests do
   not enable VM relocation or transfer ownership of real RC objects.
6. **Activation readiness.** Recheck root completeness, barriers, stable tracing
   lifetime authority, hash/identity, and source retirement. Only then implement
   real adapter copying and a restricted nursery under a separate approval.

Slices 1--4 and raw reservations are useful independently; raw reservations
do not authorize the remaining admission or activation work. Root
publication, write barriers, non-moving tracing reclamation, and any Lean-model
work retain their existing stage reviews. Nothing here adds stub copying
methods to make `Evacuation<CloverHeapAdapter>` compile.

## Verification of This Investigation

The original investigation was source/design inspection. Subsequent isolated
tests exercise moving-source selection, strong and weak repair, and copied
graph preservation; they do not validate production evacuation, full non-moving
collection, or RC/tracing cutover. The standalone header and raw HeapSpace
allocator are implemented groundwork, not movement activation.

The single-object simplification removes grouped placement and its adjacency
tests while retaining root-order, collection-selection, graph-oracle,
weak-repair, and allocation-failure coverage. Working-tree validation on
2026-11-01, based on `a7c007bf`: 50 focused GC/storage tests passed; full Debug
`all check` passed 2,339 tests and full Release passed 2,328 tests. Each full
configuration had three JIT-disabled skips and one disabled test. The focus
thread bypassed the shared build lock as authorized; other jobs and the lock
file were untouched. Formatting, local documentation links, and diff checks
passed. These are isolated-copying and storage tests, not production movement.
