# Weak Repair Units

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Implemented |
| Scope | Full-selection early/late weak resolution, exact-unit scheduling and representative coordinated invalidation |
| Owning layers | Lean models the resolver/driver and a concrete unit callback; C++ layouts retain callback policy and storage-lifetime responsibilities |
| Validated against | Lean 4.35.0; C++ `Evacuation` and weak-unit protocol at `bb6c259e` (2026-11-01) |
| Supersedes | N/A; extends the strong evacuation proof |

Build with `lake build`. The implementation is in
[WeakRepair.lean](CloverGC/WeakRepair.lean), proofs in
[WeakRepairProofs.lean](CloverGC/WeakRepairProofs.lean), examples in
[WeakExamples.lean](CloverGC/WeakExamples.lean), and axiom guards in
[WeakAudit.lean](CloverGC/WeakAudit.lean).

## Objects, Units and Storage

Objects still use the strong model's logical IDs and canonical references.
Weak units have separate stable IDs (`Fin k`), standing for disjoint repair
storage in final/stable owners or external locations. They are not object roots.
`Input.ownerUnits` enumerates units for each logical owner; `external` supplies
units independently of owner traversal. Unit templates contain optional original
object IDs and an opaque payload. Null slots are ignored by the resolver.

`WellFormed` explicitly requires unique external IDs, unique IDs within each
owner, disjoint owners' units and disjoint external/owner enumeration. In the
model, updating one ID cannot change another. Actual address stability, native
ownership and non-overlapping callback storage remain adapter obligations.
The registration theorem covers the declared enumeration, not completeness of
CloverVM's real cache/root inventory.

The representative callback has optional weak fields and an opaque scalar
payload. It is not a proof of every cache-specific callback: mandatory guards,
active union arms, tagged sentinels, RC-owned plans and native destruction are
not modeled. In particular, null optional fields are harmless here, whereas a
specific IC may treat a null mandatory guard as grounds for invalidation.

## Early and Late Resolution

All original objects participate in this model's collection. An original
movable target resolves through forwarding. A NonMoving original resolves only
if marked; immovability does not imply liveness. Valid already-copied references
resolve unchanged. Otherwise early resolution is Pending and late resolution
is Dead. No uncollected-old shortcut is modeled yet.

Neither resolver marks, copies, scans, or schedules a strong object. The
`collectWeak_strong` theorem erases all weak work and obtains exactly the
previous strong collector result. This is stronger than a test that a particular
weak-only target stayed dead. Its scope is this opaque-payload model: real
callbacks may drop strong payload references already conservatively traced;
that ownership behavior is not claimed to leave real heap bytes unchanged.

`repairUnit` maps the field repair over every active weak field before combining
statuses. Resolved fields are rewritten immediately, Pending fields retain their
exact abstract references, and null fields remain null. A Dead field dominates
Pending and resets every field plus the payload. It does not merely clear the
dead pointer, and Pending cannot short-circuit checking later fields.

## Executable Schedule

The driver follows one legal C++ schedule:

1. Seed all strong roots and perform early visits for external units.
2. After each physical strong scan, enumerate that canonical owner's units in
   the post-scan state and repair them early.
3. Enqueue only units whose callback returns Pending, storing exact IDs rather
   than owner IDs or a request to rescan the owner.
4. After the strong driver completes, visit the saved deferred list with the
   late resolver, then empty the weak queue. No more roots or units are added.

The late helper uses list recursion over the frozen deferred list rather than
literal deque pops. The audit trace proves each deferred unit is visited in
order; late callbacks are proved Complete, so emptying the queue does not hide
unprocessed Pending work. Earlier settled units receive no late callback.
`registered` and `lateVisits` are audit histories, not inputs to scheduling or
new C++ metadata. Unit fields/payload updates do not consult the histories.

## The Proof Structure

`Progress` records monotonic marks and immutable installed forwarding entries.
We prove every strong discovery, repair and scan, and the complete drain,
preserves that relation. It establishes that a target resolved early remains
the same canonical target in the eventual strong result.

For unit proofs, `UnitInvariant` refers to that eventual result as a
specification; the executable driver does not read the future state. It says:

- Unregistered units still contain their input templates.
- A registered unit's current partial repair has the same eventual late repair
  as its original template.
- Registered units absent from the pending queue already equal their final
  repair; pending IDs are registered and duplicate-free.

The invariant starts trivially, is preserved by each fresh early visit, and is
carried through the integrated strong/weak drain. Distinct strongly scanned
owners plus well-formed enumeration imply each unit is registered once. Late
repair then makes every registered unit equal the direct final repair of its
original template, even when its early fields were only partially resolved.

## Final Results

| Theorem | Claim |
|---|---|
| `resolve_early_not_dead` / `resolve_late_not_pending` | The two phases cannot confuse Pending and Dead |
| `resolve_stable` | Early resolved targets remain valid and unchanged |
| `repair_late_complete` | Late unit repair cannot remain Pending |
| `repair_dead_dominates` | Any Dead field forces coordinated clearing, regardless of other Pending fields |
| `early_unit_compatible` / `early_complete_correct` | Partial writes preserve final meaning; early-settled units need no late work |
| `collectWeak_strong` | Weak processing cannot retain targets or alter strong traversal |
| `collection_registered_iff` | Exactly external units and units of strongly reachable owners are supplied |
| `collection_registered_nodup` | No supplied unit gets two early visits |
| `collection_late_exact` / `collection_late_nodup` | Late visits are exactly the deferred IDs, with no duplicates |
| `collection_units_correct` | Every registered unit equals its original template's final repair |
| `collection_live_unit` | If all targets survive, fields are canonical and the payload is preserved |
| `collection_dead_unit` | Any dead target clears the entire unit and payload, including otherwise-live fields |
| `collection_disposal_safe` | Every retained registered weak reference is canonical after collection |

Examples exercise a movable target copied into NonMoving storage, a live
NonMoving original initially Pending, a dead NonMoving original, weak-only
garbage and its strong child, adjacent settled/deferred units in the same owner,
external units, null slots and coordinated payload reset. An unreachable
owner's unit deliberately retains stale fields: it is not a surviving unit and
does not root its owner. A synthetic resolver checks Pending-before-Dead
precedence even though full-selection early resolution itself never returns Dead.

## Remaining Boundary

This verifies one concrete full-selection weak-unit protocol, not arbitrary
callback code. Stable unit storage and declared enumeration are assumptions;
allocation failure, RC/native cleanup and resurrection/finalizer behavior are
outside the model. Minor filtering, weak remembered-set coverage, runtime root
completeness and formal refinement of C++ to Lean remain separate work.
