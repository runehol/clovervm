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

## Stage 1: Prepared Allocation Problem

- [ ] Define dense allocator-local IDs and storage for program points,
  occurrences, live ranges, bundles, and per-register allocation maps.
- [ ] Linearize block entry, instruction Early and Late points, and block exit
  without publishing positions into Core IR.
- [ ] Expand default and sparse instruction constraints into anchored use, def,
  temporary, and clobber occurrences.
- [ ] Compute precise same-block SSA liveness and live ranges from instruction
  operands for the initial one-block prepared graphs.
- [ ] Create singleton bundles and compute their requirements, allocation
  priorities, and spill weights.
- [ ] Add deterministic dumps and an internal verifier for positions,
  temporality, liveness, range ordering, non-overlap, and occurrence ownership.

This stage ends at a read-only prepared allocation problem. It performs no
physical assignment and cannot affect generated code. Cross-block liveness,
block parameters, and edge arguments enter in Stage 4 without changing the
allocator-local range representation.

## Stage 2: Conflict-Free Register Assignment

- [ ] Add the priority queue and process larger bundles first.
- [ ] Probe legal registers in `RegisterClassDefinition::allocation_order()`
  and record non-overlapping assignments in per-register maps.
- [ ] Produce the real `LocationAssignments` result and allocator move-bundle
  container, even though the initial move set is empty.
- [ ] Make AArch64 CFG emission consume `LocationAssignments` instead of its
  hardcoded `x0` mapping.
- [ ] Add a symbolic allocation checker covering assignments, occurrence
  requirements, interference, and generated move bundles.
- [ ] Execute one-block multi-value AArch64 tests through the existing code
  cache and near/far pool retry.

This stage accepts only allocation problems that fit without eviction,
splitting, fixups, or spills. A conflict that the later allocator could solve
is a recoverable compilation failure, not permission to introduce a temporary
allocation policy.

## Stage 3: Backtracking, Eviction, and Splitting

- [ ] Collect every conflicting bundle and the first conflict point while
  probing a candidate physical register.
- [ ] Implement strictly-higher-spill-weight eviction and requeue evicted
  bundles at their unchanged allocation priority.
- [ ] Split a non-minimal bundle at a useful first conflict and requeue both
  children with recomputed requirements, priorities, and spill weights.
- [ ] Add reserved spill-weight tiers for ordinary minimal and fixed-register
  minimal bundles.
- [ ] Detect irreducible excessive pressure and fail compilation cleanly.
- [ ] Add a debug iteration bound and adversarial tests for equal-weight
  conflicts, repeated eviction, fixed-register pressure, and split progress.

The progress proof in the initial algorithm is the exit criterion for this
stage. Merely passing ordinary examples is not enough.

## Stage 4: Affinities, CFG Transfers, and Moves

- [ ] Extend precise liveness and range construction across CFG edges, block
  parameters, and edge arguments.
- [ ] Merge non-overlapping bundles across block parameters and edge arguments.
- [ ] Add affinity merging for explicit moves and reused inputs.
- [ ] Record edge, split, and fixup moves in allocator move bundles.
- [ ] Normalize incompatible fixed-register and same-as-input occurrences into
  constrained ranges plus explicit fixups.
- [ ] Implement the unified parallel-move resolver, including cycles and the
  agreed scratch-location policy.
- [ ] Extend the symbolic checker and generated tests across branches, joins,
  loops, critical edges, duplicate edge arguments, and cyclic moves.

Block parameters and edge arguments retain their distinct SSA identities.
Bundle merging provides physical continuity without changing Core def/use
semantics.

## Stage 5: Temporaries, Clobbers, and Calls

- [ ] Normalize every clobbered register into an immovable Late fixed
  reservation in its physical-register allocation map.
- [ ] Allocate anonymous temporary ranges with their declared timing and
  register requirements.
- [ ] Support fixed entry, call-argument, call-result, and return occurrences
  without hardcoding ABI policy into the generic allocator.
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
