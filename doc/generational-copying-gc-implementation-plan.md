# Generational Copying GC Implementation Plan

This document describes staging for the collector direction in
[Generational Copying GC Design Notes](generational-copying-gc.md). It is an
implementation plan, not a second collector design.

The implementation order should follow the codebase that exists now. CloverVM
already has native-module and Clover C API work; it does not yet have a CPython
Limited API surface. The first steps should therefore make the existing native
boundary compatible with movable objects before adding compatibility layers that
no current caller uses.

## Stage 1: Clover Native API Handles

Refactor the existing Clover native API boundary before moving ordinary VM
objects.

- Make `clover_handle` an opaque handle to VM-managed storage rather than a raw
  `Value` copy.
- Define the active native-call handle frame that owns transitory handles.
- Add `clover_persistent_handle` for native state that outlives one API entry.
- Ensure C API helpers resolve handles before storing managed values into VM
  objects.

This stage does not require removing deferred refcounting. Refcounting remains
the lifetime authority while the native boundary stops exposing raw movable
`Value` storage.

## Stage 2: Barrier Call Sites While Refcounting Remains

Introduce the generational write-barrier API while refcounting still keeps
objects alive. The first barrier implementation can be no-op or diagnostic-only,
but the call sites should be the real owner-aware heap stores.

Do not hide barriers inside `incref` or `decref`: those helpers know the child
but not the heap owner. The minor-GC invariant is about an old owner storing a
young child, so the barrier belongs where the owner is known.

Initial owner-aware store targets include:

- `Object::write_storage_location`;
- `Object::write_empty_storage_location`;
- `OverflowSlots::set`;
- `ValueArray<T>` and `HeapPtrArray<T>` element and backing replacement paths;
- dict and list storage paths;
- module/global store helpers.

The early barrier shape should preserve hot-path factoring:

- reject non-refcounted `Value`s before computing owner/backing state;
- use known-refcounted entry points when the caller has already proven the
  child is a heap reference;
- pass `ThreadState *` directly when it is already available;
- call `active_thread()` only after proving a store really needs to record an
  old-to-young edge.

## Stage 3: Class And Shape Metadata Policy

Class and shape metadata should bypass the nursery and be allocated directly in
old movable storage. They are not immortal: major GC traces and may reclaim or
move them. Short-lived inner classes and functions can still die when their
class and shape graphs become unreachable, but they do not need to die in a
minor collection.

The intended invariant is:

```text
ClassObject and Shape are class metadata objects. They are never nursery-young.
Object::shape stores and Shape <-> ClassObject metadata links do not need the
ordinary minor-GC write barrier.
```

This keeps `Object::set_shape` and object initialization out of the ordinary
old-to-young barrier path.

Descriptor and transition names are different. Shape-owned name slots may store
ordinary `String` objects. If the stored name is nursery-young, shape
construction or transition creation must either promote the name or record the
shape in a cold metadata remembered set. The cold metadata barrier belongs in
shape construction and transition code, not in `Object::set_shape`.

## Stage 4: Trace Descriptors And Logical Promotion

Add native-layout trace descriptors and use them in a non-moving validation
pass before implementing physical copying.

At safepoints, the validation pass should:

1. publish managed roots;
2. scan roots and remembered old objects;
3. logically promote reachable young objects to old;
4. clear remembered state after each remembered old object is scanned;
5. debug-check that no unremembered old object points into the nursery.

This stage validates root publication, trace descriptors, remembered sets, and
barrier coverage while deferred refcounting still provides object lifetime.

## Stage 5: Copying Nursery

Implement the copying nursery using the same roots, trace descriptors, and
remembered sets validated earlier.

The first nursery policy should remain simple:

- stop the world at safepoints;
- evacuate reachable young objects;
- promote every young survivor en masse;
- update roots, handles, wrapper targets, and remembered old objects;
- clear remembered state and rebuild it through future write barriers.

Deferred refcounting can still remain during this stage as a compatibility and
debugging scaffold. It should be removed or narrowed only after the moving
collector is carrying ordinary managed-object lifetime.

## Deferred Work

Defer these until the native-handle and managed-collector pieces have been
validated:

- CPython Limited API wrapper identity machinery;
- remembered module slots;
- survivor spaces;
- promotion heuristics;
- old-generation tuning;
- card tables;
- region-style remembered sets;
- concurrent copying;
- load barriers.
