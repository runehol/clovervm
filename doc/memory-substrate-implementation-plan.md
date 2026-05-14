# Memory Substrate Implementation Plan

This document is the step-by-step implementation plan for the memory substrate
described in [Refcounting and Safepoints](refcounting-and-safepoints.md),
[Heap Slab Allocation and Reuse](heap-slab-allocation-and-reuse.md), and
[Layout-ID-Driven Value Scanning and Deallocation Dispatch](layout-id-driven-scanning.md).

The goal is a correct single-threaded baseline for deferred reference counting,
root discovery, heap object teardown, and slab accounting. The first pass should
use one ordinary slab family for regular allocations; size-class routing comes
later. The implementation should keep API shapes compatible with later
multi-threaded safepoint coordination, but it should not attempt no-GIL
execution, Python finalizers, cycle collection, size-class routing, or
C-extension deallocation semantics in the first pass.

## Ground Rules

- Follow the durable ownership, ZCT, stack scanning, and safepoint invariants in
  [Refcounting and Safepoints](refcounting-and-safepoints.md).
- Follow the durable slab accounting and handoff invariants in
  [Heap Slab Allocation and Reuse](heap-slab-allocation-and-reuse.md).
- Follow the durable heap layout descriptor and teardown invariants in
  [Layout-ID-Driven Value Scanning and Deallocation Dispatch](layout-id-driven-scanning.md).
- Out-of-memory during reclamation is fatal in the baseline. Root-set
  allocation, debug duplicate-ZCT checks, and other reclamation bookkeeping do
  not need recovery paths in the first implementation.
- Preserve interpreter dispatch shape and `MUSTTAIL` constraints when adding
  safepoint polling.

## First Vertical Milestone

Phases 1 through 7 now form one vertical correctness milestone: lifecycle/ZCT
plumbing, slab accounting, value-span teardown, safepoint arrival, root
collection, ZCT processing, and whole-slab release. Early phases should add
internal assertions and focused unit tests where possible, but full semantic
validation only becomes meaningful once every-safepoint reclamation, managed
root collection, serial ZCT processing, and slab blocker release are all in
place.

## Phase 1: Heap Lifecycle State And ZCT Plumbing

1. [x] Add a heap lifecycle state to `HeapObject`:
   - `Normal`
   - `InZct`
   - `Reclaiming`
   - `Dead`
2. [x] Add a per-`ThreadState` zero count table represented as
   `std::vector<HeapObject *>`.
3. [x] Route `DECREF` zero transitions through a helper that performs only
   `Normal -> InZct` and appends exactly one ZCT entry.
4. [x] Keep current refcount fields plain in the single-threaded
   implementation, but isolate transitions behind helpers so a future packed
   atomic state can replace them.
5. [x] Add debug assertions for illegal transitions, especially attempts to enqueue
   `InZct`, `Reclaiming`, `Dead`, or non-reclaimable-tagged objects.
6. [x] Ensure every committed reclaimable allocation with `refcount == 0` is
   explicitly enqueued in the ZCT. This is intentionally eager; slab enumeration
   can optimize it later.
7. [x] Thread allocation through a reclamation context rooted in `ThreadState`.
   Any allocation entry point that creates reclaimable objects must take or
   recover that context.
8. [x] Refcount mutation is centralized through helpers in `value.h`. Audit those
   helpers and their callers so every zero transition enqueues through the ZCT
   path. The existing data structures were developed with "zero means enqueue"
   in mind, but this path is lightly tested and should get focused coverage.

Validation:

- [x] Unit tests for duplicate enqueue prevention.
- [x] Debug-only checks that every ZCT entry has lifecycle state `InZct`.
- [x] Tests for `OwnedValue`, `MemberValue`, and container/object stores that
  exercise zero-refcount enqueue rather than immediate recursive destruction.
- [x] Cross-phase validation after Phases 4 and 5: tests showing a newly allocated
  object that only lives in frame slots is retained while rooted and reclaimed
  after the frame drops it.

## Phase 2: Allocation-Time Slab Accounting And Slab Pinning

1. [x] Split allocation into reserve, construct, and construction-failure cleanup
   concepts where needed.
2. [x] Rename `slab->n_live_objects` to `slab->n_reclaim_blockers` and implement
   the counter rules from the reclamation design.
3. [x] Implement allocator-open reclaim blockers for ordinary slabs.
4. [x] Increment the object reclaim blocker when memory is reserved for an object.
5. [x] Implement the same blocker rules for dedicated large-object slabs.
6. [x] On constructor failure, use the cold path to find the owning slab and drop
   the object reclaim blocker. The bump memory remains abandoned.
7. [x] Add or preserve enough allocator metadata to find the owning slab for a
   `HeapObject *` or failed construction address.
8. [x] Introduce the global slab lookup granule map described in the reclamation
   design, using a 4 KiB initial lookup granule.
9. [x] Register every granule covered by ordinary and dedicated slabs when the slab
   is created.
10. [x] Make each slab remember its owning `GlobalHeap` and hand itself back when
    `n_reclaim_blockers` reaches zero.

Validation:

- [x] Tests that failed construction drops the object reclaim blocker exactly once.
- [x] Tests that constructed objects decrement their owning slab during
  reclamation without reading reclaimed object memory.
- [x] Tests that an otherwise empty slab is not reset while installed as an active
  allocation slab.
- [x] Tests that a dedicated large-object slab has no active-allocator blocker,
  gains one object blocker on allocation, and is handed back when that object
  blocker is dropped on construction failure or later reclamation.
- [x] Debug assertions that every reclaimable object maps to exactly one slab.

## Phase 3: Minimal Layout Descriptor Facade

1. [ ] Introduce a descriptor-shaped API for object size, owned-child scanning,
   and native teardown, following the layout-ID design. The mechanical heap
   value-span scanner has been narrowed to an `ObjectValueSpan` helper for a
   concrete heap object, but the broader layout-ID descriptor facade is not in
   place yet.
2. [ ] Implement the initial native descriptor path using metadata descriptors for
   layouts that can be described by size plus scanned `Value` regions.
3. [ ] Bridge any still-unmigrated layout through existing `HeapLayout` decoding
   only as a compatibility path, not as the main reclamation interface. The
   current scanner still reads `HeapLayout` directly.
4. [ ] Route reclamation child scanning and teardown through the descriptor facade,
   not through ad hoc metadata decoding at the reclamation call sites.
5. [ ] Add startup or debug validation that metadata descriptors match the existing
   object layout metadata for migrated layouts.
6. [ ] Add custom dynamic descriptor handlers only for layouts that cannot be
   expressed cleanly by ordinary metadata descriptors.
7. [ ] Migrate fixed native layouts to `NativeLayoutId` metadata descriptors in
   small groups as the safepoint/ZCT invariants come online.
8. [x] Keep C-extension descriptor kind as a reserved design point, but do not
   implement extension `tp_dealloc` behavior in the baseline.

Validation:

- [x] Tests that reclamation uses the object value-span helper for owned-child
  scanning.
- [ ] Descriptor parity tests for metadata descriptor entries as they are added.
- [ ] Focused teardown tests for objects with owned children through the facade.
  There is a mechanical value-span deallocator test, but it is not yet through
  the descriptor facade.
- [x] Compact dynamic-layout reclamation test for tuple.
- [x] Expanded dynamic-layout reclamation test for tuple.

## Phase 4: Single-Threaded Safepoint Request And Arrival

1. [x] Add a global or VM-owned pending-safepoint flag.
2. [x] Add a safepoint scan record to `ThreadState`:
   - `lowest_live_stack_slot`
   - `accumulator_or_not_present`
3. [x] Add committed-state interpreter hooks, not standalone safepoint
   bytecodes:
   - committed safepoint with no live accumulator
   - committed safepoint with live accumulator
4. [ ] Insert safepoint polls only at initial scan-safe locations:
   - [x] function entry after frame setup and argument adaptation
   - [x] normal function return after the caller frame has been restored
   - [ ] codegen-marked loop back edges
   Exception propagation and unwind paths are not safepoint locations in the
   first pass.
5. [x] Keep the safepoint fast path as a cheap flag check that preserves hot opcode
   handler shape.
6. [x] Put scan-record publishing and VM-global reclamation in cold slow paths.
   `ThreadState::handle_safepoint()` runs per-arrival testing callbacks with the
   active thread installed, then enters `NoActiveThreadScope` and calls
   `VirtualMachine::complete_safepoint()`. `complete_safepoint()` owns
   `run_heap_reclamation(threads)`, clears the request, and refires testing mode
   if needed.
7. [x] Add a debug/test mode that treats every committed safepoint as a
   reclamation trigger. In this mode, the VM keeps a safepoint request pending
   after each completion, so every allowed call/return poll publishes its scan
   record and runs heap reclamation even if ordinary allocation pressure would
   not request a safepoint.
8. [ ] Audit safepoint check placement so no check is reachable while borrowed
   `Value`s are live only in C++ helper locals.

Validation:

- [ ] Interpreter tests that safepoints can be requested and reached at entry,
  return, and eventually loop back edges.
- [x] Reclamation tests run with the every-safepoint reclamation mode enabled.
- [x] Existing hot-path frame checks still pass for hot opcode handlers.

## Phase 5: Conservative Managed Root Collection

1. [x] Implement stack scanning from the published `lowest_live_stack_slot` up to
   the permanent Clover frame sentinel stored in `ThreadState`.
2. [x] Insert every refcounted-pointer-shaped slot into a temporary
   `absl::flat_hash_set<HeapObject *>`.
3. [x] Insert `accumulator_or_not_present` if it has refcounted pointer shape.
4. [x] Do not dereference candidate roots or validate them against allocator
   metadata during stack scanning.
5. Treat stack-scanned values only as a filter against the ZCT.
6. Prefer targeted clearing only if conservative retention becomes visible in
   tests or benchmarks.
7. [x] In debug builds, validate that `lowest_live_stack_slot` is within the managed
   stack bounds and does not scan past the `ThreadState` sentinel.

Validation:

- [x] Tests where a zero-refcount object is protected only by a live frame slot.
- [x] Tests where a zero-refcount return value is protected only by the published
  accumulator.
- [x] Tests showing stale pointer-shaped non-ZCT values in scanned slots do not
  affect correctness and are not dereferenced.

## Phase 6: Serial ZCT Processing

1. [x] Build the safepoint-local root set before scanning any ZCT.
2. [x] Process each ZCT with index-based scan/keep compaction and a dynamic scan
   bound.
3. For each entry:
   - [x] if `refcount > 0`, transition `InZct -> Normal`
   - [x] if `refcount == 0` and the object is in roots, keep it in `InZct`
   - [x] if `refcount == 0` and the object is not in roots, transition to
     `Reclaiming` for currently supported metadata layouts.
4. [x] Tear down `Reclaiming` objects through reclamation-specific child release,
   not ordinary hot-path `decref()` calls. This is currently wired through
   `ObjectValueSpan`; full layout-ID descriptor-facade routing remains Phase 3
   work.
5. [x] When child releases reach zero, append them to the ZCT currently being
   processed so cascades can be handled in the same safepoint.
6. [x] After teardown, transition the object to `Dead`, decrement its slab
   reclaim-blocker count, and make the allocation invalid for ordinary heap use
   for currently supported compact and expanded metadata layouts.
7. [x] Add debug validation that no heap object appears in more than one ZCT.

Validation:

- [x] Tests for root-kept ZCT entries remaining in the ZCT across safepoints.
- [x] Tests for positive-refcount stale ZCT entries returning to `Normal`.
- [x] Tests for unrooted ZCT entries being reclaimed without inspecting the
  object after its blocker may have released the backing slab.
- [x] Tests for cascaded child reclamation during the same safepoint.
- [x] Tests for duplicate-ZCT detection in debug builds.

## Phase 7: Whole-Slab Release

1. [x] When object teardown decrements the slab reclaim-blocker count to zero,
   hand the slab to `GlobalHeap` for reclamation immediately.
2. [x] Treat active allocation installation as a slab reclaim blocker. A slab with no
   constructed objects but still installed in a `ThreadLocalHeap` is not eligible
   for release.
3. [x] Return empty dedicated large-object slabs to the release path.
4. [x] Keep partial-slab hole allocation out of the baseline.
5. [x] Switch the VM's default thread to fresh slabs after builtin
   initialization, so bootstrap objects and runtime allocations naturally
   separate by lifetime. `ThreadLocalHeap::switch_to_new_slabs()` opens new
   allocator slabs; future size-class routing will switch one active slab family
   at a time.

Validation:

- [x] Tests that a slab is not released while still installed as a thread's active
  allocation slab, even if no constructed objects remain in it.
- [x] Tests that a dedicated large-object slab whose only blocker is the object
  blocker disappears from slab lookup after reclamation.
- [x] Tests that VM bootstrap switches post-initialization allocations to fresh
  slabs.
- [x] Existing allocation and interpreter tests continue to pass.

## Phase 8: Policy And Pressure

1. Add ZCT growth and allocation pressure triggers after correctness tests are
   stable.
2. Keep multiple pending requests coalesced into one safepoint.
3. Add debug counters for:
   - ZCT entries processed
   - objects reclaimed
   - objects kept by stack roots
   - objects returned to `Normal`
   - slabs released
4. Add failure-mode tests for allocation pressure while a safepoint is pending.

Validation:

- Deterministic reclamation tests using every-safepoint reclamation mode.
- Stress tests that allocate, drop, and safepoint repeatedly.
- Counter sanity checks in debug builds.

## Phase 9: Multi-Thread-Ready API Shape

Do this only after the single-threaded baseline is correct.

1. Have the safepoint coordinator iterate the registered `ThreadState`s already
   tracked by `VirtualMachine`.
2. For a thread with no managed Clover stack, publish `lowest_live_stack_slot`
   equal to that thread's `ThreadState` sentinel.
3. Add `Attached`, `Detached`, and `GC` states without enabling arbitrary
   concurrent object mutation yet.
4. Require thread exit to transfer any non-empty ZCT to another live thread.
5. Keep root scanning parallelization and atomic packed refcount/lifecycle state
   as later work.

Validation:

- Single-threaded behavior remains unchanged through the coordinator path.
- Tests for no-managed-stack sentinel records and thread ZCT handoff once
  threads exist.

## Later Extension: Size-Class Slab Routing

Do not implement size-class routing in the first pass. The baseline allocator
uses one ordinary slab family for regular allocations, plus the existing
dedicated path for large allocations if needed. After reclamation invariants are
working, ordinary slabs can be split into size-class families for placement.

1. Compute final aligned allocation size before classification.
2. Route small and medium allocations to thread-local active slabs by size
   class.
3. Open fresh class-specific active slabs when the current slab for that class
   is exhausted.
4. Route allocations above the dedicated-slab threshold to one-object dedicated
   slabs.
5. Keep slab sizes page-aligned, quantized, and bounded by policy constants.

Validation:

- Tests for classification using final aligned size.
- Tests for dedicated large-object slabs.
- Benchmarks comparing allocation-heavy workloads before and after routing.

## Explicit Non-Goals For The Baseline

- Cycle collection.
- Partial-slab hole allocation.
- Python-level finalizers, weakref callbacks, or resurrection.
- C-extension `tp_dealloc` integration.
- Size-class slab routing.
- Native stack scanning.
- Per-PC safepoint maps.
- Safepoints during call preparation or argument adaptation.
- Safepoints during exception propagation or unwind.
- Parallel ZCT processing.
- Atomic no-GIL refcount/lifecycle transitions.

## Completion Criteria

The baseline memory substrate is complete when:

1. Objects can be allocated, committed, dropped to zero heap refcount, retained
   by managed stack roots across safepoints, and reclaimed after those roots are
   gone. This completion criterion covers acyclic unreachable reclaimable
   objects; reclaiming cycles is not part of the baseline.
2. Object teardown releases owned children through descriptor-driven scanning,
   and cascaded children can be reclaimed during the same safepoint. The current
   implementation uses `ObjectValueSpan` over decoded `HeapLayout`; the
   layout-ID descriptor facade is still future work.
3. Slab reclaim-blocker counts match object allocation lifetimes plus active
   allocator installation, and fully unblocked slabs are released.
4. Safepoint polls exist only at the agreed initial scan-safe locations.
5. Debug checks can detect duplicate ZCT entries and missing slab ownership.
6. `ninja -C build-debug all check` passes.
