# CloverVM Memory Reclamation Design (Initial Version)

## Overview

This document describes the initial memory reclamation strategy for CloverVM. The design combines:

- Thread-local bump allocation
- Atomic reference counting (RC)
- Deferred reclamation via safepoints
- Whole-slab reuse

The system is explicitly non-moving to preserve object address stability, which is required for compatibility with C extensions and direct pointer usage.

---

## Allocation Architecture

### Thread-Local Allocation

Each thread owns a ThreadLocalHeap containing a current slab allocator:

- Allocation is performed via bump pointer within the current slab.
- This path is lock-free and extremely fast.
- When the slab is exhausted, a new slab is requested from the global heap.

### Global Heap

The GlobalHeap is responsible for:

- Allocating new slabs (via OS-backed allocation)
- Allocating large objects (see below)
- Maintaining a pool of free slabs for reuse

It is not involved in per-object allocation on the fast path.

---

## Large Object Allocation

Objects exceeding a size threshold (LargeAllocationSize) are allocated separately:

- A dedicated slab is created sized specifically for the object.
- The slab contains exactly one object.
- This slab is logically owned by the allocating thread.

This simplifies reclamation: when the object is reclaimed, the entire slab becomes reusable.

---

## Object Lifetime Model

### Reference Counting

- Heap references use atomic INCREF / DECREF operations.
- When DECREF reduces the reference count to zero, the object is not immediately reclaimed.

### Zero Count Table (ZCT)

- Objects whose reference count reaches zero are placed in a per-thread Zero Count Table.
- These objects are considered candidates for reclamation, not immediately dead.

### Safepoint Validation

Reclamation occurs only at safepoints:

1. All threads reach a safepoint.
2. Stack and register roots are scanned.
3. Objects in the ZCT that are still reachable are removed.
4. Remaining objects are considered truly dead and are reclaimed.

---

## Slab Reclamation Strategy

### Slab Metadata

Each slab maintains:

- n_live_objects: number of committed, live objects in the slab

### Object Commitment

Object allocation proceeds in two phases:

1. Reserve raw memory via bump allocation.
2. Initialize object state sufficiently for safe teardown.
3. Commit the object:
   - Increment slab->n_live_objects
   - Make the object visible to the system

Only committed objects participate in reference counting and reclamation.

### Reclamation

At safepoint:

- Each truly dead object is:
  - Finalized (destructor / field DECREF)
  - Accounted for by decrementing its slab’s n_live_objects

### Whole-Slab Reuse

- When slab->n_live_objects == 0, the slab is fully dead.
- The slab is returned to a global free slab pool.
- Future allocations may reuse slabs from this pool instead of allocating new ones.

---

## Concurrency Model

### Reference Counts

- Object reference counts are atomic, as references may be modified by multiple threads.

### Slab Counters

- slab->n_live_objects is non-atomic.
- It is only modified during controlled reclamation phases.
- Slabs are logically owned by a single thread for allocation purposes.

### Global Structures

- The free slab pool is synchronized.
- The global heap is only accessed on slow paths.

---

## Failure and Exception Handling

To maintain correctness:

- slab->n_live_objects is incremented only after successful object commitment.
- If object construction fails before commit:
  - The reserved memory is abandoned
  - No slab accounting is affected

---

## Properties

### Advantages

- Fast allocation path (bump pointer, thread-local)
- No object movement (stable addresses)
- Simple reclamation model
- Low synchronization overhead

### Limitations

- No partial slab reuse (fragmentation possible)
- Cyclic garbage is not reclaimed
- Reclamation is delayed until safepoints

---

## Future Extensions

Possible future improvements include:

- Partial slab reuse
- Cycle detection and collection
- More sophisticated slab reuse policies

---

## Summary

The system combines deferred reference counting with whole-slab reclamation:

- Allocation is fast and thread-local
- Reclamation is deferred and validated at safepoints
- Memory reuse occurs at slab granularity
