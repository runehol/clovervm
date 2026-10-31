# CloverVM Copying Adapter Readiness

| Field | Value |
|---|---|
| Document type | Investigation |
| Status | Proposed |
| Implementation | Partial; isolated conditional groups implemented; production copying remains unimplemented |
| Scope | Production adapter prerequisites and bounded preparatory slices, not permission to move runtime objects |
| Owning layers | Memory owns header and allocation; layouts own extents and copy admission; runtime owns roots and lifetime mode; `src/gc/` owns traversal |
| Validated against | Conditional-group working tree based on `977b2581` (2026-10-31); runtime findings remain source inspection |
| Supersedes | N/A; proposals below require their own implementation reviews |

The [main GC design](generational-copying-gc.md),
[implementation plan](generational-copying-gc-implementation-plan.md), and
[layout contract](native-layout-descriptors.md) remain authoritative. This
investigation does not change generation policy, lazy initialization, stable
placement, RC lifetime authority, or the paused frame-state work.

## Bottom Line

The generic collector is ahead of the runtime adapter. Strong/weak slot repair
is implemented. Real source classification, forwarding, admitted copying, and
collector-owned destination allocation are not. Adding those methods to
`CloverHeapAdapter` now would hide missing invariants behind an apparently
complete interface.

Several useful prerequisites can still be implemented while RC remains fully
operational. Conditional group formation is now implemented in the test
adapter. The next runtime-layout work is the unified live-layout query, starting
with co-locating existing descriptor data and then adding initialized extents.
Retired backing contents are not a prerequisite: after the tracing cutover
they are unreachable garbage, not objects the copying traversal must inspect.
These preparatory changes do not require a nursery or physical relocation.

## What Exists, and What It Does Not Establish

| Current code | Established | Still missing for copying |
|---|---|---|
| [HeapObject](../src/object_model/heap_object.h) | Eight-byte RC/lifecycle/native-ID/aux header | Generation, remembered membership, forwarding |
| [Layout descriptors](../src/memory/native_layout_descriptor.h) | Object-size query, initialized strong spans, custom strong and weak hooks | Implemented unified allocated/initialized extent query and explicit copy admission |
| [Instance](../src/object_model/instance.cpp) | Shape-derived capacity, aux-counted lazy initialized prefix | Forwarding-safe metadata contract during real copying |
| [Array backings](../src/object_model/vm_array_backing.h) | Live reference backings have local capacity counts and initialized cells; old-count zeroing is RC bookkeeping | Raw live-backing initialized frontier; no retired-backing copy requirement |
| [SlabAllocator](../src/memory/slab_allocator.h) | Stable mappings, bump reservation, valid-object bitmap | Collector-only ownership/publication protocol and group placement |
| [ThreadLocalHeap](../src/memory/thread_local_heap.cpp) | Mutator allocation and RC epoch bookkeeping | Destination allocation independent of RC reclamation scheduling |
| [ReclamationRootSet](../src/memory/heap_reclamation.h) | Conservative object identities used to protect RC reclamation | Complete precise mutable root locations |
| [Reclamation](../src/memory/heap_reclamation.cpp) | Reference release, native destruction, ZCT and bitmap retirement | Tracing lifetime authority and safe source abandonment |

The refcounted/interned address tags are representation classes, not generation
or movability. Stable refcounted objects must retain the refcounted address
encoding. Stable is not synonymous with immortal or interned.

## Proposed Transitional Header

Keep the current eight-byte prefix intact. A deliberately unpacked reference
version adds generation and a nullable forwarding pointer:

```cpp
// Proposed fields, not implemented code.
int32_t refcount;
HeapLifecycleState lifecycle_state;
NativeLayoutId native_layout_id_;
uint16_t native_layout_aux_count;

HeapGenerationState generation_state_; // Young / Old / OldRemembered
HeapObject *forwarding_target_;         // nullptr means not forwarded
```

On the current 64-bit ABI this version would have an expected 24-byte header,
including alignment padding. Verify that explicitly in the header slice. Do
not claim its cost is just sixteen extra bytes: the allocator rounds record
reservations to 32-byte strides, so some records cross an allocation boundary.
Measure allocation throughput and representative record/slab occupancy before
and after. Header growth also changes Object field offsets used by the JIT;
audit offset-derived code and structural tests, not only `sizeof` assertions.

All existing allocations should initially be Old and not forwarded. There is
no nursery today. Generation classification tests can exercise synthetic Young
objects without enabling copying or changing RC behavior. Marking can continue
using the existing temporary side set; this proposal does not add copied-object
tri-colour fields or settle the future stable-heap mark representation.

The forwarded source keeps its generation, native layout ID, aux count, shape
pointer, and ordinary payload. Only its dedicated forwarding field changes.
The destination gets its own normal GC state and Old generation. Source storage
is retained through strong and weak repair. Do not overlay forwarding on the
RC word, native metadata, or Object's shape field in this first version.

A sixteen-byte total header is also a concrete candidate: retain the RC/native
prefix and use a second `uintptr_t` GC word. The three generation states fit
in two low bits; all current managed pointer encodings leave those bits clear.
The remaining bits can hold a nullable forwarding target. Generation remains
available after forwarding, and extracting a target masks out the generation
bits without stripping the refcounted/interned address tag. This uses explicit
generation metadata, not address-derived generation. Alignment/encoding
assertions and all state/target combinations need tests before choosing it.

The proposed invariant is separate GC storage, not an insistence on 24 bytes.
Compare these two representations in the header readiness review; neither is
approved by this investigation. The algorithm should depend on named generation
and forwarding accessors, not either word layout. No movement-pin field is
proposed: the current main design requires stable placement rather than pinning
inside moving space. Slab pins are reclamation blockers, not object pins.

### Why This Helps Shape-Derived Queries

`Instance::object_size_in_bytes()` reads `get_shape()->get_inline_slot_capacity()`.
With a dedicated forwarding field, the old Shape's capacity remains readable
even when the Shape has a forwarding target. No recursive tracing or repaired
class pointer is needed to read that scalar. Initially Shape is stable anyway,
because its custom destruction excludes it from ordinary moving storage.

Later source-header compaction must preserve this guarantee or introduce an
explicit forwarding-aware metadata query. That cannot be an incidental change
to how a forwarding pointer is stored.

## Extents: Do Not Copy the Allocation Blindly

Implement the existing `NativeLayoutInfo` design in the layout subsystem, not
a second GC-specific descriptor registry. It reports allocated bytes,
initialized bytes, and the ordinary strong span; custom scanners remain
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
   capacity, but RawArray growth constructs only the used prefix and does not
   clear the rest. There is no backing-local initialized-byte frontier. Keep
   this layout outside the initial moving admission set; later choose explicit
   initialized-frontier metadata or a separately approved clearing policy.

Zero-length Tuple and OverflowSlots allocations reserve at least one element,
but constructors initialize zero trailing elements when their count/capacity is
zero. Their initialized extent must stop before that placeholder. A query
cannot simply return allocated size as initialized size for all ordinary
layouts. Use the existing custom-query escape hatch where a zero-length clamp
does not fit the common formulas; do not silently extend the formula language.

The new initialized extent is a byte-copy bound, not permission to copy-construct
ownership wrappers. `Member`/`MemberHeapPtr` copy constructors retain references.
No generic C++ copy constructor may be used as a relocation primitive. The
layout admission slice must settle typed object lifetime/relocation mechanics;
scan completeness and a trivial destructor alone do not establish copy safety.

## Allocation Groups Are Conditional

Exclusive overflow ownership does not imply that owner and overflow are always
evacuated together. [Object::ensure_overflow_slot](../src/object_model/object.cpp)
can allocate a fresh overflow for an existing owner. Under generational
allocation, that owner can already be old/stable.

Accepted conditional group rules, implemented in the test adapter:

"Collected" in this table means selected for evacuation in the current
collection's moving source set, not simply young. A young-only collection and
a full collection can choose different source sets. Stable objects remain
outside copying even when a full collection marks and sweeps them.

| Encounter | Group |
|---|---|
| Collected owner, collected unforwarded overflow | Owner and overflow |
| Collected owner, overflow outside collected space | Owner only |
| Collected owner, overflow already forwarded | Owner only; scanning repairs its field |
| Selected overflow reached without copying its owner | Overflow only |

The former unconditional `exclusive_owner == nullptr` assertion is removed.
Selected companions can be copied independently. This repairs a test-model
limitation, not a running-VM regression: no production moving collector invokes
it. The generic traversal is unchanged; group formation belongs to the adapter.

The conditional-group slice adds test-only Young/Old generation labels
and an explicit YoungOnly/Full collection selection. Full selects both young
and old movable source records; neither mode selects fresh destination records
or stable storage. That exercises the group contract without choosing whether
production full GC moves or sweeps its ordinary old generation. No generation
checks or collection-mode branches belong in the generic group/traversal API.
The test source-disposal helper takes the same explicit selection, preserving
uncollected old and stable records rather than clearing its entire source pool.

Adjacency is an optimization conditional on both members being copied now.
Do not recopy an old/already-forwarded companion just to obtain adjacency.
Do not add a production owner backlink solely to discover standalone overflow;
its native layout already tells the adapter it is a standalone member.

Real placement also needs padding. Refcounted records start at addresses with
the refcounted tag bits and subsequent records are 32-byte stride aligned.
For a same-storage-class pair, place the companion after the primary's
rounded reservation, not at `primary + initialized_size` or necessarily
`primary + allocated_size`. Verify alignment, overflow arithmetic, and storage
tags for each member. Reserve the whole group before publishing forwarding;
padding and unused tails are not copied or scanned.

## Collector-Only Destination Allocation

Reuse slab reservation mechanics, not the mutator construction path.
`construct_object()` invokes a C++ constructor and immediately marks a valid
object. `ThreadLocalHeap::allocate_slow()` records RC epoch slabs and can request
reclamation. Neither is the destination-copy protocol as written.

A dedicated collection-local destination allocator should:

- grow stable-addressed chunks on demand, without a bitmap walk or Cheney scan;
- reserve each group in one chunk, with checked size/tag/alignment accounting;
- distinguish unpublished reservation from a fully initialized record;
- initialize/copy every member before exposing its forwarding association;
- record ordinary and stable destination ownership for later reclamation;
- never enter a safepoint, recursively collect, or run mutator callbacks;
- treat allocation failure as fatal, including native bookkeeping failure.

This does not require removing existing bitmaps. Keep RC/stable-heap bitmap
bookkeeping where the current reclamation owner requires it; review any removal
separately. The new nursery/copy worklist must not depend on those bitmaps.
Do not disguise global mutator allocation as collector allocation merely
because both eventually call a bump allocator.

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
| Exact builtin Tuple | Dynamic ordinary values; needs exact zero-length initialized extent and relocation contract; subclasses need separate admission |
| OverflowSlots | Capacity/aux metadata are local; needs standalone/group cases and exact zero-capacity extent |
| Instance | Shape-derived capacity plus lazy prefix; defer until extent and forwarding-safe metadata tests exist |
| ValueArrayBacking / HeapPtrArrayBacking | Query live capacity/initialized cells from existing counts; no retired-backing work; copying still needs layout admission |
| RawArrayBacking | Defer moving admission until its live initialized frontier is settled |
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
"not copied" from "not collected". Conditional group tests cover full moving
source selection, not complete full-heap tracing or reclamation.

## Follow-Up Slices Proposed for Review

1. **Conditional allocation groups in the isolated heap (implemented).** Exercise old owner
   with young overflow, young owner with old overflow, and an already-forwarded
   companion. Preserve one copy/scan per source and do not require adjacency
   when the companion is not copied with its owner. Exercise YoungOnly and Full
   moving-source selections, both root orders, and destination exclusion in both
   modes. An unreachable old movable weak target survives YoungOnly selection
   but is cleared under Full selection. No runtime header changes or stable-heap
   mark/sweep implementation.
2. **Unified descriptor lookup (next proposed implementation slice).** Replace
   the separate release/object-size LUTs with one NativeLayoutId-indexed table
   whose entries contain the existing ReleaseDescriptor and ObjectSizeDescriptor.
   Expose `native_layout_descriptor_for(id)` and migrate all in-repo users; do
   not retain duplicate compatibility lookup APIs. Preserve the current formulas,
   callbacks, validation, ownership and object layouts. This is the first bounded
   step toward the existing unified-query design, not its final formula encoding.
   It deliberately does not add an initialized-size field or fabricate a default.
3. **Live initialized-extent query.** Add the existing NativeLayoutInfo design
   with allocated bytes, initialized bytes and the ordinary strong span, using
   the unified lookup. Cover lazy Instance, live reference backings, and empty
   trailing allocations. Keep custom scanning/release distinct from an ordinary
   empty span. Settle raw live-backing initialization separately; no blind
   allocated-equals-initialized fallback. No retired-backing audit or new
   historical-reservation field is part of this work.
4. **Transitional header.** Review the sixteen-/twenty-four-byte candidates, preserve RC,
   default current allocations Old, test independent GC state, and measure
   header/stride/construction costs. Do not publish real forwarding in RC mode.
5. **Collector-only reservation and admission tests.** Use dedicated storage
   with no mutator reentry. Settle C++ relocation lifetime, per-layout admission,
   tag-compatible group placement, publication, and slab ownership before real
   adapter methods are exposed. Isolated byte-reservation tests do not enable
   VM relocation or transfer ownership of real RC objects.
6. **Activation readiness.** Recheck root completeness, barriers, stable tracing
   lifetime authority, hash/identity, and source retirement. Only then implement
   real adapter copying and a restricted nursery under a separate approval.

Slices 1--4 are useful independently and do not authorize slices 5--6. Root
publication, write barriers, non-moving tracing reclamation, and any Lean-model
work retain their existing stage reviews. Nothing here adds stub copying
methods to make `Evacuation<CloverHeapAdapter>` compile.

## Verification of This Investigation

The original investigation was source/design inspection at the commit in the
metadata table. The subsequent conditional-group slice changes only the test
heap and its tests, not the runtime header, allocator, or generic traversal.
Its checks validate selected-source group formation, not production copying
readiness. Header-size alternatives and runtime proposals have not been
implemented or benchmarked. Relative links and diff formatting are checked
separately.

Conditional-group verification: 39 focused GC tests passed; full Debug
`all check` passed 2,297 tests and full Release passed 2,287, each with three
existing JIT-disabled skips and one disabled test. The
[adapter verification record](gc-collector-heap-adapter.md#conditional-groups-and-collection-selection)
states the cases and the full-collector coverage boundary.
