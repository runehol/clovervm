# CloverVM Copying Adapter Readiness

| Field | Value |
|---|---|
| Document type | Investigation |
| Status | Proposed |
| Implementation | Partial; isolated copying, live layout queries, and raw-space source classification implemented; production copying remains unimplemented |
| Scope | Production adapter prerequisites and bounded preparatory slices, not permission to move runtime objects |
| Owning layers | Memory owns header and allocation; layouts own extents and copy admission; runtime owns roots and lifetime mode; `src/gc/` owns traversal |
| Validated against | Classification working tree based on `4467eced` (2026-11-01); earlier groundwork validation recorded below |
| Supersedes | N/A; proposals below require their own implementation reviews |

The [main GC design](generational-copying-gc.md),
[implementation plan](generational-copying-gc-implementation-plan.md), and
[layout contract](native-layout-descriptors.md) remain authoritative. This
investigation does not change generation policy, lazy initialization, stable
placement, RC lifetime authority, or the paused frame-state work.

## Bottom Line

The generic collector is ahead of the runtime adapter. Strong/weak slot repair
is implemented. A classification-only index now works with real headers in raw
HeapSpace storage. Runtime source selection, forwarding, admitted copying, and
integrated destination ownership are not wired into CloverHeapAdapter. Adding those methods to
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
| [HeapObject](../src/object_model/heap_object.h) | Contains standalone eight-byte HeapHeader with generation/lifecycle/native-ID/aux/RC and tested forwarding encoding | Runtime source-space selection, remembered coverage, and admitted production forwarding |
| [EvacuationSpaces](../src/gc/evacuation_spaces.h) | Sorted selected-space mapping index; validates source generation/NonMoving state even after forwarding | Runtime space-role selection and lifetime, layout admission, and adapter integration |
| [Layout descriptors](../src/memory/native_layout_descriptor.h) | Unified allocation/copy query, ordinary strong spans, custom strong and weak hooks | Explicit relocation admission and forwarding-safe metadata contract |
| [Instance](../src/object_model/instance.cpp) | Shape-derived capacity, aux-counted lazy initialized prefix | Forwarding-safe metadata contract during real copying |
| [Array backings](../src/object_model/vm_array_backing.h) | Live reference backings have local capacity counts; raw backings copy full byte storage; old-count zeroing is RC bookkeeping | Relocation admission; no retired-backing copy requirement |
| [SlabAllocator](../src/memory/slab_allocator.h) | Stable mappings, bump reservation, valid-object bitmap | Collector-only ownership/publication protocol |
| [HeapSpace](../src/memory/heap_space.h) | Owns a singly linked slab list, supplies checked raw allocation, and visits mapping ranges | Runtime ownership transfer, source-role lifecycle, and publication |
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
four bytes of RC. The accepted next encoding keeps generation in bits 0--1,
uses bit 2 for per-object NonMoving, reserves normal bits 3--4 as zero so pointer
tags `0x08`/`0x10` identify forwarding, leaves bit 5 spare, and retains normal RC
lifecycle in bits 6--7. The NonMoving/pointer-tag header protocol is implemented;
central initial construction policy is implemented. Callers pass an explicit
`InitialMobilityPolicy` non-type template argument through the heap APIs to
construction, with Debug backing-heap checks and no runtime Release policy
lookup: interned storage and layouts declaring
`native_destroy` are NonMoving; ordinary managed layouts remain movable.
Initialization remains Old/Normal. Nursery/copying remain disabled; the bit
alone does not establish production movement admission.

A forwarding store replaces the header with the encoded target address plus
the source generation, with no explicit forwarded flag. A valid tagged target
has low bits 0--2 clear and exactly one pointer tag in `0x18`. Reading the target
masks only bits 0--1; generation remains a byte
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
NonMoving is a one-way per-allocation placement property, independent of
generation and layout. Initial policy covers stable proxies/extensions,
CodeObjects, Shapes, and existing custom-destruction/immortal categories; a
normally movable string/backing can later be promoted to NonMoving storage.
Mark the stable destination and repair references before exposing its address,
not the nursery source in place. Full pinning bits/counts remain deferred until
reference mapping is in place. No lock protocol is introduced; slab reclamation
pins retain their separate contract.

### Why This Helps Shape-Derived Queries

`Instance::object_size_in_bytes()` reads `get_shape()->get_inline_slot_capacity()`.
The transitional and final forwarding representations overwrite normal header metadata. Layout
queries must not depend on native-ID/aux fields remaining available in a
forwarded source. Source generation and its former movable state remain readable.
Initially Shape is non-moving, so reading scalar capacity does not require
following Shape forwarding or recursively tracing its outgoing references.
Revisit this only if Shape movement is later admitted.

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
No generic C++ copy constructor may be used as a relocation primitive. Use the
generic memcpy protocol below; layout admission and reference repair remain
separate from byte copying.

### Accepted Byte-Copy Protocol

Copying invokes no constructors, assignment operators, or destructors. There
is no native-layout switch selecting relocation constructors and no per-layout
relocation-constructor API. The memory layer obtains `NativeLayoutInfo` before
source metadata is overwritten, reserves `allocated_size` bytes from HeapSpace,
and uses `std::memcpy` to preserve exactly `copy_size` bytes. Allocator rounding
is not the copy bound. Lazy spare slots remain uninitialized; zero-length
trailing placeholders are not read.

The adapter prepares destination generation/header state before publication.
The collector then installs source forwarding, enqueues the destination once,
and repairs references through existing strong-slot and weak-unit interfaces.
Those interfaces and the per-layout checklist also cover self/interior pointers
and other address-dependent state where applicable. Copying is not a second RC
owner: neither copying nor source abandonment retains or releases children.

**Accepted temporary C++ exception:** retaining `Member` and `MemberHeapPtr`
special members make Object/Float and other containing records non-implicit-
lifetime types. Creating their destination records by memcpy into raw storage
temporarily lacks the C++20 lifetime guarantee. This is an explicitly accepted
implementation tradeoff, not something an assertion or sanitizer makes defined.
Do not introduce relocation constructors to work around it. Remove the exception
when RC-backed member representations are replaced, and add compile-time
trivially-copyable checks for admitted movable records at that cutover.
Variable-tail storage retains its separate extent/lifetime audit. The C++ trait
does not replace the reference-repair and address-dependence checklist.

This language-level exception does not authorize forwarding live RC-owned
sources, skipping RC releases, or movement before tracing owns lifetime.
Test-only copies remain outside runtime ownership/publication.

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
heap, byte-copy publication, and RC/source retirement remain separate reviews.

The intended runtime owner is the memory manager, with nursery, old, and stable
spaces surviving individual collections. Thread-local allocation cursors will
borrow nursery slabs; a minor collection covers the whole VM nursery, not just
one thread's slabs. That ownership and cursor integration is future work: the
current space is an unsynchronized raw allocator, used exclusively by its caller.
Tracing, forwarding, construction, and safepoint policy remain outside this class.

The single-object simplification passed 50 focused GC/storage tests and full
Debug/Release checks. These checks do not enable production evacuation.

## Source Classification and Initial Admission

The classification-only slice implements `HeapSpace::visit_memory_ranges()` and
a collection-scoped `gc::EvacuationSpaces` index. Callers supply a span of
`EvacuationSpace` records containing a borrowed space pointer and expected
Young or Old generation (Old also accepts OldRemembered headers). Construction
enumerates the selected spaces' slab mappings once, sorts half-open integer
address ranges, and rejects overlaps. Lookup uses binary search, with no bitmap
walk, per-object registration, or allocation-path change.

An object outside the selected mappings is not a collected moving source. An
object inside must be movable and have the expected generation; contradiction
is fatal, never an excuse to skip a source. Classification reads no native-layout
or aux fields and remains valid after forwarding. Fresh destination spaces are
not selected as sources, even if both source and destination are movable Old
objects. This query assumes valid object pointers; mapping membership does not
prove that an arbitrary address is a constructed object or allocation start.

The index owns only its range metadata. Selected source mappings must outlive
it, and source allocation stops before the snapshot is built. Destination spaces
may continue growing independently. This slice does not wire copying methods
into CloverHeapAdapter, activate evacuation, or settle stable-object liveness.
In particular, returning false does not establish that an outside-source object
is live or that a full collection can skip scanning it.

`EvacuationSpaces::contains()` tests containment in the selected mappings and
remains true after forwarding. Header generation and NonMoving checks validate
the invariants of those spaces; they do not decide whether an object needs
copying. The collector's adapter separately answers membership and movability.
Generation is not inferred solely from addresses, and placement must not be
inferred solely from native layout. Interned, stable, large, and uncollected old
objects are not nursery sources. A full collection can additionally select
movable old source spaces; it still does not copy stable objects. Space
classification is an allocator concern; the algorithm does not enumerate
arbitrary heaps to classify a pointer.
This is a classification contract, not a requirement for a page-map lookup on
every edge. If allocation guarantees that every Young object belongs to the
active collection's nursery, a header generation test can be the fast path and
source-range checks can validate that invariant. Selecting only some nurseries
would require distinguishing their membership explicitly.
NonMoving does not distinguish old sources from fresh movable old destinations;
collection-specific source membership remains necessary for full movement.
Admission is enforced before allocating into a moving source space. Finding an
inadmissible object there is an invariant violation, not permission to skip it
and leave references pointing into storage that will be discarded.

Initial admission should be an explicit conservative routing policy using the
native layout descriptors/audit, not a new registry that equates "scanable"
with "movable". Unknown/unapproved layouts bypass the nursery.

| Candidate | Evidence and remaining gate |
|---|---|
| Exact builtin Float, including NaN | Fixed native payload, ordinary strong fields, and location-independent canonical NaN hash; still needs byte-copy/root/admission tests; subclasses are not implicitly admitted |
| Exact builtin Tuple | Dynamic ordinary values; query excludes unused zero-length placeholder; needs relocation contract; subclasses need separate admission |
| OverflowSlots | Capacity/aux metadata are local; needs ordinary strong-edge coverage and exact zero-capacity extent |
| Instance | Shape-derived capacity plus lazy prefix; defer until extent and forwarding-safe metadata tests exist |
| ValueArrayBacking / HeapPtrArrayBacking | Query live capacity/initialized cells from existing counts; no retired-backing work; copying still needs layout admission |
| RawArrayBacking | Query copies its full byte extent with no ordinary pointers; still needs relocation admission |
| CodeObject, Shape, JitCodeObject | Custom destruction: stable allocation, never ordinary source copying |
| Remaining layouts, resource owners, exports, large objects | No implicit admission; retain their reviewed stable/bypass policy |

Current [float hashing](../src/builtin_types/float.cpp) uses a canonical,
location-independent NaN hash. The earlier address-derived NaN blocker is
resolved; generic identity/`id()` and raw-address exposure still require their
own activation audit.
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

The isolated phase now separates collection membership (`is_collected()`) from
allocation capability (`is_movable()`). Movability does not depend on the
collection mode or whether an object was forwarded: ordinary destinations are
movable but uncollected, stable destinations are non-moving and uncollected.
Full participation includes
existing NonMoving objects; YoungOnly excludes them. Strong discovery marks and
queues canonical copies or existing NonMoving objects once. Moving input
payloads stay unchanged; live NonMoving owners have slots repaired in place.
Weak repair follows moving-source forwarding or NonMoving marks, deferring
unresolved collected targets early and clearing them after strong closure.
Uncollected targets remain alive without being scanned implicitly. This closes
the isolated stable-liveness gap, not runtime integration: VM adapter policy,
immortal-object handling, non-moving sweeping, and lifetime authority remain
separate gates. The allocator's `EvacuationSpaces::contains()` remains a storage
containment helper, not a collector adapter predicate.

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
5. **Byte-copy groundwork, then admission.** Raw HeapSpace reservation is
   implemented. Continue in these bounded slices, without relocation constructors:
   - **5a: Generic byte-copy primitive and exact-Float tests (implemented).**
     `copy_object_bytes(HeapSpace *, const HeapObject *)` lives in
     `src/memory/heap_object_copy.{h,cpp}`; both pointers are required.
     The memory layer
     queries layout, reserves allocated extent, and memcpys the copy extent.
     Exercise an exact Float in test-owned destination storage, preserving
     payload bits (including signed zero and NaNs), shape/overflow references,
     and header bytes. Verify source bytes and child refcounts are unchanged.
     Copies are not registered as second RC owners; discard their raw storage
     without releasing references or running native destruction. No source
     mutation, forwarding, production CloverHeapAdapter copying methods, or
     nursery admission. Document the temporary C++ lifetime exception beside
     the primitive. The byte-copy helper itself does not change generation.
     Validation: 82 focused copy/storage/layout tests passed; full Debug
     `all check` passed 2,345 tests with three JIT-disabled skips and one disabled
     test. Correctness review found no issues; all 78 applicable focused Release
     copy/storage/layout tests also passed. Formatting and diff checks passed.
     No production movement is enabled.
   - **5b: Dynamic extents and copied-reference repair.** Reuse that primitive
     for zero/nonzero Tuple and OverflowSlots, lazy Instance prefixes, and live
     reference/raw backings. Verify allocation versus copy bounds, untouched
     spare storage, and copied strong-slot repair without RC traffic. Cover
     aliases, self-reference, and address-dependent fixups where applicable.
     Sources retain their existing lifetime authority; copies remain test-owned
     and unpublished. Optional capacity shrinking stays deferred.
     **5b.1 extent tests are implemented:** six tests cover real allocations for
     Tuple, OverflowSlots, lazy Instance prefixes, and all three backing layouts.
     They verify copied bytes and full reservation spacing without inspecting
     uncopied tails. Production copying code is unchanged. All 88 focused Debug
     and 84 applicable focused Release tests passed; full Debug `all check`
     passed 2,351 tests with three JIT-disabled skips and one disabled test.
     **5b.2 copied-reference repair is implemented:** four tests use the existing
     strong-slot visitor and slot rewrite operation with test-only
     source/destination pairs. They cover shared tagged/raw references,
     self-reference with a lazy tail, a two-object cycle, and owner/backing links.
     Source bytes and ZCT state remain unchanged; source cycles are broken with
     ordinary stores before RC teardown. No RC source is forwarded and no repair
     API was added. All 95 focused Debug and 91 applicable focused Release tests
     passed; full Debug `all check` passed 2,355 tests with three JIT-disabled
     skips and one disabled test. Production copying remains disabled.
   - **5c: Admission and real-adapter readiness.** Audit slots, extents, fixups,
     identity/hash, exports, and destruction using the existing layout registry.
     Unknown layouts remain non-moving. Establish source classification for
     young/full collections, destination generation/ownership, and forwarding-
     safe metadata access. Plan real adapter wiring without enabling it ahead
     of the activation gates below.
     Start with the accepted
     [NonMoving header migration](gc-object-header.md#code-migration-slices):
     pointer-tag forwarding/one-way accessors first, then centralized initial
     construction policy. Keep reference mapping ahead of full pinning support.
6. **Activation readiness.** Recheck root completeness, barriers, stable tracing
   lifetime authority, hash/identity, and source retirement. Only then implement
   real adapter copying and a restricted nursery under a separate approval.

Slices 1--4 and raw reservations are useful independently; raw reservations
do not authorize the remaining admission or activation work. Root
publication, write barriers, non-moving tracing reclamation, and any Lean-model
work retain their existing stage reviews. Nothing here adds stub copying
methods to make `Evacuation<CloverHeapAdapter>` compile.

## Verification of This Investigation

The classification working tree based on `4467eced` passed 71 focused Debug
GC/storage/copy tests and 70 applicable Release tests. Full Debug `all check`
passed 2,368 tests and full Release passed 2,352, each with three existing
runtime-JIT-disabled skips and one disabled test. Tests cover young/full moving
source selection, empty selections, same-generation destinations, destination
growth, multiple/oversized mappings, and generation-preserving forwarding to
ordinary and NonMoving destinations. Fatal overlap and header/storage mismatch
checks run in both configurations. C++ formatting and diff checks pass.
No production allocation, collection, forwarding, or retirement path changes.

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
