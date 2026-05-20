# No-Reference Slabs

## Context

The adapted `nbody` benchmark exposes a different allocation profile than the
earlier container-heavy benchmarks. It creates millions of short-lived heap
floats, stores heap floats in lists, and repeatedly overwrites those list slots.
That drives heavy retain/release traffic and pushes a large number of objects
through the zero-count table.

Temporarily disabling reclamation made `BM_NBody/100000` much faster, which
suggests the bottleneck is not just allocation. The current general object
reclamation path is doing too much work for immutable leaf objects such as
floats.

## Idea

Introduce a separate allocation/reclamation path for heap objects that contain
no reclaimable references. These objects would live on no-reference slabs. The
slabs are counted rather than scanned: they do not need release descriptor walks,
child-value scanning, or zero-count table processing.

Candidate layouts:

- `Float`: payload is a `double`;
- `String`: payload is character data;
- future big integers: payload is integer limbs.

The key observation is that builtin immutable instances can use immortal fixed
shapes. Their shape/class metadata is not an ownership-bearing reference from
the instance's perspective. Once that is explicit, these objects are
effectively leaf payloads.

## Required Invariants

An object can use no-reference slabs only if all of these are true:

- its shape is fixed and immortal;
- it cannot grow instance attributes or transition shape;
- it has no `Member<Value>` or `Member<TValue<T>>` fields;
- it has no custom deallocation that releases child heap objects;
- all pointer-like metadata reachable from the object is immortal or otherwise
  non-owning;
- its native layout is known to the allocator and refcount path as
  no-reference.

These invariants are semantic, not just layout trivia. If a type can ever own a
child `Value`, gain per-instance attributes, or participate in mutable shape
transitions, it does not belong on no-reference slabs.

## Reclamation Shape

Today, decref-to-zero sends ordinary refcounted objects to the zero-count table.
For a no-reference object, the fast path can be simpler:

```text
decref(value):
    if value is no-reference object and refcount reaches zero:
        return object storage to the no-reference allocator
    else if ordinary refcounted object and refcount reaches zero:
        add object to the zero-count table
```

No-reference slabs can maintain live/free counts directly. Reclamation does not
need to scan object payloads. A slab can be released when its live count reaches
zero and it has no allocator pins.

For fixed-size objects such as `Float`, a per-layout free list is likely enough.
For variable-size objects such as `String` and future bigints, size classes or a
large-object no-reference path may be needed.

## Why This Helps

Nbody's hot loop produces a large stream of temporary heap floats:

```python
body[3] = body[3] - dx * other_mass * mag
```

Each temporary currently behaves like a full general object:

- allocated from ordinary refcounted slabs;
- retained or released through ordinary `Value` traffic;
- pushed to the zero-count table when it dies;
- later processed by general reclamation.

But a dead `Float` has no children to release. General reclamation work is pure
overhead for it. A no-reference slab path would preserve the Python-visible heap
object while avoiding the general object death pipeline.

This is distinct from NaN tagging or inline doubles. It keeps `Float` as an
object, but gives immutable leaf objects an allocation and reclamation strategy
that matches their actual ownership structure.

## Open Questions

- How should native layout metadata mark no-reference layouts?
- Should the object header distinguish fixed immortal shape pointers from
  ordinary mutable shape pointers?
- Should no-reference deallocation return objects to a per-layout free list,
  per-size-class free list, or counted slab allocator?
- How should this interact with conservative stack roots that may still contain
  stale no-reference object addresses?
- Can interned strings share this path, or should interned storage remain
  separate because of lifetime and deduplication rules?
- Should the first implementation target only `Float`, then generalize once the
  shape is measured?

## Non-Goals

- Do not change the `Value` representation.
- Do not add NaN tagging or inline doubles.
- Do not weaken Python-visible object identity while an object is alive.
- Do not put mutable instances or attribute-bearing objects on no-reference
  slabs.
