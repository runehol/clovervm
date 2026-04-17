# CloverVM Memory Reclamation Design (with Size-Class Slabs)

## Overview

This document describes the memory reclamation strategy for CloverVM. The design combines:

- Thread-local bump allocation
- Atomic reference counting (RC)
- Deferred reclamation via safepoints
- Whole-slab reuse
- Size-class slabs for small and medium-sized objects

The system is explicitly non-moving to preserve object address stability, which is required for compatibility with C extensions and direct pointer usage.

The design is structured so that:

- object liveness is determined by RC + ZCT + safepoint validation
- memory reuse initially occurs at slab granularity
- allocation remains fast by routing requests to thread-local active slabs

---

## Allocation Architecture

### Thread-Local Allocation

Each thread owns a `ThreadLocalHeap` containing a set of current slab allocators, one per size class:

- Allocation is performed via bump pointer within the current slab for the selected size class.
- This path is lock-free and extremely fast.
- When the slab for a size class is exhausted, a new slab is requested from the global heap or reused from the free slab pool for that class.

This preserves the fast-path property of the original design while improving packing and reducing fragmentation caused by mixing very different object sizes in the same slab.

### Global Heap

The `GlobalHeap` is responsible for:

- Allocating new slabs (via OS-backed allocation)
- Allocating large objects (see below)
- Maintaining pools of free slabs for reuse
- Serving slab refill requests from thread-local heaps

It is not involved in per-object allocation on the fast path.

---

## Size Classes

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

This simplifies reclamation: when the object is reclaimed, the entire slab becomes reusable.

The dedicated-slab threshold is a policy decision, but it should be tied to the slab sizing strategy and should prevent oversized objects from being mixed into ordinary size-class slabs.

---

## Object Lifetime Model

### Reference Counting

- Heap references use atomic `INCREF` / `DECREF` operations.
- When `DECREF` reduces the reference count to zero, the object is not immediately reclaimed.

### Zero Count Table (ZCT)

- Objects whose reference count reaches zero are placed in a per-thread Zero Count Table.
- These objects are considered candidates for reclamation, not immediately dead.

### Safepoint Validation

Reclamation occurs only at safepoints:

1. All threads reach a safepoint.
2. Stack and register roots are scanned.
3. Objects in the ZCT that are still reachable are removed.
4. Remaining objects are considered truly dead and are reclaimed.

This ensures correctness in the presence of:

- deferred stack/register references
- concurrent execution without a global interpreter lock

---

## Slab Reclamation Strategy

### Slab Metadata

Each slab maintains:

- `n_live_objects`: number of committed, live objects in the slab
- size-class identity or slab kind metadata
- allocation bounds / bump pointer state

### Object Commitment

Object allocation proceeds in two phases:

1. Reserve raw memory via bump allocation from the selected slab.
2. Initialize object state sufficiently for safe teardown.
3. Commit the object:
   - increment `slab->n_live_objects`
   - make the object visible to the system

Only committed objects participate in reference counting and reclamation.

### Reclamation

At safepoint:

- each truly dead object is:
  - finalized (destructor / field `DECREF`)
  - accounted for by decrementing its slab’s `n_live_objects`

### Whole-Slab Reuse

- when `slab->n_live_objects == 0`, the slab is fully dead
- the slab is returned to the free slab pool for its size class, or to the appropriate pool for its slab kind
- future allocations may reuse slabs from that pool instead of allocating new ones

At this stage, memory reuse occurs only at whole-slab granularity.

---

## Allocation Fast Path

For ordinary objects, allocation proceeds as follows:

1. Compute final aligned allocation size.
2. Select a size class from that allocation size.
3. Attempt bump allocation from the thread’s current slab for that class.
4. If exhausted, refill from the class-specific free slab pool or allocate a fresh slab from the global heap.
5. If the allocation exceeds the dedicated-slab threshold, bypass the size-class slab system and allocate a dedicated slab.

This preserves a simple fast path while avoiding general-purpose free-list allocation.

---

## Concurrency Model

### Reference Counts

- Object reference counts are atomic, as references may be modified by multiple threads.

### Slab Counters

- `slab->n_live_objects` is non-atomic.
- It is only modified during controlled reclamation phases.
- Slabs are logically owned by a single thread for allocation purposes.

### Global Structures

- Free slab pools are synchronized.
- The global heap is only accessed on slow paths.

---

## Failure and Exception Handling

To maintain correctness:

- `slab->n_live_objects` is incremented only after successful object commitment.
- If object construction fails before commit:
  - the reserved memory is abandoned
  - no slab accounting is affected

This ensures that every increment of `n_live_objects` corresponds to exactly one eventual decrement during reclamation.

---

## Properties

### Advantages

- Fast allocation path (bump pointer, thread-local)
- No object movement (stable addresses)
- Better placement for similarly sized objects
- Simple reclamation model
- Low synchronization overhead

### Limitations

- No partial slab reuse (fragmentation from mixed lifetimes remains possible)
- Cyclic garbage is not reclaimed
- Reclamation is delayed until safepoints
- More active slab state is maintained per thread than in the single-slab design

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

The system combines deferred reference counting with size-class slab allocation and whole-slab reclamation:

- allocation is fast and thread-local
- object liveness is determined by RC + ZCT + safepoint validation
- small and medium objects are routed to size-class slabs
- large objects use dedicated slabs
- memory reuse occurs at slab granularity

This provides a simple, correct, and performant baseline that improves object placement without introducing partial-slab reuse complexity.
