# Young Weak Resolution and Repair

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Accepted |
| Implementation | Partial: resolver and proof-carrying weak heap; driver integration pending |
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

The remaining accepted slices are:

1. Add the young weak driver and prove its strong result equals the existing
   generational strong driver. Visit external and remembered weak units early,
   seed roots and strong remembered fields, visit owner units while draining,
   then complete deferred repair. The C++ collector permits this schedule.
2. Prove registration uniqueness, early/late repair correctness and retained
   weak-reference safety, preserving the existing full path and proof claims.
   Split the proof work into further reviewable commits as needed.
3. Extend the shared before/after heap format to v3 with required ordered
   `remembered_weak` unit IDs. These use existing unit IDs, not allocation IDs
   or a new numbering scheme. Preserve the list through diagnostic capture.
4. Bind the C++ runner's remembered units to actual old-owner storage and add
   directed fixtures, without changing the production collector templates.
5. Extend Lean conversion and capture. Unselected old movable owners must emit
   repaired weak state; only selected movable originals preserve stale input
   weak payloads. Add matching runner and process tests.
6. Extend Python generation and campaigns to young weak heaps and document the
   completed support. Keep remembered strong owners and weak units separate.

Both runners still reject young heaps containing weak storage. The weak heap's
coverage is established in the Lean representation; driver, format and runner
integration remain pending. Barriers, physical remembered-set retirement/reclamation,
repeated collections and mutation testing remain separate work.
