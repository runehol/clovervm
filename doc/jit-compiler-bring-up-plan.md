# JIT Compiler Bring-up Plan

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Proposed |
| Implementation | Core instruction storage, typed construction, graph publication, on-demand use lists, traversal and staged rewriting, compilation-session constant retention, code-cache publication, generic machine-code emission, AArch64 assembly, value-pool loads, and a direct single-register CFG-to-AArch64 path are implemented; heap `JitCodeObject` integration, backend assignment, bytecode translation, and compiler/runtime entry remain |
| Scope | Initial JIT staging, vertical slices, temporary runtime policies, and validation |
| Owning layers | The JIT owns compilation and generated transitions; the interpreter, managed calling convention, native boundaries, and reclaimer retain their existing contracts |
| Validated against | Supporting infrastructure, instruction-representation tests, CFG and rewrite tests, code-cache tests, and executable AArch64 tests in the working tree on 2026-07-23 |
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
- an initially exit-capable or throwing call also materializes constants
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
construction and rewriting, AArch64 emission, value-pool loading, executable
memory, and code-cache publication were intentionally implemented before
compiler entry because they are targetable, independently testable
infrastructure. The next three implementation fronts are partly independent;
the plan records their gates rather than pretending that all work must proceed
in one strict line.

### Landed foundation: storage, graph mechanics, and emission

The first implementation slice is already in the tree. It established:

- VM-owned code-cache allocation and publication, including pessimistic sizing,
  pool slices, W^X transition, commit-failure policy, and per-thread access;
- a target-independent `MachineCodeEmitter` with code fragments, labels,
  relocations, value-pool entries, and final layout;
- executable AArch64 assembler and macro-assembler support, including direct
  branches, value-pool loads, and immediate synthesis helpers;
- the naturally aligned 48-byte instruction record and instruction arena,
  schema-generated metadata, concrete typed instruction classes, typed
  terminator access, uniform operand/reference traversal, explicit constant
  defs, `ValueConstant`s, and positional Snapshot slots;
- CFG blocks, source-owned block edges, block parameters, graph construction,
  structural verification, generation-bound query caches, and on-demand
  `UseLists`;
- program-order read-only instruction traversal and graph-wide staged rewriting,
  including original or normalized callback input, operand reconstruction,
  prefix and suffix insertion, def replacement, detachment poisoning, and
  post-rewrite verification;
- `CompilationSession` ownership of the arena and a monotonic retained-value
  vector exposed through `retain_and_pin_value()`.

The current cache-retained C++ `JitCodeObject` remains bring-up scaffolding.
Turning it into a heap object with a native-layout scanner is part of the
remaining runtime publication work, not a reason to route around the existing
cache or emitter.

### Milestone 1: minimal Core instructions in CFG blocks

Connect typed instructions to blocks and make a hand-built Core CFG verifiable.
This is the smallest useful graph substrate for both the backend and bytecode
paths.

The initial graph-construction slice provides:

- session-owned compilation arenas containing `ControlFlowGraph` objects that
  do not retain a session or allocator pointer;
- a privileged `GraphBuilder` that takes the session, allocates one unpublished
  graph from its arena, makes or emplaces blocks, instructions, and parameters,
  makes source-owned block edges, derives successor predecessor lists during
  finalization, and returns the published arena-owned graph;
- separate ordinary vectors for a block's ordered parameter definitions and
  body instructions;
- final verification of block ownership, instruction placement, Core-kind
  legality, terminator placement, same-block definition-before-use, result
  classes, and value representations before publication.

This milestone is complete. `Const` appears in representative graphs, and the
compilation session owns a monotonic `std::vector<Owned<Value>>`. Graph building
calls `retain_and_pin_value()` for every pointer constant it encounters, while
compiler-created managed values use the same operation immediately after
allocation. The retain is also the compilation pin because Core instruction
payloads are not GC-scannable relocation slots. The session remains alive until
the heap `JitCodeObject` has been created and its initialized constant-pool
slots are visible to its native-layout scanner; that publication boundary
transfers responsibility from compilation retention to GC tracing.

Detached-storage poisoning, generation-bound `UseLists`, read-only traversal,
and staged published-graph rewriting also landed ahead of their original
milestones. Snapshot-expanded liveness, non-entry block parameters and their
edge argument lists, and general CFG-edge rewriting remain deferred. Entry
block parameters already represent function arguments.

### Milestone 2: executable AArch64 from minimal Core

Lower a hand-constructed, verified Core CFG all the way to executable AArch64
without decoded bytecode, interpreter entry, Snapshots, side exits, overflow
checks, recovery, or register allocation.

The first slice is complete. The emitter accepts one tagged entry `Parameter`
and walks one published block containing non-pointer `Const`, `AndSMI`,
`OrrSMI`, `EorSMI`, and `Return`. Focused tests execute the resulting AArch64.
The backend intentionally maps every `ProgramValueRef` to `x0`; this proves the
CFG-to-code path but cannot represent two simultaneously live values.

The first programs are:

```text
entry:
    %arg0 = Parameter
    Return %arg0

entry:
    %constant = Const ValueConstant(non-pointer)
    Return %constant
```

The initial identity sub-slice exposes two layers. `generate_aarch64_assembly`
deterministically lowers a published CFG into a caller-selected
`AArch64MacroAssembler`. `emit_aarch64_from_cfg` owns finalization and code-cache
publication: it first generates in `NearLiteral` mode and, only when finalization
returns `PoolOutOfRange`, discards that emission and regenerates from the CFG in
`FarPageRelative` mode. Allocation and publication failures remain typed
compilation failures and do not trigger the range retry.

For the single-parameter identity graph, the backend maps the tagged parameter
directly to the AArch64 argument/result register `x0`. This deliberately avoids
introducing a pretend allocation table before any instruction presents a
placement choice. A non-pointer `Const` uses the macro assembler's immediate
`mov` operation. The assembler and code cache can already emit and rewrite
value-pool slots, but a managed-pointer `Const` is not a complete published-code
feature until the heap `JitCodeObject` owns a native layout that scans those
slots.

If the full function-call ABI is not ready, the test may use a narrow generated
leaf harness that passes machine-level tagged `Value` arguments and reads the
returned tagged `Value`. The important point is that the input is a real Core
CFG and the output is executable machine code, not another standalone assembler
test. This private harness is not the interpreter main harness and does not need
the JIT-to-interpreter thunk.

Remaining scope:

- define the first real `AllocationConstraints` for the currently lowered
  instructions;
- replace the universal `x0` mapping with location assignments that handle at
  least two simultaneously live tagged values;
- lower `Mov` through the existing graph rewriter where normalization or
  assignment requires it;
- classify constants during backend preparation and emit immediate synthesis or
  traced value-pool loads as appropriate;
- preserve the already working near-pool retry, code-cache publication, and
  focused execution tests.

The output should include the smallest durable backend-preparation and
location-assignment products rather than another special-case mapping. A
single-block linear assignment or deliberately tiny allocator is sufficient;
general CFG liveness, branches, calls, Snapshots, side exits, and recovery
remain out of scope.

### Immediate implementation frontier

Three coherent slices can proceed from the landed foundation:

1. **Backend assignment.** Finish Milestone 2 by introducing real location
   constraints and assignments for a one-block function. This is the shortest
   route to executing expressions with multiple live values and exercises the
   graph rewriter for `Mov` insertion.
2. **Bytecode-to-Core construction.** Begin Milestone 3 as a structural path
   that produces and verifies Core plus Snapshots without executing it. This
   can advance independently of register allocation and establishes the actual
   compiler input.
3. **Heap `JitCodeObject` publication.** Replace the cache-retained C++ wrapper
   with a heap object, give it a native layout and an external-pool scanning
   hook, and keep the compilation session alive until those slots are
   scannable. This closes managed-constant lifetime and is a gate for real
   runtime installation, but it does not by itself make generated functions
   more expressive.

No option should introduce a second CFG, constant-pool, or emission path. The
backend and translator consume the existing published graph; heap publication
adapts the existing `CodeCache` result.

### Milestone 3: bytecode walking and symbolic interpreter state

Build decoded bytecode into Core by walking bytecode IR with a symbolic
interpreter state. This starts from the minimal Core shapes proven by
Milestones 1 and 2, but extends the scaffolding enough to construct exact
Snapshots. It still does not require runtime entry.

Scope:

- add a minimal decoded-bytecode walker and `BuilderContext`;
- model the symbolic accumulator, bytecode registers, current bytecode PC, and
  logical frame identity;
- lower function arguments as entry definitions;
- lower accumulator/register movement to symbolic bindings and `Mov` only when
  a real Core value is needed;
- lower tagged constants uniformly to `Const` and capture their
  `ProgramValueRef`s in Snapshots;
- build Snapshot instructions from the symbolic state at entry, return, and
  unsupported-bytecode boundaries;
- verify the produced Core graph against expected structure.

Generated side-exit code and runtime recovery are deferred, but this milestone
should already make Snapshot construction boring. That unlocks unsupported
exits, guards, call continuations, and later deoptimization without having to
invent Snapshot state at each feature.

### Milestone 4: interpreter entry with normal compiled return

Wire the interpreter/runtime to enter generated code and return normally for the
minimal return-parameter and return-constant functions. This is the first real
compiled execution path from Python, but it still has no side exits.

Scope:

- require heap `JitCodeObject` publication and GC-scannable value-pool slots
  before installing managed-pointer constants or persistent JIT entries;
- install and select a JIT entry for one compiled `CodeObject`;
- define the minimal generated-call ABI for arguments, return value, managed
  frame setup, and generated return target;
- implement the JIT-to-interpreter thunk used when an interpreted caller runs a
  compiled callee and the compiled callee returns to interpreter control;
- define the minimal managed/host transition record needed for interpreter to
  generated-code entry and normal return;
- ensure code lookup, entry invalidation, and compilation failure fall back to
  the interpreter without publishing partial code.

Validation must show:

- an interpreted call can select a JIT entry;
- the managed frame header and generated return target agree;
- arguments, return value, code object, and caller state are preserved;
- repeated entries restore the managed and host stacks exactly;
- destroying the compilation session after publication leaves every managed
  pool value alive through `JitCodeObject` tracing, with no ownership gap.

### Milestone 5: unsupported-bytecode side exit and Snapshot recovery

Add the first non-returning generated exit:

```text
install JIT entry
    -> enter generated code on the managed stack
    -> capture the function-entry Snapshot
    -> unconditional unsupported-bytecode exit
    -> run generated recovery and resume code
    -> switch to the host stack
    -> resume the interpreter at the first unsupported bytecode
```

This milestone introduces Snapshot plumbing, recovery, accumulator publication,
canonical frame synchronization, and interpreter handoff. Recovery code may
initially be emitted as one sequence; interning recovery plans is not part of
this milestone. It reuses the JIT-to-interpreter thunk introduced for normal
compiled return; the new work is recovering state before taking that handoff.

Scope:

- consume the Snapshots built by the bytecode walker;
- implement Snapshot-expanded point-use/liveness checks for the values needed
  by the taken exit;
- materialize Snapshot `ProgramValueRef`s into canonical frame homes and
  accumulator state, including rematerializable `Const` defs;
- install the resume bytecode PC and structural frame metadata expected by the
  interpreter;
- preserve the managed/host transition record on every exit path.

Validation must force recovery of ordinary and `Const`-produced Snapshot
references and show that reclamation can inspect the
suspended managed chain through the transition.

### Milestone 6: straight-line tagged bytecode state

Enable execution for simple straight-line tagged bytecodes after entry,
return, and unsupported side exits are working. Unsupported successors still
exit before execution.

This milestone establishes:

- load/store/move/constant bytecodes beyond direct return;
- logical accumulator/register bindings distinct from synchronized canonical
  homes;
- canonical-home tracking and non-trivial recovery assignments;
- accumulator publication and clearing on exits;
- forced unsupported-successor exits after partially compiled straight-line
  prefixes.

Each added opcode needs an interpreter-equivalence test for success and every
side-exit condition.

### Milestone 7: one guard and side-exit family

Add one checked operation family before attempting broad IC specialization.
The first candidate should be a narrow guard or SMI operation with one explicit
side exit and one Snapshot shape.

Scope:

- lower the operation to explicit Core checks and terminal action;
- force both success and failure paths;
- verify Snapshot availability, replay PC, accumulator action, and recovery;
- keep optimization disabled except for trivial local cleanup.

Shape guards, validity checks, attribute ICs, and redundant-check elimination
come after this first side-exit family is boring.

### Milestone 8: control flow and block parameters

Compile branches and joins using block parameters with parallel-copy edge
semantics. Validate loops, backedges, dominance, Snapshot availability, and
edge-move cycles. This milestone also establishes the CFG machinery needed by
later lowerings that introduce new branches or join points.

### Milestone 9: trusted no-safepoint leaf calls

Before arbitrary native or Python calls, add only calls certified not to
safepoint, allocate, enter Python, or raise. They use fixed calling-convention
locations and native ABI preservation but no root publication.

This gives the backend a call-shaped lowering without also taking on
reentrant stack transitions, pending-exception handoff, or Python call
continuations.

### Milestone 10: register allocation and physical recovery plans

Add real target register classes, liveness, register allocation, block-edge
moves, spills, and fixed-register constraints. Allocation consumes only
backend-prepared Core. Intern recovery plans only after allocation makes their
physical operations known. Keep resume-state selection separate so exits with
different bytecode PCs can share identical recovery code.

Validation must force recovery from registers, spills, canonical slots,
encodable inline constants, traced pool values, boxed F64 recovery actions, and
reified values.

### Milestone 11: native, Python, and mixed-mode calls

Implement reentrant stack-transition thunks and calls selected through the
existing managed calling convention. Distinguish:

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

### Milestone 12: broader ICs and conservative local optimization

Expand guards and ICs in small groups: shape guards, validity checks,
attribute operations distinguished by IC semantics, and shape-changing receiver
results. Enable optimizations only when accompanied by IR verification and
interpreter differential tests. The effect model, not pass order or intuition,
must authorize movement.

Initial optimization candidates are local and easy to invalidate:

- redundant dominating guards with identical Snapshots and replay states;
- trivial `Mov` forwarding where representation and Snapshot availability
  remain valid;
- local constant folding into explicit `Const` defs;
- dead code with no effects and no Snapshot-expanded point uses.

Mutable-shape motion, validity-check hoisting, and movement across calls wait
for concrete effect and alias analyses with generation-checked views.

### Milestone 13: profiling-guided expansion

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
- publication-lifetime tests that destroy the compilation session immediately
  after heap `JitCodeObject` creation and then trace or rewrite its pool;
- deterministic compiler dumps across repeated compilations.

Not every item applies to every milestone. A milestone tests only the
boundaries it enables, but must include the forced-failure or reclamation hooks
for any newly enabled boundary before moving on.

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

- native-layout scanner API for the heap `JitCodeObject`'s external `Value`
  pool, plus ownership of its code-cache allocation and its relationship to
  `CodeObject`;
- concrete backend-preparation artifact shape: lowering choices, legalized
  constant decisions, `AllocationConstraints` records, and invalidation
  generation;
- first location-assignment strategy for a one-block graph and the exact point
  at which required `Mov` instructions are inserted;
- decoded-bytecode input and `BuilderContext` shape for the first
  bytecode-to-Core translator;
- minimal generated-call ABI for Milestone 4 normal compiled returns, including
  argument/result registers, managed frame setup, generated return target, and
  the JIT-to-interpreter thunk;
- concrete managed/host transition-record layout and unwind behavior;
- initial register allocation strategy beyond the accepted global reservation
  of AArch64 `x16` as the backend's `IP0` scratch register;
- the exact straight-line opcode set for Milestone 6 and the first guard family
  for Milestone 7;
- code lookup, invalidation, compilation triggers, and failure policy beyond
  the accepted rule that code-cache allocation failure publishes nothing and
  continues in the interpreter;
- validation hooks for forced exits, safepoints, and nested transitions;
- criteria for replacing immortal generated code;
- measurements that justify precise stack maps or the optional Semantic IR.

## Related Documents

- [JIT Compiler and IR](jit-compiler-and-ir.md)
- [JIT Register Allocation](jit-register-allocation.md)
- [JIT Machine-Code Emission](jit-machine-code-emission.md)
- [JIT Code Cache and Publication](jit-code-cache.md)
- [Semantic IR and Specialization](jit-semantic-ir-and-specialization.md)
- [Function Calling Convention](function-calling-convention.md)
- [Native/Managed Boundary Contracts](native-managed-boundaries.md)
- [Decision Log](decision-log.md)
