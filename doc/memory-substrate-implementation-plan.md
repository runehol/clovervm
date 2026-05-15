# Memory Substrate Implementation Plan

This document is the current forward plan for CloverVM's memory substrate. The
historical lifecycle/ZCT/safepoint baseline, bitmap-discovered young-object
reclamation, and native-layout descriptor migration are implemented. This plan
now tracks the remaining design arc:

1. production reclamation policy;
2. size-partitioned thread-local heaps.

Background design documents:

- [Refcounting and Safepoints](refcounting-and-safepoints.md)
- [Heap Slab Allocation and Reuse](heap-slab-allocation-and-reuse.md)
- [Valid-Object Bitmap Reclamation](committed-object-bitmap-reclamation.md)
- [Native Layout Descriptors](native-layout-descriptors.md)

## Current Baseline

The current implementation has a correct single-threaded deferred-refcount
baseline:

- `HeapObject` lifecycle states: `Normal`, `InZct`, `Reclaiming`, `Dead`.
- Per-thread ZCTs with duplicate-entry validation.
- Refcount zero transitions enqueue through helper APIs.
- Fresh zero-refcount reclaimable allocations do not enter the ZCT at allocation
  time. Reclamation discovers young zero-refcount candidates from per-slab
  valid-object bitmaps; stack-rooted young candidates are moved into the ZCT so
  they remain discoverable later.
- Conservative stack root collection from published safepoint scan records.
- Safepoints embedded in committed call/return paths, not explicit bytecodes.
- VM-global `run_heap_reclamation()` over registered `ThreadState`s.
- Reclamation releases owned cells through `NativeLayoutId` keyed release
  descriptors.
- Reclamation clears owned `Value` cells before releasing copied child values.
- Custom dealloc descriptors run with the reclaimed thread installed as active,
  so deallocators may call ordinary `decref()`.
- Child releases that reach zero append to the ZCT currently being processed.
- `object_size_in_bytes(const HeapObject *)` provides descriptor-driven opaque
  size queries for already-allocated objects.
- Allocation uses concrete type-local `sizeof(T)` or `T::size_for(...)` helpers;
  it does not dispatch allocation sizing by native layout ID.
- `Object` carries Python class identity. Slot-backed Python-visible objects use
  `SlotObject` for inline and overflow attribute storage.
- Slab lookup by 4 KiB granule.
- Active allocator and epoch-discovery ownership are represented by slab pins.
  Valid object headers are represented by per-slab valid-object bitmap bits.
- Thread-local reclamation epochs track ordinary inactive slab pressure and
  dedicated large-object allocation bytes.
- Allocation slow paths request a safepoint when initial slab-pressure policy
  thresholds are crossed.
- Fully unblocked slabs are released/unmapped; ordinary slab pooling is not part
  of the current plan.
- VM bootstrap switches the default thread to fresh slabs after builtin setup.

Near-term order:

1. Add production reclamation triggers and counters on top of the descriptor
   teardown boundary.
2. Split ordinary heaps into size partitions. Partial-slab hole reuse, if it
   ever happens, belongs after size classes make hole sizes predictable.

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
  finalizers, weakrefs, public C-extension `tp_dealloc`, partial-slab hole
  allocation, and native stack scanning out of this milestone.

## Phase 1: Reclamation Policy

Add production reclamation triggers after bitmap discovery and descriptor
scanning are stable. The initial slab-pressure hook is intentionally small:
`ThreadLocalHeap::allocate_slow()` requests a safepoint when inactive epoch slab
pressure or dedicated large-object bytes cross fixed thresholds.

1. [ ] Surface or refine counters for:
   - ZCT length;
   - valid-object bitmap entries scanned;
   - objects reclaimed;
   - objects retained by stack roots;
   - slabs released.
2. [ ] Request reclamation on ZCT growth.
3. [ ] Request reclamation on valid-object bitmap scan budget.
4. [ ] Coalesce multiple pending requests into one safepoint.
5. [ ] Tune and document the initial slab-pressure thresholds.
6. [ ] Keep every-safepoint reclamation as deterministic testing mode only.

Validation:

- Tests that each future trigger requests a safepoint/reclamation without
  immediate arbitrary-point reclamation.
- Tests that multiple pending requests coalesce.
- Stress tests that allocate, drop, and safepoint repeatedly under the policy
  triggers.
- Counter sanity checks in debug builds.

## Phase 2: Size-Partitioned Thread-Local Heaps

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

- Multi-thread `Attached` / `Detached` / `GC` state model.
- Thread exit ZCT and epoch slab handoff. This matters once threads can exit
  independently; it is not a blocker for the current single-threaded milestone.
- Parallel root scanning.
- Packed atomic refcount/lifecycle state for no-GIL.
- Cycle collection.
- Python finalizers, weakrefs, resurrection, and C-extension `tp_dealloc`.
- Native stack scanning.
- Per-PC safepoint maps.
- Partial-slab hole allocation, if measurements ever justify it.
