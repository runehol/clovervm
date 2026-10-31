# Weak Repair Units

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Isolated collector and CloverVM layout/cache unit enumeration implemented; production copying and weak remembered-set integration remain deferred |
| Scope | Enumerated weak repair units, early resolution, exact-unit deferral, and coordinated payload invalidation |
| Owning layers | Layout/cache code enumerates units and owns reset semantics; the generic collector owns resolution phase and deferred scheduling |
| Validated against | CloverVM unit-enumeration working tree based on `649dca5b` (2026-10-31); earlier isolated verification recorded below |
| Supersedes | Whole-owner deferral and the late-only owner-wide weak repair hooks |

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

The shared implementation in `src/gc/weak_repair.h` templates the following
types on `Object`; the sketch below uses `HeapObject` to show the CloverVM-facing
form. `memory/heap_reference_slot.h` exposes concrete aliases and the borrowed
`WeakUnitVisitor`. The old pointer-or-null `WeakTargetResolver` API is removed.
The test adapter exposes `WeakRepairUnit<TestObject>` as `WeakUnit`.

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
It occupies two machine words (16 bytes on the 64-bit targets), regardless of
the number of weak fields it repairs. There is no stored owner, phase, status,
captured resolver, or separately allocated callback. The same callback receives
the early resolver and, only if deferred, the late resolver. Unit-specific
invalidation belongs inside that callback, not a second stored callback.

### Enumeration and Exact-Unit Deferral

The adapter exposes `visit_weak_units(object, visitor)`. For a `CodeObject`, its
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

In the isolated implementation, `Evacuation::drain()` scans strong slots and
enumerates weak units in destination objects. `add_weak_root()` accepts explicit
weak roots or exact units supplied by an external provider. `finish()` drains
remaining strong work, seals tracing, and runs late repair on the deferred
units. After it begins late repair, no more strong roots or weak units may be
added. Phase assertions diagnose API misuse; they are caller preconditions,
not a recoverable runtime protocol. Every unit must be supplied exactly once,
without overlapping units that can invalidate each other's storage.

Late callbacks must return `Complete`; the late resolver never returns
`Pending`. Native allocation exceptions during early/late repair reach the
same fatal adapter handler as copying and queue allocation. Source storage
remains valid until `finish()` returns; test-only explicit disposal then
destroys source records without affecting destination records. The generic
collector itself does not release or reuse source storage.

`tests/test_gc_weak_repair.cpp` verifies the two-word/trivially-copyable handle,
early completion, partial repair, exact-unit deferral among adjacent entries,
coordinated guard/payload reset, weak roots, external units, phase sealing,
allocation failure, and explicit source disposal. Its IC-like test entry is
synthetic; real cache reset/ownership coverage is described below.

### CloverVM Layout and Cache Enumeration

`visit_weak_reference_units(object, visitor)` dispatches through the existing
layout descriptor's `visit_weak_units` function pointer. It replaces the old
owner-wide repair hook without growing the descriptor. Layouts without weak
units do not invoke the visitor. `CloverHeapAdapter::visit_weak_units()` delegates
to this common entry point; it still has no production copying operations.

Shape enumerates one unit for its `previous_shape` field. CodeObject enumerates
one unit per entry in all eight cache tables. Enumeration neither repairs nor
changes ownership; the callback inspects every active weak field without
short-circuiting Pending. Dead dominates Pending and resets the whole entry.
Inline/invalid shape keys, null pointers, immediate tagged values, inactive
payload arms, and native handler pointers never reach the resolver. A pending
shape key retains its exact bits until a later repair.

Callbacks use the entries' default-reset assignment to clear guards and payload
together. Keyword-call reset frees its native register map; expanded-call reset
releases its owned keyword-name plan. Successful pointer repair itself is
RC-neutral and preserves storage class through `HeapReferenceSlot`. The caller
must install an active thread when reset can enqueue zero-count objects, and
must exclude reclamation, Python execution, safepoints, and table resizing.
Resetting one entry does not invalidate another entry's repair-unit address.

Existing fake-relocation tests now enumerate units and invoke their callbacks
with a late resolver; there is no second runtime owner-wide repair path.
Additional tests cover exact entry addresses, independent active-field counts
across every cache family, partial repair with one adjacent deferred entry,
pending/dead precedence, owned-plan release, and Shape backlink deferral.
Real object movement and automatic weak remembered-unit discovery are not
enabled or implied by this migration.

CloverVM migration working-tree verification on 2026-10-31, based on
`649dca5b`: all 92 focused layout/weak/collector tests passed. Full Debug
`all check` passed 2,291 tests and full Release `all check` passed 2,281 tests;
both had three existing JIT-disabled skips and one disabled test. Descriptor
size remains 56 bytes. The populated cache fixture exposes 15 independent
units and 46 active weak fields across all eight cache families; independent
per-target counts verify that Pending never short-circuits the remaining fields.

Working-tree verification on 2026-10-31, based on `72c64a3e`: all 33 focused
GC tests passed. Full Debug `all check` passed 2,285 tests and full Release
`all check` passed 2,275 tests; each configuration had three existing
JIT-disabled skips and one disabled test. The phase-misuse assertion test is
Debug-only. The generic header passed standalone C++20 compilation with Abseil
but no VM headers. Queue handles are statically checked to be trivially copyable
and 16 bytes on the 64-bit targets. Tests also exercise 512 deferred units
through destination-chunk growth. Native allocation exceptions are injected at
repair entry points; the standard deque allocator itself is not fault-injected.

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
