# JIT Register Allocation Implementation Progress

| Field | Value |
|---|---|
| Document type | Implementation progress plan |
| Status | Active |
| Scope | Staged delivery and verification of the accepted SSA bundle allocator |
| Design authority | [JIT Register Allocation](jit-register-allocation.md) |

This checklist records finite implementation work. Algorithmic invariants,
ownership boundaries, and continuing bring-up constraints remain in the design
document and deliberately do not have checkboxes.

## Landed Prerequisites

- [x] Define target-independent register classes, physical registers, register
  sets, and target-owned allocation order.
- [x] Define default allocation constraints plus sparse per-instruction
  overrides for inputs, results, temporaries, and clobbers.
- [x] Produce initial AArch64 platform-ABI constraints for entry parameters,
  returns, and branch temporaries.
- [x] Expose schema-generated operand indices and generic operand-reference
  traversal.
- [x] Carry source loop depth as CFG block metadata for allocator heuristics.

## Stage 1: Prepared Allocation Problem

- [x] Define dense allocator-local IDs and storage for ephemeral allocation
  positions, occurrences, immutable live ranges, bundle fragments, and
  singleton bundles.
- [x] Linearize every block's entry, instruction Early and Late points, and
  exit without publishing positions into Core IR; represent all allocation
  ranges as inclusive-start, exclusive-end intervals.
- [x] Expand Core operands and results into anchored use and def occurrences,
  derive each range's one register class, retain sparse fixed constraints with
  their exact points, and reject incompatible class claims as compiler
  invariant failures.
- [x] Treat block parameters as entry definitions and edge arguments as
  predecessor-exit uses so precise allocation liveness and live ranges remain
  block-local while preparation covers the complete CFG.
- [x] Represent instruction temporaries as anonymous live ranges spanning
  Early through Late and retain Late clobbers as immovable physical-register
  reservations rather than bundles.
- [x] Give dead definitions minimal one-point ranges, including unused
  `Uninitialized` definitions.
- [x] Create one singleton bundle with one exact `{range, LiveRangeId}`
  fragment for every live range; keep chosen allocations in a later separate
  assignment table.
- [x] Compute bundle allocation priority and spill weight by visiting the
  source occurrences covered by each fragment.
- [x] Add deterministic dumps and an internal verifier for positions,
  temporality, liveness, half-open range ordering, fragment containment,
  non-overlap, occurrence coverage, sparse fixed constraints, and singleton
  bundle ownership.

This stage ends at a read-only prepared allocation problem. It performs no
physical assignment and cannot affect generated code. The prepared
representation is multi-block, but every bundle initially contains exactly one
live range fragment. Ordinary `AnyRegister` requirements are implicit and are
not stored per occurrence.

`Snapshot` results remain non-allocatable, and their captured values are not
uses at the Snapshot definition. Until consumer-position expansion lands in
Stage 6, this initial preparation rejects executable Snapshot consumers rather
than silently omitting recovery liveness.

## Stage 2: Conflict-Free Assignment and Materialization

- [x] Add the priority queue and process larger bundles first.
- [x] Probe legal registers in `RegisterClassDefinition::allocation_order()`
  and record non-overlapping assignments in per-register maps.
- [x] Define `AllocationLocation` and replace register-only requirements and
  fixed constraints with `AnyRegister`, `FixedLocation`, and `SameAsInput`.
- [x] Define dense `LivenessPosition`/`LivenessRange` occupancy geometry and
  compute the minimum coverage of Early/Late uses and defs, including
  whole-instruction Early defs and Late uses.
- [x] Add structural zero-width `TransferPoint`s separately from liveness
  geometry.
- [x] Add constraint-driven splitting immediately before the first
  incompatible use, or after an incompatible def, and record connectors in a
  structural `BundleTransferSchedule` grouped by
  `(TransferPoint, TransferPhase)`.
- [x] Produce `RegisterAllocationResult` with final bundles,
  `BundleLocationAssignments` facts, and `BundleTransferSchedule` value-flow
  actions.
- [ ] Add generic allocation materialization that resolves supported transfer
  sets in parallel, inserts Core transfer instructions, rewrites
  destination-bundle occurrences, and publishes `LocationAssignments` for the
  rewritten graph.
- [ ] Make AArch64 CFG emission consume the rewritten graph and its
  `LocationAssignments` instead of its hardcoded `x0` mapping.
- [ ] Add a symbolic allocation checker covering assignments, occurrence
  requirements, interference, location transitions, and generated transfers.
- [ ] Execute one-block multi-value AArch64 tests through the existing code
  cache and near/far pool retry.

This stage accepts only allocation problems that fit without eviction,
pressure-driven splitting, complex fixups, or allocator-owned spills. It does
perform the deterministic splitting required when one fragment cannot satisfy
its occurrence location requirements. A pressure conflict that the later
allocator could solve is a recoverable compilation failure, not permission to
introduce a temporary allocation policy. Initial executable integration
remains one-block even though the prepared-problem construction is not.

The initial assignment step produces a dense allocator-local
`BundleLocationAssignments` table. Per-register assigned fragments and clobber
ranges remain scratch indexes used only while probing candidates. The
allocator result remains valid only until generic materialization consumes its
bundle IDs, rewrites the CFG, and produces occurrence-oriented
`LocationAssignments` for recovery and machine emission.

## Stage 3: Affinities and CFG Transfers

- [ ] Merge non-overlapping bundles across block parameters and edge arguments.
- [ ] Add affinity merging for explicit moves and reused inputs.
- [ ] Preserve source `LiveRangeId`s in every merged fragment and make repeated
  requests to merge already-coalesced ranges no-ops.
- [ ] Record unresolved edge transfers when bundle constraints or overlap
  prevent coalescing.
- [ ] Extend the symbolic checker and generated tests across branches, joins,
  loops, critical edges, and duplicate edge arguments.

Block parameters and edge arguments retain their distinct SSA identities.
Bundle merging provides physical continuity without changing Core def/use
semantics. Affinity merging finishes before pressure-driven allocation
splitting begins; constraint normalization may already have partitioned
statically incompatible locations. After splitting, one source live-range ID
may occur in several bundles.

## Stage 4: Backtracking, Eviction, Splitting, and Fixups

- [ ] Collect every conflicting bundle and the first conflict point while
  probing a candidate physical register.
- [ ] Implement strictly-higher-spill-weight eviction and requeue evicted
  bundles at their unchanged allocation priority.
- [ ] Split a non-minimal bundle by partitioning its copied fragments without
  mutating the immutable source live ranges; partition sparse fixed constraints
  by point and recompute child priority and spill weight from covered
  occurrences.
- [ ] Normalize remaining same-as-input and multi-location occurrences into
  constrained fragments plus explicit fixups.
- [ ] Record edge, pressure-split, and fixup transfers in
  `BundleTransferSchedule`.
- [ ] Complete the unified parallel-transfer resolver, including cycles and the
  agreed scratch-location policy.
- [ ] Add reserved spill-weight tiers for ordinary minimal and fixed-location
  minimal bundles.
- [ ] Detect irreducible excessive pressure and fail compilation cleanly.
- [ ] Extend the symbolic checker for split connectors, fixed-location
  pressure, and preserved source-value provenance.
- [ ] Add a debug iteration bound and adversarial tests for equal-weight
  conflicts, repeated eviction, fixed-location pressure, and split progress.

The progress proof in the initial algorithm is the exit criterion for this
stage. Merely passing ordinary examples is not enough.

## Stage 5: Temporaries, Clobbers, and Calls

- [x] Make initial assignment enforce every prepared immovable Late clobber
  reservation, selecting another register or failing compilation when it
  cannot honor one.
- [ ] Make instruction lowering consume the locations assigned to anonymous
  temporary ranges.
- [ ] Support fixed-location call-argument and call-result occurrences without
  hardcoding ABI policy into the generic allocator.
- [ ] Validate calls whose early arguments remain legal in registers clobbered
  at Late, and reject contradictory fixed defs and clobbers.

Every physical register written by an instruction is represented by either an
explicit result def or a clobber reservation, never both.

## Stage 6: Snapshots, Canonical Homes, and Recovery

- [ ] Expand each Snapshot use at its consumer position into point uses of the
  captured values.
- [ ] Derive possible canonical homes from the closest later Snapshot consumer
  when recovery planning can legally use them.
- [ ] Expand allocation liveness through sunk values to the first unsunk
  recovery inputs that must physically exist.
- [ ] Feed finalized register, canonical-home, and rematerialization locations
  into recovery planning without assigning locations to virtual Snapshot
  results.
- [ ] Extend checker coverage to shared Snapshots, multiple consumers, F64
  boxing, constants, and sunk recovery closures.

Snapshot instructions remain virtual state dependencies. Their consumers,
rather than their placement alone, determine allocation demand.

## Stage 7: Quality and Robustness

- [ ] Add generated prepared-CFG fuzzing and cross-check the SSA verifier,
  liveness, allocator, move resolver, and symbolic allocation checker.
- [ ] Record allocation statistics and stable annotated dumps for priorities,
  spill weights, merges, probes, evictions, splits, and moves.
- [ ] Measure generated code before tuning register probe offsets, merge
  ordering, split placement, redundant-move elimination, or rematerialization.
- [ ] Measure compilation time and compact allocator-local storage only where
  profiles show it matters.

Detailed spill-cost tuning, profitable rematerialization,
caller-context-sensitive pressure, alternate allocation algorithms, and
Machine IR scheduling remain later work until code-quality measurements justify
them.
