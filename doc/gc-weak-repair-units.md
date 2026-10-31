# Weak Repair Units

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Not started; current CloverVM hooks are late-only and owner-wide |
| Scope | Enumerated weak repair units, early resolution, exact-unit deferral, and coordinated payload invalidation |
| Owning layers | Layout/cache code enumerates units and owns reset semantics; the generic collector owns resolution phase and deferred scheduling |
| Validated against | Existing IC fields and weak hooks at `38936060` (2026-10-31); proposed API is not implemented |
| Supersedes | Whole-owner deferral proposed during collector design; existing runtime hooks are not yet replaced |

This is the weak-repair part of the
[isolated collector/heap-adapter design](gc-collector-heap-adapter.md).
It does not implement production weak-root or remembered-unit coverage.

A weak repair unit is an addressable object, embedded struct, or standalone
field containing one or more weak references with coordinated invalidation
semantics. Layout code enumerates units directly; they need not be reached by
following heap pointers. Adjacent IC entries are distinct units. Do not defer
the entire `CodeObject` merely because one entry has an unresolved target.

Most targets outside the collected space need no late work. During the object's
scan, resolve each unit as far as possible:

| Target | Early resolver | Late resolver after strong closure |
|---|---|---|
| Outside collected source space | Resolved to the same address | Resolved to the same address |
| Collected source with forwarding | Resolved to its destination | Resolved to its destination |
| Collected source without forwarding | Pending; leave untouched | Dead |

An old target is preserved because a young collection does not reclaim its
storage, not because every old object is a root. Neither resolver marks or
copies through a weak edge. Null/primitive/sentinel fields are handled by their
unit's field-specific rules, not passed as object references to the resolver.

The following CloverVM-facing sketch is new design, not the existing late-only
`WeakTargetResolver` API. The test heap supplies equivalent types for its own
object representation; the generic collector names them through the adapter.

```cpp
enum class WeakState { Resolved, Pending, Dead };
enum class WeakRepairResult { Complete, Pending };

struct WeakResolution
{
    WeakState state;
    HeapObject *target; // Non-null and meaningful only for Resolved.

    static WeakResolution resolved(HeapObject *);
    static WeakResolution pending();
    static WeakResolution dead();
};

using WeakResolver = absl::FunctionRef<WeakResolution(HeapObject *)>;

struct WeakRepairUnit
{
    void *address;
    WeakRepairResult (*repair)(void *, WeakResolver);

    template<auto Repair, typename T>
    static WeakRepairUnit bind(T &value)
    {
        return {&value, [](void *address, WeakResolver resolver) {
            return Repair(*static_cast<T *>(address), resolver);
        }};
    }

    WeakRepairResult try_repair(WeakResolver resolver) const
    {
        return repair(address, resolver);
    }
};
```

The unit is a borrowed address plus a static callback, not a separately allocated
registration object. It does not retain the resolver or its callback state.

### Enumeration and Exact-Unit Deferral

The adapter adds `visit_weak_units(object, visitor)`. For a `CodeObject`, its
layout implementation visits individual entries in each cache table, for example:

```cpp
for(auto &entry: code.inline_caches.attribute_read_caches)
    visitor(WeakRepairUnit::bind<repair_attribute_read_ic>(entry));
```

The collector performs early repair as units are enumerated:

```cpp
heap.visit_weak_units(object, [&](auto unit) {
    if(unit.try_repair(early_resolver) == WeakRepairResult::Pending)
        weak_pending.push_back(unit);
});
```

After the strong copying queue is drained, pop and repair those exact units with
the late resolver. Late resolution cannot remain pending. Do not rescan every
weak field in the owning object or walk the heap to rediscover them. The weak
queue contains only unresolved units, though a pathological workload may defer
every unit. Its allocation failure follows the same fatal policy as other
collector allocation.

### IC Repair and Payload Invalidation

For an attribute-read IC, guards and active payload references belong to one
unit. A schematic callback using the current entry's fields is:

```cpp
WeakRepairResult repair_attribute_read_ic(
    AttributeReadInlineCache &entry, WeakResolver resolver)
{
    if(entry.receiver_shape == nullptr || entry.lookup_validity_cell == nullptr)
    {
        entry.clear();
        return WeakRepairResult::Complete;
    }

    auto shape = repair_pointer(entry.receiver_shape, resolver);
    auto cell = repair_pointer(entry.lookup_validity_cell, resolver);
    auto plan = repair_active_plan(entry.plan, resolver);

    if(shape == WeakState::Dead || cell == WeakState::Dead ||
       plan == WeakState::Dead)
    {
        entry.clear(); // Both guards and the entire payload.
        return WeakRepairResult::Complete;
    }
    return shape == WeakState::Pending || cell == WeakState::Pending ||
                   plan == WeakState::Pending
               ? WeakRepairResult::Pending
               : WeakRepairResult::Complete;
}
```

Field helpers rewrite resolved pointers without ownership operations, report
pending/dead fields without dereferencing dead targets, and respect tagged
values, shape-key sentinels, and the active payload arm. A pending field must
not short-circuit inspecting other fields. The unit can resolve those fields
immediately or invalidate the entire entry if another target is dead.

Invalidation uses the entry's reset/clear operation, including any owned native
plan or managed references. It is not merely nulling weak fields, nor a blind
`memset` of an owning C++ value. Reset may drop references that already
participated in strong tracing; their copies may conservatively survive this
collection. It must not discover new strong references through weak targets.

### Unit Lifetime and Runtime Coverage

Enumerate units in their final destination or already stable storage. Their
addresses must remain valid until late repair: no cache-vector resizing or
owner destruction. Resetting one unit must not invalidate another queued unit;
if weak substructures share a destruction boundary, enumerate the enclosing
repair unit instead of overlapping independently scheduled subunits.

Explicit weak roots can be enumerated as units by their root provider and use
the same early/late protocol. Uncollected old/stable owners still require
weak-remembered coverage of relevant exact units during minor collection; they
are not necessarily encountered by strong copying. Strong-edge remembered sets
alone do not cover weak-only young references. Unit registration must be updated
or removed before storage moves, resizes, or is reclaimed, and must not itself
keep an otherwise dead owner alive. This runtime/barrier/lifetime integration
is a separate readiness boundary, not supplied by the copying template.
