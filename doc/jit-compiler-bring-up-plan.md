# JIT Compiler Bring-up Plan

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Proposed |
| Implementation | Not started |
| Scope | Initial JIT staging, vertical slices, temporary runtime policies, and validation |
| Owning layers | The JIT owns compilation and generated transitions; the interpreter, managed calling convention, native boundaries, and reclaimer retain their existing contracts |
| Validated against | N/A |
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

The interpreter, reclaimer, and initial deoptimization runtime consume only
canonical interpreter frame state. Compiled code may cache newer values in
machine registers, but before an operation that may reclaim memory it
synchronizes every dirty canonical home in the complete active logical frame
chain and publishes the accumulator through `ThreadState`.

Before returning to the interpreter, generated exit code also installs the
required bytecode PCs and structural frame metadata. Consequently, the initial
runtime does not scan optimized registers or interpret compiled stack maps.

This implies:

- an untripped safepoint poll need not publish if reclamation cannot begin
  asynchronously;
- a tripped poll synchronizes and leaves compiled execution before reclamation;
- a safepoint-capable call publishes all dirty active frames before entry;
- a value live across reclamation has a canonical managed home;
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
a dependency, but they must preserve an executable fallback throughout.

### Milestone 0: enter generated code and immediately leave

The first compiler supports no bytecodes:

```text
install JIT entry
    -> enter generated code on the managed stack
    -> capture the function-entry Snapshot
    -> unconditional unsupported-bytecode exit
    -> run generated recovery and resume code
    -> switch to the host stack
    -> resume the interpreter at the first bytecode
```

This punches through code ownership, executable-memory entry, managed frame
setup, Snapshot plumbing, recovery, accumulator publication, stack switching,
and interpreter handoff before opcode semantics or register allocation obscure
boundary errors. It includes the smallest real Core IR needed for one entry
block, one Snapshot, and one unconditional exit rather than a separate
throwaway code-generation path. The selector, recovery operations, and resume
operations may initially be emitted as one sequence; interning them is not part
of this milestone.

Validation must show:

- an interpreted call can select a JIT entry;
- the native call/return target and managed frame header agree;
- the original frame, arguments, accumulator, code object, and bytecode PC are
  unchanged after the round trip;
- reclamation can inspect the suspended managed chain through the transition;
- repeated and reentrant entries restore both stacks exactly.

### Milestone 1: decoded frontend and general Core construction

Generalize the milestone-zero block into the decoded-bytecode frontend, block
and edge model, `BuilderContext`, typed instruction and result identities, and
reusable Core construction. The compiler may still exit at the first bytecode,
but it can now describe the complete decoded block scaffold and construct exact
logical entry state without a function-specific emission path.

The backend may initially use deliberately poor fixed locations. Correct
Snapshot liveness, block arguments, edge transfers, and recovery state matter
before register-allocation quality. Machine IR is not required.

### Milestone 2: straight-line tagged state

Add constants and bytecode register/accumulator movement without gratuitous SSA
identities. Values remain in tagged `Value` representation, and unsupported
successors retain the universal side exit.

This milestone establishes:

- function arguments as entry definitions;
- logical slot bindings distinct from synchronized canonical homes;
- Snapshot liveness as values move among accumulator and register bindings;
- canonical-home tracking and non-trivial recovery assignments.

Each added opcode needs an interpreter-equivalence test for success and every
side-exit condition.

### Milestone 3: guards and inline-cache specializations

Snapshot IC contents into the compilation and lower selected monomorphic cases
to explicit checks and terminal actions. Add narrowed shape-check results, SMI
arithmetic with overflow exits, validity checks, attribute operations
distinguished by their IC semantics, and shape-changing receiver results.

Optimization remains conservative. Redundant-check elimination is enabled only
where dominance, receiver versioning, effects, and exit replay state make the
proof direct.

### Milestone 4: control flow and block parameters

Compile branches and joins using block parameters with parallel-copy edge
semantics. Validate loops, backedges, dominance, Snapshot availability, and
edge-move cycles. This milestone also establishes the CFG machinery needed by
later lowerings that introduce new branches.

### Milestone 5: native and mixed-mode calls

Implement reentrant stack-transition thunks and calls selected through the
existing managed calling convention. Distinguish:

- trusted calls certified not to safepoint or enter Python, which need native
  ABI preservation but no root publication;
- arbitrary native calls, which switch to the host stack and satisfy the
  boundary rooting contract;
- Python calls, which dynamically choose a generated or interpreted callee.

Publication on a successful call path is continuing fast-path code, not a
deduplicated non-returning side exit. Calls from inlined logical frames are
deferred until multi-frame activation and synchronization are implemented.

### Milestone 6: allocation and conservative local optimization

Add target location summaries, register classes, block-edge moves, spills, and
fixed-register constraints. Intern recovery plans only after allocation makes
their physical operations known. Keep resume-state selection separate so exits
with different bytecode PCs can share identical recovery code.

Enable optimizations in small groups, accompanied by IR verification and
interpreter differential tests. The effect model, not pass order or intuition,
must authorize movement.

### Milestone 7: profiling-guided expansion

Measure compilation latency, generated code size, side-exit rates, publication
traffic, native transition cost, and register pressure. Those measurements
decide whether to add broader polymorphic Core lowering, a Semantic IR
optimization frontend, precise stack maps, generic deoptimization
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

The initial compiler still constructs declarative post-allocation safepoint and
deoptimization state. That permits a measured migration without making the
collector depend on new metadata during bring-up:

1. serialize shadow safepoint maps while canonical publication remains
   authoritative;
2. compare mapped roots against canonical scanning in debug builds;
3. introduce a generic entry that saves registers and interprets recovery
   translations;
4. allow selected compiled frames to use precise maps instead of continuing
   publication;
5. reconsider permanent canonical backing for inlined frames only after mapped
   frames work;
6. move to a mixed platform stack only after generated interpreter handlers and
   an exact mixed-stack walker exist.

These are migration directions, not milestones required for a useful first
compiler.

## Bring-up Decisions Still Required

- concrete implementation of the accepted code-cache allocation, Tier-1 W^X
  transition, and instruction-cache synchronization design;
- concrete managed/host transition-record layout and unwind behavior;
- initial register allocation strategy and reserved registers;
- the first supported opcode set and the order in which control flow, ICs, and
  calls enter the compiler;
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
