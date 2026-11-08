# Young Weak Resolution and Repair

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Accepted |
| Implementation | Partial: model and repair correctness proved; retained-reference safety and runner integration pending |
| Scope | Extend the generational strong model with young weak repair |
| Owning layers | Lean owns the model and proofs; differential runners own format conversion and capture |
| Validated against | Lean 4.35.0 and `src/gc/evacuation.h::resolve_weak` |
| Supersedes | N/A |

Slice one implements `Generational.Weak.resolve`. Unselected sources resolve
unchanged before forwarding or marks are inspected. Selected sources delegate
to the existing full weak resolver. Allocated copies resolve unchanged; the
strong invariant supplies their validity and marks. The resolver returns only a
resolution, with no strong discovery, allocation or state update.

Checked results establish old-target preservation in young mode, exact full
resolver equivalence, no early Dead or late Pending, stability of early
decisions under strong progress, and retained-reference safety for successful
late resolution under the copy invariant and valid-reference preconditions.
Examples cover old movable and nonmoving objects without
marks, unresolved/forwarded young targets, mixed-age guards and weak payloads,
coordinated reset, and distinct immediate/null cells. Exact axiom guards are
imported by the default build. The full weak model is unchanged.

Slice two implements `RawWeakHeap`, `WeakWellFormed`, `WeakHeap` and a decidable
validator that returns the validity witness with the unchanged data. It reuses
the existing unit partition rules and requires every unit to be located at an
owner or externally. Remembered weak unit IDs are unique and belong to old
owners, including nonmoving owners. Every old-owner unit with a young target is
covered, including either guard or the payload and inactive entries.
Conservative remembered units are allowed. Coverage is independent of
collection mode and separate from strong remembered membership.

Derived results establish owner uniqueness, owner/external and
remembered/external separation, the age of a remembered unit's owner, and the
absence of young targets in unremembered old-owner units. Checked examples test
formal invalidity and validator rejection, complete location, each stored target
position, conservative/nonmoving cases, order preservation and empty heaps.
Unit IDs abstract disjoint repair storage; native address/lifetime correctness
remains an adapter obligation.

Slice three implements the driver with the existing weak state and repair
semantics. It registers external units followed by remembered weak units in
young mode, then seeds roots and strong remembered fields. Full mode omits
remembered weak registration. Each strong scan visits its owner's units after
repairing strong fields, and deferred repair runs after the drain. Erasing weak
state preserves both the entire strong result and scan trace, with an auxiliary
theorem for arbitrary fuel and state. Exact axiom guards and directed examples
are included. Registration and deferred-queue proofs follow in slice four.

Slice four proves exact registration order and membership: external units,
mode-selected remembered units, then units belonging to logically scanned
owners. Registration is unique. The old-owner witness separates remembered
units from young scanned owners; full mode omits remembered registration.
Deferred work is a subsequence of registration history, and late visits equal
that queue exactly, in order and without duplicates. Checked examples include
an unrooted old owner's remembered unit and full-mode owner reachability.
Exact axiom guards track the standard Lean axioms used by these proofs.

Slice five lifts selection-aware resolver stability through optional fields,
tagged cells and coordinated entries. Late unit repair always completes. Early
repair followed by late repair equals direct late repair, and an early Complete
result already equals its eventual late result, assuming strong-state progress.
These local results do not require a copy invariant or valid-reference witness;
they establish repair semantics, not retained-reference safety. Directed examples
cover mixed ages, partial early relocation, dead-guard reset, inactive entries,
nulls and immediates. Exact axiom guards require only `propext` for the three
main results. The driver and existing full-model proofs are unchanged.

Slice six introduces `desired`, direct late repair of an original template, and
a three-field `UnitInvariant` relative to a fixed eventual strong result.
Unregistered units retain their templates, registered units remain compatible
with direct late repair, and registered units outside the pending queue already
equal their desired values. Single and repeated fresh early visits preserve the
invariant under strong-state progress. Batches require the combined
registration list to be duplicate-free. Queue properties remain separate in
the existing scheduling proofs. Directed examples cover settled, pending and
unregistered units, with exact axiom guards for both preservation results.

Slice seven proves that generational discovery, field repair, scans and bounded
draining preserve existing marks and forwarding. Weak draining preserves the
semantic unit invariant relative to its eventual strong result; the final
registration list's uniqueness supplies fresh per-scan visits. Actual startup
and draining establish the invariant immediately before deferred repair,
relative to the independent `Generational.evacuate` result. This includes the
explicit weak visits before strong seeding. Examples cover forwarding/mark
preservation, zero fuel, remembered old storage, copied-owner units, unregistered
storage and full mode. Exact axiom guards cover all seven new results.

Slice eight proves final unit-value correctness. Every registered unit equals
direct late repair of its original template against the independent final
strong result. Exact lookup for unique late visits combines compatibility of
pending units with correctness of already-settled units. Unregistered units
remain unchanged, including inactive storage that direct repair would clear.
The strong queue is empty before late repair and the weak queue is empty after
collection. Directed examples cover partial early writes, live relocation,
dead-entry reset, early completion, unvisited inactive storage and full mode.
Exact axiom guards cover all seven results. Retained-reference safety is still
separate from these unit-value equalities.

The remaining accepted slices are:

1. Prove retained weak-reference safety, preserving the existing full path and proof claims.
   Split the proof work into further reviewable commits as needed.
2. Extend the shared before/after heap format to v3 with required ordered
   `remembered_weak` unit IDs. These use existing unit IDs, not allocation IDs
   or a new numbering scheme. Preserve the list through diagnostic capture.
3. Bind the C++ runner's remembered units to actual old-owner storage and add
   directed fixtures, without changing the production collector templates.
4. Extend Lean conversion and capture. Unselected old movable owners must emit
   repaired weak state; only selected movable originals preserve stale input
   weak payloads. Add matching runner and process tests.
5. Extend Python generation and campaigns to young weak heaps and document the
   completed support. Keep remembered strong owners and weak units separate.

Both runners still reject young heaps containing weak storage. The weak heap's
coverage is established in the Lean representation; final weak proofs, format
and runner integration remain pending. Barriers, physical remembered-set retirement/reclamation,
repeated collections and mutation testing remain separate work.
