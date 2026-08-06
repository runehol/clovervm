# JIT Register Allocation Open Work

| Field | Value |
|---|---|
| Document type | Implementation ledger |
| Status | Active |
| Scope | Unfinished implementation and verification work for the accepted SSA bundle allocator |
| Design authority | [JIT Register Allocation](jit-register-allocation.md) |

The prepared problem, forwarding definitions, deterministic constraint
splitting, cross-edge and non-overlapping same-as-input bundle coalescing,
conflict-free initial assignment, generic and edge-transfer-block
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
- [x] Form one block-local live range for operand 0 and each forwarding result,
  including refining guards, without a result affinity or result move.
- [ ] Add affinities and fixups for explicit copies and destructive reused
  inputs. Each destructive instruction must separately establish that
  overwriting the input is compatible with its side-exit recovery contract.
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
- [ ] Add remaining destructive reused-input and multi-location fixups.
- [x] Split register-only pressure ranges before the conflicting instruction
  and record their connectors in the transfer schedule.
- [ ] Record remaining fixup connectors in the transfer schedule.
- [ ] Add `FixedUse` input requirements and operand-local fixed-use fixups;
  keep `AnyLocation` independent from temporary-spill eligibility.
- [ ] Trim maximal spill-safe carrier intervals across trusted-handler calls
  with two ordinary bundle splits, respecting occurrence
  `minimum_coverage` and excluding observable program points.
- [ ] Give each carrier a final register probe, then assign abstract
  allocator-owned spill slots with deterministic best-effort reuse across
  non-overlapping carriers.
- [ ] Resolve abstract spill slots to managed-frame locations during
  materialization and report the required spill extent.
- [ ] Materialize ordinary authoritative transfers before fixed-use parallel
  copies, rewriting only the selected call operands.
- [ ] Generalize spilling beyond non-observable trusted-handler call carriers.
- [x] Add the reserved spill-weight tiers required by the allocator's progress
  argument.
- [ ] Add a debug iteration bound.
- [x] Detect irreducible pressure and fail compilation cleanly.

## Calls and Clobbers

- [x] Describe trusted-handler call arguments, results, and clobbers without
  embedding AArch64 ABI policy in the generic allocator.
- [ ] Implement fixed-use argument copies without pinning their source bundles
  to caller-clobbered ABI registers.
- [ ] Validate that the initial fixed-use targets are clobbered after their
  Early uses, and reject contradictory fixed defs and clobbers.

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
