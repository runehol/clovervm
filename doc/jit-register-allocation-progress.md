# JIT Register Allocation Open Work

| Field | Value |
|---|---|
| Document type | Implementation ledger |
| Status | Active |
| Scope | Unfinished implementation and verification work for the accepted SSA bundle allocator |
| Design authority | [JIT Register Allocation](jit-register-allocation.md) |

The prepared problem, deterministic constraint splitting, cross-edge bundle
coalescing, conflict-free initial assignment, generic and edge-transfer-block
materialization, worklist-driven parallel assignment ordering including
all-stack cycles, `LocationAssignments`, and AArch64 integration are
implemented. This ledger records only unfinished work; the design document owns
algorithms, invariants, and layer boundaries.

## Correctness Checking

- [ ] Add a symbolic allocation checker covering assignments, occurrence
  requirements, interference, location transitions, and generated transfers.
- [ ] Extend it as splitting, edge transfers, fixed-location pressure, and
  Snapshot liveness land.

## Affinities and CFG Transfers

- [x] Merge non-overlapping bundles across block parameters and edge arguments.
- [ ] Add affinities for explicit copies and reused inputs.
- [x] Preserve source `LiveRangeId`s in merged fragments and make repeated
  coalescing requests no-ops.
- [x] Record unresolved edge transfers when overlap or constraints prevent
  coalescing.
- [x] Materialize edge transfers in dedicated CFG blocks, including critical
  edges.
- [ ] Materialize block-exit transfers.

## Backtracking, Splitting, and Spilling

- [x] Collect every conflicting bundle and the first conflict point while
  probing a register.
- [x] Implement strictly-higher-spill-weight eviction and requeue evicted
  bundles without changing their allocation priority.
- [x] Split copied bundle fragments without mutating immutable source live
  ranges; repartition fixed constraints and recompute child heuristics.
- [ ] Add remaining same-as-input and multi-location fixups.
- [x] Split register-only pressure ranges before the conflicting instruction
  and record their connectors in the transfer schedule.
- [ ] Record remaining fixup connectors in the transfer schedule.
- [ ] Provide allocator-owned spill slots for ordinary allocation pressure.
- [ ] Add per-value spill bundles and trim register-free regions around
  pressure splits into them, recording register-to-spill and spill-to-register
  connectors.
- [x] Add the reserved spill-weight tiers required by the allocator's progress
  argument.
- [ ] Add a debug iteration bound.
- [x] Detect irreducible pressure and fail compilation cleanly.

## Calls and Clobbers

- [ ] Support fixed-location call arguments and results without embedding ABI
  policy in the generic allocator.
- [ ] Validate early arguments that remain in registers clobbered at Late and
  reject contradictory fixed defs and clobbers.

Every register written by an instruction remains represented by either an
explicit result def or a clobber reservation, never both.

## Snapshots and Transition Programs

- [ ] Expand Snapshot captures into point uses at each consumer.
- [ ] Derive legal canonical homes from later Snapshot consumers.
- [ ] Reach through sunk transition values to the first unsunk inputs that must
  physically exist.
- [ ] Feed finalized register, canonical-home, and rematerialization locations
  into transition planning without allocating virtual Snapshot results.
- [ ] Cover shared Snapshots, multiple consumers, boxing, constants, and sunk
  transition closures in the symbolic checker.

## Robustness and Measurement

- [ ] Add generated prepared-CFG fuzzing and cross-check the SSA verifier,
  liveness, allocator, transfer resolver, and symbolic checker.
- [ ] Add adversarial tests for equal-weight conflicts, repeated eviction,
  fixed-location pressure, split progress, branches, joins, loops, and critical
  edges.
- [ ] Record allocation statistics and stable annotated dumps for probes,
  evictions, splits, spills, and transfers.
- [ ] Measure generated code and compilation time before tuning heuristics,
  storage, redundant-copy elimination, or rematerialization.

Alternative allocation algorithms, caller-context-sensitive pressure, and
Machine IR scheduling remain out of scope until measurements justify them.
