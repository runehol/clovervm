# Memory Substrate Implementation Plan

This document is the current forward plan for CloverVM's memory substrate. The
historical lifecycle/ZCT/safepoint baseline is implemented; this
plan now tracks the next design arc:

1. bitmap-based valid-object tracking and young-object discovery;
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
- Active allocator and epoch-discovery ownership are represented by slab pins.
  Valid object headers are represented by per-slab valid-object bitmap bits.
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
- Slab walking must discover valid object headers from slab metadata, not by
  scanning owned values.
- Keep allocation fast paths small. Per-allocation work must be justified by
  removing equal or greater existing work.
- Keep no-GIL atomic refcount/lifecycle state, cycle collection, Python
  finalizers, weakrefs, C-extension `tp_dealloc`, partial-slab hole allocation,
  and native stack scanning out of this milestone.

## Phase 1: Valid-Object Header Bitmap

Replace per-object slab reclaim blockers with a valid-object header bitmap
as described in
[Committed-Object Bitmap Reclamation](committed-object-bitmap-reclamation.md).

1. [x] Add fixed-size valid-object bitmap metadata to `SlabAllocator`.
   - Derive bitmap word count from `DefaultSlabSize` and the 32-byte heap pointer
     granularity.
   - Keep `first_object_header` in slab metadata.
   - Do not add summary counters or nonempty-word masks until measurements show
     they are needed.
2. [x] Add helpers for:
   - `mark_valid_object(HeapObject *)`
   - `clear_valid_object(HeapObject *)`
   - `has_valid_objects()`
   - iteration over valid object headers by scanning set bits.
3. [x] Set the valid-object bit only after successful construction.
4. [x] Leave failed construction as abandoned bump memory with no valid bit.
5. [x] Keep dedicated large-object slabs on the same bitmap API. They should only
   set bit 0.
6. [x] Add debug assertions for header alignment, bit index bounds, duplicate
   bit set, and clearing absent bits.

Validation:

- Tests that successful construction marks exactly one valid header bit.
- Tests that constructor failure does not mark a valid header bit.
- Tests that bitmap iteration returns valid objects and skips abandoned bump
  gaps.
- Tests that dedicated large-object slabs use the same bitmap path and only set
  bit 0.

## Phase 2: Slab Pins and Heap-Owned Release

Move slab release authority out of slabs and into `GlobalHeap`.

1. [x] Split slab pins from valid object presence.
   - The bitmap represents valid object headers.
   - `n_slab_pins` represents slab lifetime ownership.
   - Named methods distinguish active allocator ownership from epoch discovery
     ownership while updating the same counter:
     `add_active_allocator_pin()`, `drop_active_allocator_pin()`,
     `add_epoch_discovery_pin()`, `drop_epoch_discovery_pin()`.
2. [x] Replace object reclaim-blocker increments/decrements with bitmap
   mark/clear.
3. [x] Remove the `GlobalHeap *` back-pointer from `SlabAllocator`.
4. [x] Add `GlobalHeap::release_slab_if_empty(SlabAllocator *)`.
5. [x] Keep pin add/drop symmetric and local to `SlabAllocator`; dropping a pin
   must not release or unmap a slab as a side effect.
6. [x] During object teardown, clear valid-object bits and remember release
   candidate slabs;
   do not release/unmap slabs immediately from object teardown.
7. [x] After explicit batch points, run `release_slab_if_empty()` for release
   candidate slabs:
   - after reclamation drops epoch discovery pins and clears valid-object bits.
   - active slab switches drop the old active allocator pin but do not run a
     release check there, because the epoch discovery pin keeps the slab alive
     until reclamation.

Validation:

- Tests that an empty slab with an active allocator pin is not released.
- Tests that dropping the final pin does not release a slab until the caller runs
  `release_slab_if_empty()`.
- Tests that reclaiming the final valid object releases the slab only during
  the batched post-reclamation release step.
- Tests that epoch-list membership pins a slab even after it is no longer the
  active allocation slab.
- Tests that slab lookup no longer finds released ordinary and dedicated slabs.

## Phase 3: Thread-Local Epoch Slab Lists

Track young-object candidate slabs at the allocator-epoch level, not per
allocation.

1. [x] Add `ThreadLocalHeap::epoch_slabs_since_reclamation`.
2. [x] Add `ThreadLocalHeap::ordinary_inactive_slabs_since_reclamation`, seeded
   to zero after each reclamation.
3. [x] Add `ThreadLocalHeap::dedicated_large_bytes_since_reclamation` for
   large-object policy. Dedicated slabs use the same epoch slab list as ordinary
   slabs.
4. [x] Add a helper to remember an active slab once per reclamation epoch.
   - Each call appends one epoch-list membership and adds one epoch discovery
     pin.
   - Allocation/switch paths should make duplicate membership impossible by
     design; add debug assertions for that invariant.
5. [x] Update the list when a thread-local heap installs or switches active
   slabs:
   - construction of `ThreadLocalHeap`;
   - ordinary slab exhaustion;
   - `switch_to_new_slabs()`;
   - future size-class active slab switches.
6. [x] Increment `ordinary_inactive_slabs_since_reclamation` when an ordinary
   active slab is switched out. Drop the old slab's active allocator pin and
   rely on its epoch discovery pin to keep it alive until reclamation scans the
   epoch list and performs batched release checks.
7. [x] Track dedicated large-object slabs in the shared epoch list and
   dedicated byte counter, not in the ordinary inactive-slab counter.
   - First insertion adds an epoch discovery pin.
   - Construction failure before commit leaves an unmarked allocation in the
     epoch-pinned slab; the normal epoch finish drops the pin and release-checks
     the slab.
8. [x] Do not update the epoch slab list on every allocation.
9. [x] After each reclamation, reset each thread-local heap's epoch slab list to
   the slabs currently open for allocation, reset the ordinary inactive counter
   to zero, and clear the dedicated large-object byte counter.
   - Drop epoch discovery pins for scanned epoch slabs.
   - Run release checks for scanned epoch slabs whose pins were dropped.
   - Add epoch discovery pins for current active slabs that seed the next epoch.
10. [x] Expose these lists and counters to VM-global reclamation through
    `ThreadState` or `ThreadLocalHeap` accessors.

Validation:

- Tests that slab switches append slabs to the epoch slab list.
- Tests that post-reclamation reset leaves current active slabs on the list.
- Tests that ordinary slab switches increment the inactive ordinary slab counter
  and post-reclamation reset clears it.
- Tests that dedicated large-object slabs update the shared epoch list and
  dedicated byte counter but not the ordinary inactive slab counter.
- Tests that ordinary allocation does not mutate the epoch slab list.
- Tests that a slab present only through epoch-list ownership survives until the
  epoch pin is dropped and a release check runs.

## Phase 4: Bitmap-Based Young-Object Discovery

Use epoch slab bitmaps to discover young zero-refcount
objects that no longer enter the ZCT at allocation time.

1. [x] Extend `run_heap_reclamation()` to collect roots once, then process:
   - existing ZCT entries;
   - bitmap-discovered young candidates from each thread-local heap's
     epoch slab list, including dedicated large-object slabs.
2. [x] For each bitmap-discovered object:
   - ignore it if `refcount > 0`;
   - ignore it if `lifecycle_state != Normal`;
   - if `refcount == 0 && lifecycle_state == Normal && roots.contains(obj)`,
     transition `Normal -> InZct` and append it to a ZCT;
   - otherwise transition `Normal -> Reclaiming` and reclaim through the normal
     teardown path.
3. [x] Ensure young rooted objects become durable ZCT entries, because no heap
   `DECREF` may rediscover them after the stack root disappears.
4. [x] Keep child-release cascades appending to the currently processed ZCT.
5. [x] Remove eager allocation-time ZCT enqueue once bitmap discovery covers
   young zero-refcount objects.
6. [x] Keep positive-refcount allocation behavior out of the ZCT.
7. [x] During reclamation, drop epoch discovery pins only after the corresponding
   epoch slabs have been scanned; run slab release checks only after candidate
   sources are done.

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
   - valid-object bitmap entries scanned;
   - objects reclaimed;
   - objects retained by stack roots;
   - slabs released.
2. [ ] Request reclamation on ZCT growth.
3. [ ] Request reclamation when ordinary inactive slabs since the previous
   reclamation crosses a threshold.
4. [ ] Request reclamation when dedicated large-object bytes since the previous
   reclamation crosses a threshold.
5. [ ] Request reclamation on valid-object bitmap scan budget.
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
   `epoch_slabs_since_reclamation`.
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

- Refactor `process_zero_count_table_for_reclamation()`, which is now mostly a
  focused test helper, so tests either go through the production per-thread
  reclamation epoch path or through an explicitly test-scoped wrapper.
- Multi-thread `Attached` / `Detached` / `GC` state model.
- Thread exit ZCT handoff.
- Parallel root scanning.
- Packed atomic refcount/lifecycle state for no-GIL.
- Cycle collection.
- Python finalizers, weakrefs, resurrection, and C-extension `tp_dealloc`.
- Native stack scanning.
- Per-PC safepoint maps.
- Partial-slab hole allocation, if measurements ever justify it.
