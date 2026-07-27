# JIT Register Allocation Open Work

| Field | Value |
|---|---|
| Document type | Implementation ledger |
| Status | Active |
| Scope | Unfinished implementation and verification work for the accepted SSA bundle allocator |
| Design authority | [JIT Register Allocation](jit-register-allocation.md) |

The prepared problem, deterministic constraint splitting, conflict-free initial
assignment, generic materialization, worklist-driven parallel assignment
ordering including all-stack cycles, `LocationAssignments`, and one-block
AArch64 integration are implemented. This ledger records only unfinished work;
the design document owns algorithms, invariants, and layer boundaries.

## Correctness Checking

- [ ] Add a symbolic allocation checker covering assignments, occurrence
  requirements, interference, location transitions, and generated transfers.
- [ ] Extend it as splitting, edge transfers, fixed-location pressure, and
  Snapshot liveness land.

## Affinities and CFG Transfers

- [ ] Merge non-overlapping bundles across block parameters and edge arguments.
- [ ] Add affinities for explicit copies and reused inputs.
- [ ] Preserve source `LiveRangeId`s in merged fragments and make repeated
  coalescing requests no-ops.
- [ ] Record unresolved edge transfers when overlap or constraints prevent
  coalescing.
- [ ] Materialize transfers at block exits and on edges, including critical
  edges and duplicate edge arguments.

## Backtracking, Splitting, and Spilling

- [ ] Collect every conflicting bundle and the first conflict point while
  probing a register.
- [ ] Implement strictly-higher-spill-weight eviction and requeue evicted
  bundles without changing their allocation priority.
- [ ] Split copied bundle fragments without mutating immutable source live
  ranges; repartition fixed constraints and recompute child heuristics.
- [ ] Add remaining same-as-input and multi-location fixups.
- [ ] Record pressure-split and fixup connectors in the transfer schedule.
- [ ] Provide allocator-owned spill slots for ordinary allocation pressure.
- [ ] Add the reserved spill-weight tiers and a debug iteration bound required
  by the allocator's progress argument.
- [ ] Detect irreducible pressure and fail compilation cleanly.

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
