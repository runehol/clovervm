# JIT Compiler Bring-up Plan

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Proposed |
| Implementation | Core instruction storage, typed instruction construction, preliminary CFG, code-cache allocation/publication, generic machine-code emitter, AArch64 assembler, value-pool loads, and executable AArch64 tests are implemented; Core graph publication, backend preparation, and compiler/runtime entry remain |
| Scope | Initial JIT staging, vertical slices, temporary runtime policies, and validation |
| Owning layers | The JIT owns compilation and generated transitions; the interpreter, managed calling convention, native boundaries, and reclaimer retain their existing contracts |
| Validated against | Supporting infrastructure, instruction-representation tests, CFG tests, code-cache tests, and executable AArch64 tests in the working tree on 2026-07-22 |
| Supersedes | Bring-up material formerly embedded in [JIT Compiler and IR](jit-compiler-and-ir.md) |

This plan brings up compiled execution through small, executable vertical
slices. The durable compiler and recovery architecture is defined by
[JIT Compiler and IR](jit-compiler-and-ir.md). This document owns temporary
policies, implementation order, validation, and criteria for replacing those
policies.

The first compiler lowers decoded bytecode and inline-cache snapshots directly
to Core IR. The optional Semantic IR frontend and its type inference are not on
the bring-up path.

## Initial Policies

These policies reduce the number of runtime subsystems that must change at
once. They are constraints on the initial implementation, not permanent JIT
invariants.

### Canonical frame publication

The interpreter, reclaimer, and initial recovery runtime consume canonical
frame storage. Compiled code may cache newer values in machine registers.
Before an operation that may reclaim memory, it publishes every live managed
root in the complete active logical frame chain and clears stale canonical
slots that could otherwise be mistaken for roots. Before a call that may throw
or otherwise leave JIT without returning through a generated recovery
continuation, it conservatively materializes complete canonical frame state
instead. A call consumes its input accumulator, so that dead value is not
published and its root slot is cleared; a normal result or exception
continuation supplies the next accumulator state.

The intended later call convention returns every outcome through generated JIT
code. Its continuation resumes normally, enters a compiled exception handler
when one covers the call, or performs exact Snapshot recovery only when the
exception or another outcome must leave JIT. Reaching that convention removes
eager full-frame materialization from normally returning calls without forcing
exceptions that can be handled in JIT back through the interpreter.

Before returning to the interpreter, generated exit code also installs the
required bytecode PCs and structural frame metadata. Consequently, the initial
runtime does not scan optimized registers or interpret compiled stack maps.

This implies:

- an untripped safepoint poll need not publish if reclamation cannot begin
  asynchronously;
- a tripped poll synchronizes and leaves compiled execution before reclamation;
- a safepoint-capable call publishes all live managed roots before entry;
- an initially exit-capable or throwing call also materializes inline values
  and boxes interpreter-visible F64 values before entry;
- a managed value live across reclamation has a canonical managed home;
- non-bytecode temporaries do not remain live across reclamation solely through
  compiler metadata.

Publication writes do not invalidate register caches. After a non-moving
reclamation, compiled execution may continue to use the same cached values when
the transition contract permits it.

### Separate managed and host stacks

Generated Python executes with the architectural stack pointer in the existing
Clover stack. The hand-written interpreter, runtime, extensions, and every C or
C++ target execute on the host stack. Every initial JIT-to-native transition
uses a stack-switching thunk, including calls that could eventually be
certified as native leaves.

A reentrant transition record preserves, logically:

```text
previous transition record
managed SP and FP
host SP
published managed frontier
continuation for the suspended side
```

Records nest across managed-to-native-to-managed re-entry. This keeps the
existing Clover stack scanner and reclaimer usable during JIT bring-up. The
exact record layout and unwind mechanics must be designed and tested before
native calls are enabled.

### Unsupported bytecodes exit before execution

Every unsupported bytecode is an unconditional side exit at its entry state:

```text
supported bytecode   -> execute compiled
unsupported bytecode -> recover its pre-bytecode state
                      -> interpreter executes it
```

This is the universal feature-staging mechanism. It applies to explicit raise
operations, `ReturnOrRaiseException`, and every other bytecode not yet lowered.
The JIT does not partly execute an unsupported operation.

A Python call itself may eventually be compiled without compiled exception
dispatch. The selected callee runs in generated code when its `CodeObject` has
a JIT entry and otherwise in the interpreter. If execution later reaches an
unsupported exception-dispatch bytecode, that bytecode exits before execution
and the interpreter performs the existing table lookup and unwind. The call is
not rolled back.

### Conservative optimization

The first optimizer preserves instruction order. It performs only
transformations with local, trivial effect proofs, such as eliminating an
identical dominating SMI check across pure arithmetic. Mutable-shape motion,
validity-check hoisting, and motion across calls wait for the effect and alias
model to be implemented and verified.

An unavailable analysis disables an optimization. It never weakens the proof
required by that transformation.

### Immortal generated code

Generated code is initially neither reclaimed nor relocated. Invalidation
prevents new entry but does not destroy machine code into which an active
callee may return. Active-frame-aware retirement and code-memory reclamation
are later lifecycle work.

## Milestone Sequence

Each milestone should land as a coherent, reviewable change with its own tests.
Later milestones may refine this ordering when implementation evidence exposes
a dependency, but they must preserve an executable fallback throughout. The
order below reflects the foundation already landed: instruction storage, CFG
scaffolding, AArch64 emission, value-pool loading, executable memory, and
code-cache publication were intentionally implemented before compiler entry
because they are targetable, independently testable infrastructure.

### Landed foundation: storage, emission, and code ownership

The first implementation slice is already in the tree. It established:

- VM-owned code-cache allocation and publication, including pessimistic sizing,
  pool slices, W^X transition, commit-failure policy, and per-thread access;
- a target-independent `MachineCodeEmitter` with code fragments, labels,
  relocations, value-pool entries, and final layout;
- executable AArch64 assembler and macro-assembler support, including direct
  branches, value-pool loads, and immediate synthesis helpers;
- the 48-byte instruction record, tagged-address instruction arena,
  schema-generated metadata, concrete typed instruction classes, typed
  terminator access, operand/reference traversal, inline constants,
  `ValueConstant`s, and heterogeneous Snapshot payloads;
- preliminary CFG blocks, block edges, and structural verification.

The remaining milestones should build on this substrate rather than route
around it with throwaway entry or emission paths.

### Milestone 1: publishable Core graph substrate

Finish the Core graph substrate before runtime entry. This milestone makes it
possible to construct, publish, verify, and locally mutate a Core graph without
generating code.

Scope:

- complete the bulk `GraphBuilder` and finalize path for one arena-backed CFG;
- enforce one CFG per arena-backed instruction allocation domain;
- complete placement checks for instruction kind, block ownership, terminator
  placement, block-edge ownership, and cross-graph references;
- finish managed-constant retention for `ValueConstant` attributes and Snapshot
  arrays;
- finish detached-storage poisoning and editor-owned replacement semantics;
- build the on-demand use traversal/index contract for `ProgramValueRef`,
  `InlineValueConstant`, and Snapshot-expanded point uses;
- verify `AnyRepresentation` remains Snapshot-only and every
  `ValueRepresentation` has its required `Mov` kind.

Validation should cover schema metadata, typed accessors, variadic and Snapshot
payload storage, graph verification failures, managed constant pinning, and
editor replacement of reference and inline-constant operands.

### Milestone 2A: executable AArch64 from minimal Core blocks

Once instructions are placed in verified CFG blocks, the next backend milestone
is not a general backend framework; it is one tiny Core program lowered all the
way to executable AArch64 code. This track does not require decoded bytecode
coverage or interpreter entry. It consumes a hand-constructed, verified Core CFG
and proves that backend preparation, emission, code-cache publication, and
execution agree.

The first programs should be deliberately small and must not require Snapshots,
side exits, overflow checks, or recovery:

```text
entry:
    %arg0 = Parameter
    Return %arg0

entry:
    %constant = SynthesizeImmediate InlineValueConstant
    Return %constant

entry:
    %constant = LoadConstantPoolValue ValueConstant
    Return %constant
```

If the function-call ABI is not ready, the test may use a narrower executable
test harness that calls a generated leaf with machine-level tagged `Value`
arguments and reads the returned tagged `Value`. The important point is that
the input is a real Core CFG and the output is executable machine code, not a
standalone assembler test.

Scope:

- define only the target-specific backend-preparation pieces needed for this
  tiny program, following the full phase contract in
  [JIT Compiler and IR](jit-compiler-and-ir.md);
- select the lowering family for `Parameter`, `SynthesizeImmediate`,
  `LoadConstantPoolValue`, and `Return`;
- assign initial argument and result locations by a deliberately fixed
  convention rather than a real allocator;
- legalize embedded `InlineValueConstant`s for the selected lowering forms;
- emit immediate synthesis and traced value-pool loads through the Core
  materialization instructions;
- assign each selected lowering a `LocationSummary`, including fixed registers,
  scratch requirements, call behavior, and encodable remaining constants;
- emit the prepared Core block through the existing AArch64 emitter and code
  cache;
- execute the generated code from a focused test and verify the returned tagged
  value.

These programs prove argument/result convention, immediate synthesis, traced
pool loads, Core-to-emitter lowering, executable publication, and execution.
Real liveness, register allocation, branches, calls, Snapshots, side exits, and
recovery are deliberately out of scope for this milestone.

### Milestone 2B: bytecode and Core-construction path

The other natural expansion path is toward decoded bytecode. This track does
not require executable entry; it can produce verified Core graphs and compare
their structure against bytecode semantics.

Scope:

- add a reusable decoded-bytecode frontend and `BuilderContext`;
- lower function arguments as entry definitions;
- lower accumulator/register loads and stores to `Mov` and logical bindings;
- lower bytecode constants to `InlineValueConstant`, `ValueConstant`,
  `SynthesizeImmediate`, or `LoadConstantPoolValue` according to their GC
  class;
- preserve universal unsupported-bytecode exits at the entry state for every
  unimplemented operation;
- build exact Snapshots for entry, unsupported exits, and simple bytecode
  transitions.

This track establishes logical slot bindings distinct from synchronized
canonical homes, Snapshot liveness as values move among accumulator and
register bindings, and `InlineValueConstant`/`ValueConstant` recovery from
Snapshots.

### Milestone 3: enter generated code and immediately leave

The first executable compiler supports no bytecodes:

```text
install JIT entry
    -> enter generated code on the managed stack
    -> capture the function-entry Snapshot
    -> unconditional unsupported-bytecode exit
    -> run generated recovery and resume code
    -> switch to the host stack
    -> resume the interpreter at the first bytecode
```

This milestone joins the backend/emitter and bytecode/Core-construction tracks.
It punches through code ownership, executable-memory entry, managed frame
setup, Snapshot plumbing, recovery, accumulator publication, stack switching,
and interpreter handoff before opcode semantics or register allocation obscure
boundary errors. It must use the real decoded Core graph, backend-preparation
artifact, emitter, value-pool, code-cache, and recovery path rather than a
throwaway generation path. Recovery code may initially be emitted as one
sequence; interning recovery plans is not part of this milestone.

Validation must show:

- an interpreted call can select a JIT entry;
- the native call/return target and managed frame header agree;
- the original frame, arguments, accumulator, code object, and bytecode PC are
  unchanged after the round trip;
- reclamation can inspect the suspended managed chain through the transition;
- repeated and reentrant entries restore both stacks exactly.

### Milestone 4: execute straight-line tagged bytecodes

Enable execution for the simple straight-line bytecodes whose Core construction
and backend lowering were already validated by Milestones 2A and 2B. Unsupported
successors still exit before execution.

This milestone establishes:

- real compiled execution for load/store/move/constant bytecodes;
- canonical-home tracking and non-trivial recovery assignments;
- accumulator publication and clearing on exits;
- interpreter equivalence for straight-line success paths;
- forced unsupported-successor exits after partially compiled straight-line
  prefixes.

Each added opcode needs an interpreter-equivalence test for success and every
side-exit condition.

### Milestone 5: guards, side exits, and first IC specializations

Snapshot IC contents into the compilation and lower selected monomorphic cases
to explicit checks and terminal actions. Add narrowed shape-check results, SMI
arithmetic with overflow exits, validity checks, and attribute operations
distinguished by their IC semantics.

Optimization remains conservative. Redundant-check elimination is enabled only
where dominance, receiver versioning, effects, and exit replay state make the
proof direct. Side exits remain explicit consumers of Snapshots, while
target-specific side-exit frame publication remains backend-owned.

### Milestone 6: control flow and block parameters

Compile branches and joins using block parameters with parallel-copy edge
semantics. Validate loops, backedges, dominance, Snapshot availability, and
edge-move cycles. This milestone also establishes the CFG machinery needed by
later lowerings that introduce new branches or join points.

### Milestone 7: native and mixed-mode calls

Implement reentrant stack-transition thunks and calls selected through the
existing managed calling convention. Distinguish:

- trusted calls certified not to safepoint or enter Python, which need native
  ABI preservation but no root publication;
- arbitrary native calls, which switch to the host stack and satisfy the
  boundary rooting contract;
- Python calls, which dynamically choose a generated or interpreted callee and
  publish the interpreter return PC as an instruction attribute;
- Python calls followed by `CheckNotImplemented`, which exit through the
  continuation Snapshot only when the result is `NotImplemented`;
- trusted native handlers that return `exception_marker()`, which hand off the
  already pending exception through recovery rather than replaying the call.

Publication on a successful call path is continuing fast-path code, not a
deduplicated non-returning side exit. Calls from inlined logical frames are
deferred until multi-frame activation and synchronization are implemented.

### Milestone 8: register allocation and physical recovery plans

Add real target register classes, liveness, register allocation, block-edge
moves, spills, and fixed-register constraints. Allocation consumes only
backend-prepared Core. Intern recovery plans only after allocation makes their
physical operations known. Keep resume-state selection separate so exits with
different bytecode PCs can share identical recovery code.

Validation must force recovery from registers, spills, canonical slots,
encodable inline constants, traced pool values, boxed F64 recovery actions, and
reified values.

### Milestone 9: conservative local optimization

Enable optimizations in small groups, accompanied by IR verification and
interpreter differential tests. The effect model, not pass order or intuition,
must authorize movement.

Initial candidates are local and easy to invalidate:

- redundant dominating guards with identical Snapshots and replay states;
- trivial `Mov` forwarding where representation and Snapshot availability
  remain valid;
- local constant folding into `InlineValueConstant` operands;
- dead code with no effects and no Snapshot-expanded point uses.

Mutable-shape motion, validity-check hoisting, and movement across calls wait
for concrete effect and alias analyses with generation-checked views.

### Milestone 10: profiling-guided expansion

Measure compilation latency, generated code size, side-exit rates, publication
traffic, native transition cost, register pressure, and recovery-code size.
Those measurements decide whether to add broader polymorphic Core lowering, a
Semantic IR optimization frontend, precise stack maps, generic deoptimization
translations, or backend-local Machine IR.

## Validation Strategy

Every milestone keeps interpreter execution as the reference and unsupported
bytecodes as a valid continuation. Validation should combine:

- structural IR verification after construction and every mutation pass;
- target-independent interpreter/JIT differential tests;
- forced failure of every emitted guard and overflow path;
- forced safepoint and reclamation at every enabled boundary;
- transition tests covering interpreted-to-compiled, compiled-to-interpreted,
  compiled-to-compiled, native re-entry, and nested re-entry;
- machine-code checks for frame layout, return targets, reserved registers, and
  fixed-register operations;
- deterministic compiler dumps across repeated compilations.

Tests should grow from actual contracts. Similar guard cases should be
table-driven or generated rather than accumulating one narrowly duplicated test
per opcode spelling.

## Later Runtime Migrations

The initial compiler materializes generated recovery sequences directly from
Core Snapshots, post-allocation locations, and canonical `HomeState`; identical
sequences may be interned as `RecoveryPlan`s. Canonical publication remains
authoritative for GC root discovery, so the initial compiler emits no general
root-map artifact. Later root-map work can proceed as a measured migration:

1. serialize shadow safepoint maps while canonical publication remains
   authoritative;
2. compare mapped roots against canonical scanning in debug builds;
3. allow selected compiled frames to use precise maps instead of continuing
   publication;
4. reconsider permanent canonical backing for inlined frames only after mapped
   frames work;
5. move to a mixed platform stack only after generated interpreter handlers and
   an exact mixed-stack walker exist.

A generic entry that saves registers and interprets recovery translations is an
independent possible replacement for generated recovery code, not an
intermediate required by precise root maps.

These are migration directions, not milestones required for a useful first
compiler.

## Bring-up Decisions Still Required

- exact Core graph publication API, editor transaction surface, and use-index
  lifetime rules for the first mutating passes;
- concrete backend-preparation artifact shape: lowering choices, legalized
  constant decisions, `LocationSummary` records, and invalidation generation;
- concrete managed/host transition-record layout and unwind behavior;
- initial register allocation strategy and reserved registers;
- the exact straight-line opcode set for Milestone 4 and the first IC
  specializations for Milestone 5;
- code lookup, invalidation, compilation triggers, and failure policy beyond
  the accepted rule that code-cache allocation failure publishes nothing and
  continues in the interpreter;
- validation hooks for forced exits, safepoints, and nested transitions;
- criteria for replacing immortal generated code;
- measurements that justify precise stack maps or the optional Semantic IR.

## Related Documents

- [JIT Compiler and IR](jit-compiler-and-ir.md)
- [JIT Machine-Code Emission](jit-machine-code-emission.md)
- [JIT Code Cache and Publication](jit-code-cache.md)
- [Semantic IR and Specialization](jit-semantic-ir-and-specialization.md)
- [Function Calling Convention](function-calling-convention.md)
- [Native/Managed Boundary Contracts](native-managed-boundaries.md)
- [Decision Log](decision-log.md)
