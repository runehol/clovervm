# Weak Repair Units

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Implemented |
| Scope | Full-selection early/late weak resolution, tagged weak fields and two-guard coordinated entries |
| Owning layers | Lean models the resolver/driver and a concrete unit callback; C++ layouts retain callback policy and storage-lifetime responsibilities |
| Validated against | Lean 4.35.0; C++ `Evacuation` and synthetic weak-entry source at `f8491b3b` (2026-11-07) |
| Supersedes | N/A; extends the strong evacuation proof |

Build with `lake build`. The implementation is in
[WeakRepair.lean](CloverGC/WeakRepair.lean), proofs in
[WeakRepairProofs.lean](CloverGC/WeakRepairProofs.lean), examples in
[WeakExamples.lean](CloverGC/WeakExamples.lean), and axiom guards in
[WeakAudit.lean](CloverGC/WeakAudit.lean).
Resolver/field/callback lemmas live in
[WeakResolutionProofs.lean](CloverGC/WeakResolutionProofs.lean); enumeration and
visit-history lemmas live in [WeakScheduling.lean](CloverGC/WeakScheduling.lean).
`WeakRepairProofs` keeps the unit invariant, its preservation and final results.

## Objects, Units and Storage

Objects still use the strong model's logical IDs and canonical references.
Weak units have separate stable IDs (`Fin k`), standing for disjoint repair
storage in final/stable owners or external locations. They are not object roots.
`Input.ownerUnits` enumerates units for each logical owner; `external` supplies
units independently of owner traversal. Input templates contain original IDs,
never preexisting copy indices. `RepairUnit α` has two variants: a standalone
`field` containing a tagged `Cell α`, or an `entry` containing an `Entry α`.
Using the same shape with `Fin n` for templates and `Reference n` for collection
state avoids separate representations that could disagree.

`WellFormed` explicitly requires unique external IDs, unique IDs within each
owner, disjoint owners' units and disjoint external/owner enumeration. In the
model, updating one ID cannot change another. Actual address stability, native
ownership and non-overlapping callback storage remain adapter obligations.
The registration theorem covers the declared enumeration, not completeness of
CloverVM's real cache/root inventory.

`Entry α` has exactly two nullable mandatory guards and a tagged payload.
`Cell α` is either `immediate Int` or `reference (Option α)`: null references,
immediate zero, and nonzero immediates remain distinct. `WeakGuard`, `WeakField`
and `WeakEntry` are aliases documenting roles, not different pointer/value bits.
Weakness comes from visiting these positions only through weak repair: neither
guards nor pointer payloads contribute strong graph edges. The integer payload
is a preservation abstraction, with no arithmetic or C++ integer conversion.

The entry follows `TestWeakEntry` in `tests/gc_test_heap.h`. Both guards must be
present to be active. Either missing guard resets both guards to null and the
payload to immediate zero, without consulting the resolver. Active entries
repair guard 0, guard 1, and a reference payload. Null reference payloads and
immediates need no resolution. All three reference-bearing positions share one
reset; a dead pointer payload invalidates the entry even if both guards live.
Standalone fields have no mandatory guards: nulls and immediates are preserved,
and a dead reference becomes null. C++ standalone `weak_cells` use the same
`TestCell` representation as entry payloads, including immediates and nulls.

This is not a proof of every production cache callback. Production cache guards
and resolved values are weak, but an expanded-call binding plan separately owns
strong keyword-name references. Those names are traced during strong closure;
reset can release them. The model deliberately does not represent that ownership,
RC/native destruction, other cache-specific union arms, or validity-cell policy.
The two-mandatory-guard reset rule is the synthetic entry contract, not a claim
that every production cache resets on null fields.

## Early and Late Resolution

All original objects participate in this model's collection. An original
movable target resolves through forwarding. A NonMoving original resolves only
if marked; immovability does not imply liveness. Valid already-copied references
resolve unchanged. Otherwise early resolution is Pending and late resolution
is Dead. No uncollected-old shortcut is modeled yet.

Neither resolver marks, copies, scans, or schedules a strong object. The
`collectWeak_strong` theorem erases all weak work and obtains exactly the
previous strong collector result. This is stronger than a test that a particular
weak-only target stayed dead. It remains exact for the richer tagged weak
payload because weak updates still change no strong field or collector state.
It does not claim that production resets releasing separately owned strong
plans leave real heap bytes or RC state unchanged. Logical weak-target liveness
is not a statement about the timing or effects of real finalization.

`repairUnit` repairs all positions of an active entry before `finishEntry`
combines their statuses. Resolved references are rewritten immediately; Pending
positions retain their exact references. A Dead result dominates Pending and
resets the whole entry. Pending cannot prevent a later guard or payload rewrite.
The resolver is pure in Lean: these definitions specify repair results, not an
observable effectful callback invocation trace or a C++ evaluation-order proof.

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
It belongs to the strong layer, in [CopyProgress.lean](CloverGC/CopyProgress.lean).
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
| `repair_dead_dominates` | A Dead reference forces clearing, regardless of other Pending positions |
| `repair_inactive` | Inactive entries reset for any resolver |
| `repair_early_immediate` / `repair_early_reference` | Active immediate payloads are preserved early; pointer payloads follow weak relocation, including null |
| `early_unit_compatible` / `early_complete_correct` | Partial writes preserve final meaning; early-settled units need no late work |
| `collectWeak_strong` | Weak processing cannot retain targets or alter strong traversal |
| `collection_registered_iff` | Exactly external units and units of strongly reachable owners are supplied |
| `collection_registered_nodup` | No supplied unit gets two early visits |
| `collection_late_exact` / `collection_late_nodup` | Late visits are exactly the deferred IDs, with no duplicates |
| `collection_units_correct` | Every registered unit equals its original template's final repair |
| `collection_live_unit` | Active units whose referenced guards/payload survive are mapped to canonical references; immediates are preserved |
| `collection_inactive_unit` | A supplied inactive entry resets regardless of its remaining references |
| `collection_dead_unit` | A dead guard/pointer payload resets an entry; a dead standalone reference becomes null |
| `collection_disposal_safe` | Every retained registered weak reference is canonical after collection |

The general `collection_units_correct` theorem retains its existing conclusion
and disjoint-enumeration premises: each registered unit equals a direct late
repair of its original template. Strong-state erasure, compatibility, exact
registration/deferral and completion likewise keep their conclusions. There
are no new premises on the general collection theorem that exclude inactive
or dead-payload entries.

Two old claims needed substantive changes. Unconditional early payload
preservation is false: active pointer payloads can relocate, and inactive entries
reset immediately. `repair_early_immediate` and `repair_early_reference` replace
it. The live-unit theorem now explicitly requires `active = true`; its all-live
premise covers payload references as well as guards, and its conclusion maps
those references rather than preserving an opaque payload. The inactive theorem
covers the complementary case. Disposal safety now includes every retained
reference payload through `RepairUnit.targets`.

Examples cover live/dead movable and NonMoving targets, ordinary and NonMoving
destinations, aliasing across all three entry positions, immediate and null
payloads, early and late payload repair, missing first/second/both guards,
adjacent settled/deferred units, unreachable owners, and forbidden duplicate or
overlapping enumeration. Synthetic resolver examples expose Dead-over-Pending
precedence and visible rewrites after a Pending guard, although the actual
full-selection resolver never returns Dead early or Pending late.

## Remaining Boundary

This verifies one concrete full-selection weak-unit protocol, not arbitrary
callback code. Stable unit storage and declared enumeration are assumptions;
allocation failure, RC/native cleanup and resurrection/finalizer behavior are
outside the model. Minor filtering, weak remembered-set coverage, runtime root
completeness and formal refinement of C++ to Lean remain separate work.
