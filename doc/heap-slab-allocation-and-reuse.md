# Heap Slab Allocation and Reuse

## Overview

This document describes the heap slab allocation and reuse part of CloverVM's
memory substrate. The design combines:

- Thread-local bump allocation
- Heap reference counting (RC), with atomic operations reserved for the
  multi-threaded/no-GIL version
- Deferred reclamation via safepoints
- Whole-slab release, with reuse as the next pooling step

The system is explicitly non-moving to preserve object address stability, which is required for compatibility with C extensions and direct pointer usage.

The design is structured so that:

- object liveness is determined by RC + ZCT + safepoint validation
- memory is released initially at slab granularity
- allocation remains fast by routing requests to thread-local active slabs

The first implementation uses one ordinary slab family for regular allocations.
Size-class slabs are a later extension; they improve placement but do not change
the lifetime or reclamation model.

---

## Allocation Architecture

### Thread-Local Allocation

Each thread owns a `ThreadLocalHeap` containing its current ordinary slab
allocator:

- Allocation is performed via bump pointer within the current slab.
- This path is lock-free and extremely fast.
- When the slab is exhausted, the slow path opens a fresh slab, transfers the
  active-allocator blocker, and allocates from the new slab. Ordinary free-slab
  reuse is the next policy step.
- `switch_to_new_slabs()` explicitly opens fresh active slabs. VM startup uses
  this after builtin initialization so long-lived bootstrap allocations do not
  share ordinary slabs with later runtime allocations.

This preserves the fast-path property of the original design. Later size-class
routing can improve packing and reduce fragmentation caused by mixing very
different object sizes in the same slab.

### Global Heap

The `GlobalHeap` is responsible for:

- Allocating new slabs (via OS-backed allocation)
- Allocating large objects (see below)
- Maintaining the slab lookup table
- Eventually maintaining pools of free slabs for reuse
- Serving slab refill requests from thread-local heaps

It is not involved in per-object allocation on the fast path.

---

## Size Classes

Size-class slabs are not part of the first implementation pass. The baseline
allocator has one ordinary slab family for regular allocations. The size-class
design below is a later extension that should not obscure the core reclamation
invariants.

### Classification

Small and medium-sized allocations are routed into size classes.

A simple implementation may classify requests by the highest set bit of the allocation size, for example:

```c
class_id = floor_log2(allocation_size);
```

or equivalently using a count-leading-zeros based implementation.

Classification must be performed using the final aligned allocation size, not the raw requested payload size. This includes:

- object header size
- alignment padding
- any expanded-header or layout-dependent size adjustments

### Purpose

Size classes are used to improve object placement, not to change object liveness semantics.

The goal is to cluster similarly sized objects into the same slab family so that:

- allocation remains simple
- slabs have more uniform contents
- whole-slab reuse becomes more effective
- future partial-slab reuse remains possible if needed

### Slab Sizing

Each size class may use a slab size chosen so that the expected number of objects per slab remains within a reasonable target range.

The intent is not to enforce exactly equal object counts across all classes, but to avoid extremes such as:

- very small classes producing excessively large slabs
- medium-sized classes producing slabs with too few objects to make whole-slab reuse effective

Slab sizes should therefore be:

- page-aligned
- quantized to a small set of practical sizes
- bounded by minimum and maximum slab sizes

Very large objects remain outside the size-class slab system and use dedicated slabs.

---

## Large Object Allocation

Objects exceeding a size threshold are allocated separately:

- A dedicated slab is created sized specifically for the object.
- The slab contains exactly one object.
- This slab is logically owned by the allocating thread.

Dedicated large-object slabs follow the same reclaim-blocker rules as ordinary
slabs, with no active-allocator blocker because the dedicated slab is never
installed as an active allocation slab. The allocation reserve itself adds the
object reclaim blocker. If construction fails, the cold failure path drops that
blocker; otherwise the blocker remains until the object is deallocated. When
that object blocker later drops to zero, the slab is handed to `GlobalHeap`.

The dedicated-slab threshold is a policy decision. It should prevent oversized
objects from being mixed into ordinary slabs, and later size-class routing should
keep the threshold consistent with its slab sizing strategy.

---

## Object Lifetime Model

### Reference Counting

- Heap references use `INCREF` / `DECREF` helper operations. The current
  single-threaded implementation uses plain refcount fields behind those
  helpers; the multi-threaded/no-GIL version should make the operations atomic.
- When `DECREF` reduces the reference count to zero, the object is not immediately reclaimed.

### Zero Count Table (ZCT)

- Objects whose reference count reaches zero are placed in a per-thread Zero Count Table.
- These objects are considered candidates for reclamation, not immediately dead.

### Safepoint Validation

Reclamation occurs only after safepoint arrival:

1. All threads reach a safepoint.
2. Stack and register roots are scanned.
3. Objects in the ZCT that are still reachable remain in the ZCT.
4. Remaining objects are considered truly dead and are reclaimed.

This ensures correctness in the presence of:

- deferred stack/register references
- concurrent execution without a global interpreter lock

---

## Slab Reclamation Strategy

### Slab Metadata

Each slab maintains:

- `n_reclaim_blockers`: number of current reasons the slab cannot be reclaimed
- slab family or slab kind metadata
- allocation bounds / bump pointer state

`n_reclaim_blockers` is the authoritative first-pass slab lifetime invariant. A
slab remembers its owning `GlobalHeap` and hands itself back immediately when
this counter reaches zero. The initial `GlobalHeap` policy is to unregister and
`munmap` the slab immediately. Immortal heaps use the same blocker accounting;
their objects are immortal because they are never deallocated, so their object
blockers naturally remain. Do not require a separate slab state enum for
correctness in the first design.

The counter has two sources:

- an allocator that has a slab open for allocation holds one reclaim blocker on
  that slab, and drops it when the slab is closed for allocation;
- each reserved object allocation on the slab holds one reclaim blocker, and
  drops it on constructor failure or when the constructed object is later
  deallocated.

This means a slab with no constructed objects is still not reusable if it
remains installed as an active allocation slab.

### Object Allocation And Construction

Object allocation proceeds in three steps:

1. Reserve raw memory via bump allocation from the selected slab and increment
   `slab->n_reclaim_blockers` for the object allocation.
2. Run the object constructor.
3. If construction fails, the cold failure path finds the owning slab and drops
   the object reclaim blocker. If construction succeeds, make the object visible
   to the system.

Only successfully constructed objects participate in reference counting and
reclamation.

Each constructed object also has a heap lifecycle state. The ZCT lifecycle
prevents duplicate zero-count entries and protects against double reclamation.
See [Refcounting and Safepoints](refcounting-and-safepoints.md) for the
`Normal`, `InZct`, `Reclaiming`, and `Dead` state machine.

### Reclamation

At safepoint:

- each truly dead object is:
  - torn down by clearing owned `Value` slots and releasing copied child values
  - accounted for by decrementing its slab’s `n_reclaim_blockers`

Teardown may release child objects. If those children reach zero, they are added
to the active ZCT and may be processed during the same safepoint.

### Whole-Slab Release And Later Reuse

- when `slab->n_reclaim_blockers == 0`, the slab calls back to its owning
  `GlobalHeap` immediately for reclamation
- the first `GlobalHeap` policy unregisters the slab from the lookup map and
  deallocates it with `munmap`
- a later policy may retain fully reclaimed slabs on a free list instead of
  immediately unmapping them

At this stage, ordinary slabs are not reused; they are released when fully
unblocked. The reuse design remains whole-slab granularity when the free-slab
pool is added.

### Slab Lookup

Reclamation needs to find the owning slab for a dead `HeapObject *` so it can
decrement the slab's reclaim-blocker count and eventually recycle the slab. The
object header should not carry a per-object slab pointer. Instead, slab
ownership is allocator metadata.

The global heap maintains a lookup table from heap lookup granules to slab
metadata:

```c++
std::unordered_map<uintptr_t, Slab *> slab_lookup;
```

The slab lookup granule is fixed at 4 KiB in the first design. Slab mappings are
required to start on a lookup-granule boundary. Logical slab sizes do not need to
be lookup-granule multiples; lookup registration covers every granule touched by
the slab's half-open address range. The lookup key is the address shifted by the
granule shift:

```c++
static constexpr uintptr_t SlabLookupGranuleShift = 12;
static constexpr size_t SlabLookupGranuleSize =
    size_t(1) << SlabLookupGranuleShift;

uintptr_t slab_lookup_key_for_address(void *ptr) {
    return reinterpret_cast<uintptr_t>(ptr) >> SlabLookupGranuleShift;
}
```

On slab creation, every lookup granule covered by the slab is registered to that
slab:

```c++
for(uintptr_t key = slab_lookup_key_for_address(slab->start);
    key <= slab_lookup_key_for_address(slab->end - 1);
    ++key) {
    slab_lookup.emplace(key, slab);
}
```

Lookup during reclamation is then:

```c++
Slab *slab_for_address_unlocked(void *ptr) {
    auto it = slab_lookup.find(slab_lookup_key_for_address(ptr));
    assert(it != slab_lookup.end());
    return it->second;
}
```

The hard invariant is that every reclaimable heap object address maps to exactly
one slab metadata record by slab lookup key. No two slabs may share one lookup
granule. The map is owned by `GlobalHeap`. Registration happens under the heap
lock when a slab is created. Hot lookup uses `slab_for_address_unlocked`;
callers must ensure the map is stable, such as during safepoint reclamation or
while already holding the heap lock. Dedicated large-object slabs use the same
registration mechanism as ordinary slabs.

---

## Allocation Fast Path

For ordinary objects in the first implementation, allocation proceeds as
follows:

1. Compute final aligned allocation size.
2. Attempt bump allocation from the thread’s current ordinary slab.
3. If the fast path cannot satisfy the request, call the non-inlined slow path.
4. If the allocation exceeds the dedicated-slab threshold, bypass the ordinary
   slab path and allocate a dedicated slab.
5. Otherwise open a fresh ordinary slab, add the new allocator blocker, drop the
   old allocator blocker, then reserve the object and add its object blocker.

This preserves a simple fast path while avoiding general-purpose free-list allocation.

---

## Concurrency Model

### Reference Counts

- Object reference counts are plain fields in the first single-threaded design.
- Future multi-threading should make refcount transitions atomic, because
  references may be modified by multiple threads.

### Slab Counters

- `slab->n_reclaim_blockers` is non-atomic in the first single-threaded design.
- Object blockers are dropped during controlled reclamation phases. Allocator
  blockers are added when a slab is opened for allocation and dropped when that
  slab is closed for allocation.
- Slabs are logically owned by a single thread for allocation purposes.

### Global Structures

- Future free slab pools are synchronized.
- The global heap is only accessed on slow paths.

---

## Failure and Exception Handling

To maintain correctness:

- `slab->n_reclaim_blockers` is incremented when memory is reserved for an
  object allocation.
- If object construction fails, the cold failure path finds the owning slab and
  drops that object reclaim blocker.
- The reserved bump memory is abandoned; whole-slab reclamation eventually
  recovers it.

This keeps the normal construction path simple while ensuring every object
allocation blocker gets exactly one decrement, either on construction failure or
during reclamation.

---

## Properties

### Advantages

- Fast allocation path (bump pointer, thread-local)
- No object movement (stable addresses)
- Simple whole-slab release path now, with whole-slab reuse as a later policy
- Simple reclamation model
- Low synchronization overhead

### Limitations

- No partial slab reuse (fragmentation from mixed lifetimes remains possible)
- Cyclic garbage is not reclaimed
- Reclamation is delayed until safepoint completion
- Size-class routing is deferred, so early placement may mix object sizes within
  ordinary slabs
- Ordinary free-slab reuse is deferred; fully unblocked slabs are currently
  unregistered and unmapped

---

## Rationale for Size-Class Slabs

The size-class slab design addresses a different problem from hole tracking.

- Size-class slabs improve placement by clustering similarly sized objects together.
- Whole-slab reuse remains the primary reclamation mechanism.
- The design does not attempt to reuse holes inside partially live slabs.

This means the allocator remains simple and fast, while reducing fragmentation caused by mixing very different object sizes within the same slab family.

If future measurements show that slabs frequently become mostly dead but remain pinned by a small number of surviving objects, then a later design may add partial-slab reuse. That is a separate extension and is not required for the current scheme.

---

## Future Extensions

Possible future improvements include:

- Partial slab reuse within size classes
- Cycle detection and collection
- More sophisticated size-class subdivision
- Lifetime-based segregation in addition to size-based segregation
- More advanced slab reuse policies

---

## Summary

The system combines deferred reference counting with thread-local slab
allocation and whole-slab reclamation:

- allocation is fast and thread-local
- object liveness is determined by RC + ZCT + safepoint validation
- the first implementation uses one ordinary slab family for regular allocations
- large objects use dedicated slabs
- memory reuse occurs at slab granularity

This provides a simple, correct, and performant baseline without introducing
partial-slab reuse complexity. Size-class slabs can be added later to improve
object placement without changing the core reclamation model.
