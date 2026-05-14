# Memory Substrate Implementation Plan

This document is the current forward plan for CloverVM's memory substrate. The
historical lifecycle/ZCT/safepoint/slab-blocker baseline is implemented; this
plan now tracks the next design arc:

1. bitmap-based committed-object tracking and young-object discovery;
2. native-layout-id based owned-value scanning and teardown;
3. size-partitioned thread-local heaps.

Background design documents:

- [Refcounting and Safepoints](refcounting-and-safepoints.md)
- [Heap Slab Allocation and Reuse](heap-slab-allocation-and-reuse.md)
- [Committed-Object Bitmap Reclamation](committed-object-bitmap-reclamation.md)
- [Layout-ID-Driven Value Scanning and Deallocation Dispatch](layout-id-driven-scanning.md)

## Current Baseline

The current implementation has a correct single-threaded deferred-refcount
baseline:

- `HeapObject` lifecycle states: `Normal`, `InZct`, `Reclaiming`, `Dead`.
- Per-thread ZCTs with duplicate-entry validation.
- Refcount zero transitions enqueue through helper APIs.
- Fresh zero-refcount reclaimable allocations are still eagerly added to the
  ZCT.
- Conservative stack root collection from published safepoint scan records.
- Safepoints embedded in committed call/return paths, not explicit bytecodes.
- VM-global `run_heap_reclamation()` over registered `ThreadState`s.
- Reclamation clears owned `Value` slots before releasing copied child values.
- Child releases that reach zero append to the ZCT currently being processed.
- Slab lookup by 4 KiB granule.
- Active allocator slabs and allocated objects currently use reclaim blockers.
- Fully unblocked slabs are released/unmapped; ordinary slab pooling is not part
  of the current plan.
- VM bootstrap switches the default thread to fresh slabs after builtin setup.

The next work should remove the two biggest temporary shapes:

- eager allocation-time ZCT enqueue for every young zero-refcount object;
- ad hoc `HeapLayout` value-span decoding inside reclamation.

## Ground Rules

- Stack-scanned values are only a conservative identity filter. Stack root
  collection must not dereference pointer-shaped stack values.
- ZCT processing and bitmap-discovered young objects must feed one reclamation
  mechanism with the same lifecycle transitions and root filtering.
- Object teardown may scan owned values only after the object has been selected
  for reclamation.
- Slab walking must discover committed object headers from slab metadata, not by
  scanning owned values.
- Keep allocation fast paths small. Per-allocation work must be justified by
  removing equal or greater existing work.
- Keep no-GIL atomic refcount/lifecycle state, cycle collection, Python
  finalizers, weakrefs, C-extension `tp_dealloc`, partial-slab hole allocation,
  and native stack scanning out of this milestone.

## Phase 1: Committed-Object Header Bitmap

Replace per-object slab reclaim blockers with a committed-object header bitmap
as described in
[Committed-Object Bitmap Reclamation](committed-object-bitmap-reclamation.md).

1. [ ] Add fixed-size committed-header bitmap metadata to `SlabAllocator`.
   - Derive bitmap word count from `DefaultSlabSize` and the 32-byte heap pointer
     granularity.
   - Keep `first_object_header` in slab metadata.
   - Do not add summary counters or nonempty-word masks until measurements show
     they are needed.
2. [ ] Add helpers for:
   - `mark_committed_object(HeapObject *)`
   - `clear_committed_object(HeapObject *)`
   - `has_committed_objects()`
   - iteration over committed object headers by scanning set bits.
3. [ ] Set the committed-object bit only after successful construction.
4. [ ] Leave failed construction as abandoned bump memory with no committed bit.
5. [ ] Keep dedicated large-object slabs on the same bitmap API. They should only
   set bit 0.
6. [ ] Add debug assertions for header alignment, bit index bounds, duplicate
   bit set, and clearing absent bits.

Validation:

- Tests that successful construction marks exactly one committed header bit.
- Tests that constructor failure does not mark a committed header bit.
- Tests that bitmap iteration returns committed objects and skips abandoned bump
  gaps.
- Tests that dedicated large-object slabs use the same bitmap path and only set
  bit 0.

## Phase 2: Heap-Owned Slab Release

Move slab release authority out of slabs and into `GlobalHeap`.

1. [ ] Split active allocator pins from committed object presence.
   - The bitmap represents committed objects.
   - `active_allocator_pins` represents thread-local heap ownership of active
     allocation slabs.
2. [ ] Replace object reclaim-blocker increments/decrements with bitmap
   mark/clear.
3. [ ] Remove the `GlobalHeap *` back-pointer from `SlabAllocator`.
4. [ ] Add `GlobalHeap::release_slab_if_empty(SlabAllocator *)`.
5. [ ] Make allocator-pin drops heap-mediated and call
   `release_slab_if_empty()`.
6. [ ] During object teardown, clear committed bits and remember touched slabs;
   do not release/unmap slabs immediately from object teardown.
7. [ ] After candidate processing completes, run `release_slab_if_empty()` for
   touched slabs.

Validation:

- Tests that an empty slab with an active allocator pin is not released.
- Tests that dropping the final allocator pin releases an empty slab.
- Tests that reclaiming the final committed object releases the slab only during
  the batched post-reclamation release step.
- Tests that slab lookup no longer finds released ordinary and dedicated slabs.

## Phase 3: Thread-Local Active-Slab Epoch Lists

Track young-object candidate slabs at the allocator-epoch level, not per
allocation.

1. [ ] Add `ThreadLocalHeap::slabs_active_since_reclamation`.
2. [ ] Add `ThreadLocalHeap::ordinary_inactive_slabs_since_reclamation`, seeded
   to zero after each reclamation.
3. [ ] Add `ThreadLocalHeap::dedicated_slabs_since_reclamation` and
   `ThreadLocalHeap::dedicated_large_bytes_since_reclamation` for large-object
   policy and discovery.
4. [ ] Add a helper to remember an active slab once per reclamation epoch.
   A small vector with linear duplicate suppression is enough initially.
5. [ ] Update the list when a thread-local heap installs or switches active
   slabs:
   - construction of `ThreadLocalHeap`;
   - ordinary slab exhaustion;
   - `switch_to_new_slabs()`;
   - future size-class active slab switches.
6. [ ] Increment `ordinary_inactive_slabs_since_reclamation` when an ordinary
   active slab is switched out.
7. [ ] Track dedicated large-object slabs in the dedicated list/counter, not in
   the ordinary inactive-slab counter.
8. [ ] Do not update the ordinary active-slab list on every allocation.
9. [ ] After each reclamation, reset each thread-local heap's ordinary list to
   the slabs currently open for allocation, reset the ordinary inactive counter
   to zero, and clear the dedicated large-object list/counter.
10. [ ] Expose these lists and counters to VM-global reclamation through
    `ThreadState` or `ThreadLocalHeap` accessors.

Validation:

- Tests that slab switches append slabs to the active-since-reclamation list.
- Tests that repeated activation of the same slab in one epoch does not produce
  duplicate scan entries.
- Tests that post-reclamation reset leaves current active slabs on the list.
- Tests that ordinary slab switches increment the inactive ordinary slab counter
  and post-reclamation reset clears it.
- Tests that dedicated large-object slabs update the dedicated list/counter but
  not the ordinary inactive slab counter.
- Tests that ordinary allocation does not mutate the active-slab list.

## Phase 4: Bitmap-Based Young-Object Discovery

Use active-since-reclamation slab bitmaps to discover young zero-refcount
objects that no longer enter the ZCT at allocation time.

1. [ ] Extend `run_heap_reclamation()` to collect roots once, then process both:
   - existing ZCT entries;
   - bitmap-discovered young candidates from each thread-local heap's
     active-slab list;
   - bitmap-discovered young candidates from each thread-local heap's dedicated
     large-object slab list.
2. [ ] For each bitmap-discovered object:
   - ignore it if `refcount > 0`;
   - ignore it if `lifecycle_state != Normal`;
   - if `refcount == 0 && lifecycle_state == Normal && roots.contains(obj)`,
     transition `Normal -> InZct` and append it to a ZCT;
   - otherwise transition `Normal -> Reclaiming` and reclaim through the normal
     teardown path.
3. [ ] Ensure young rooted objects become durable ZCT entries, because no heap
   `DECREF` may rediscover them after the stack root disappears.
4. [ ] Keep child-release cascades appending to the currently processed ZCT.
5. [ ] Remove eager allocation-time ZCT enqueue once bitmap discovery covers
   young zero-refcount objects.
6. [ ] Keep positive-refcount allocation behavior out of the ZCT.

Validation:

- Tests that a stack-rooted young zero-refcount object discovered from a bitmap
  is retained and moved into a ZCT.
- Tests that an unrooted young zero-refcount object discovered from a bitmap is
  reclaimed.
- Tests that positive-refcount bitmap entries are ignored.
- Tests that `InZct`, `Reclaiming`, and `Dead` bitmap entries are not processed
  as young `Normal` candidates.
- Tests that allocation no longer adds every fresh zero-refcount object to the
  ZCT.
- Existing ZCT tests continue to pass for older objects and heap `DECREF`
  transitions.

## Phase 5: Native-ID Owned-Value Scanning

Move reclamation owned-value scanning and teardown behind a native-layout-id
descriptor facade.

1. [ ] Define the descriptor facade around owned-value scanning and teardown.
   Object extent is not required for bitmap slab walks; it can be added later for
   validation, accounting, or allocation policy.
2. [ ] Implement a normalized metadata descriptor path for layouts described by
   scanned `Value` spans.
3. [ ] Bridge current `HeapLayout` decoding behind the descriptor facade only as
   a migration path.
4. [ ] Route reclamation teardown through the descriptor facade, not through
   open-coded `HeapLayout` decoding at the reclamation call site.
5. [ ] Add startup or debug validation that descriptor entries match current C++
   layout facts for migrated types.
6. [ ] Add custom dynamic descriptor handlers only for layouts that cannot be
   expressed by ordinary value-span metadata.
7. [ ] Keep C-extension descriptor kind reserved, but do not implement
   `tp_dealloc` semantics in this milestone.

Validation:

- Descriptor parity tests for migrated static layouts.
- Descriptor parity tests for migrated dynamic layouts.
- Tests that reclaimed objects clear owned values through the descriptor facade.
- Tests that compact and expanded dynamic tuple layouts still reclaim correctly.
- Tests that descriptor scanning and bitmap header discovery remain independent.

## Phase 6: Reclamation Policy

Add production reclamation triggers after bitmap discovery and descriptor
scanning are stable.

1. [ ] Add counters for:
   - ZCT length;
   - ordinary inactive slab count since last reclamation;
   - dedicated large-object bytes since last reclamation;
   - committed bitmap entries scanned;
   - objects reclaimed;
   - objects retained by stack roots;
   - slabs released.
2. [ ] Request reclamation on ZCT growth.
3. [ ] Request reclamation when ordinary inactive slabs since the previous
   reclamation crosses a threshold.
4. [ ] Request reclamation when dedicated large-object bytes since the previous
   reclamation crosses a threshold.
5. [ ] Request reclamation on committed-object scan budget.
6. [ ] Coalesce multiple pending requests into one safepoint.
7. [ ] Keep every-safepoint reclamation as deterministic testing mode only.

Validation:

- Tests that each trigger requests a safepoint/reclamation without immediate
  arbitrary-point reclamation.
- Tests that multiple pending requests coalesce.
- Stress tests that allocate, drop, and safepoint repeatedly under the policy
  triggers.
- Counter sanity checks in debug builds.

## Phase 7: Size-Partitioned Thread-Local Heaps

Split ordinary allocations into size partitions after bitmap reclamation is
stable. The goal is better lifetime and size locality, not partial-slab hole
allocation.

1. [ ] Compute final aligned allocation size before classifying.
2. [ ] Define a small initial set of size classes.
3. [ ] Replace the single ordinary active slab with active slabs by size class in
   `ThreadLocalHeap`.
4. [ ] Route small and medium allocations to the active slab for their size
   class.
5. [ ] On exhaustion, open a fresh slab for that size class and remember it in
   `slabs_active_since_reclamation`.
6. [ ] Keep dedicated large-object slabs outside ordinary size classes.
7. [ ] Reset active-since-reclamation lists after reclamation to all currently
   active size-class slabs.
8. [ ] Preserve the post-bootstrap `switch_to_new_slabs()` behavior across all
   active size classes.

Validation:

- Tests for classification using final aligned allocation size.
- Tests that each size class has independent active slab switching.
- Tests that size-class slab switches update the active-since-reclamation list.
- Tests that bitmap young-object discovery scans all active size-class slabs
  after post-reclamation reset.
- Allocation and interpreter tests continue to pass.

## Later Work

- Multi-thread `Attached` / `Detached` / `GC` state model.
- Thread exit ZCT handoff.
- Parallel root scanning.
- Packed atomic refcount/lifecycle state for no-GIL.
- Cycle collection.
- Python finalizers, weakrefs, resurrection, and C-extension `tp_dealloc`.
- Native stack scanning.
- Per-PC safepoint maps.
- Partial-slab hole allocation, if measurements ever justify it.
