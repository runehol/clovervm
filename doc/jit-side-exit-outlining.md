# JIT Side-Exit Outlining

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | Not started |
| Scope | Separating transition-only computation and Snapshot state from the executable Core CFG immediately before register allocation |
| Owning layers | Core optimization owns sinking analysis while all instructions remain in the main CFG; Core-to-Machine lowering owns per-consumer `SideExit` objects; Machine use lists and register allocation own side-exit argument uses; allocation materialization owns point-correct argument renaming; transition emission owns deferred computation and interpreter-state publication |
| Validated against | N/A |
| Supersedes | N/A; if accepted, this direction requires corresponding changes to [JIT Compiler and IR](jit-compiler-and-ir.md), [JIT Register Allocation](jit-register-allocation.md), and [JIT Transition Programs](jit-transition-program.md) |

This proposal introduces a side-exit outlining boundary between Core
optimization and register allocation. Before that boundary, Snapshots and
instructions selected for transition-only sinking remain ordinary Core
instructions in the main graph. This preserves the existing SSA, use-list,
code-motion, CSE, DCE, and sinking analyses.

Immediately before register allocation, each executable side exit receives its
own parameterized `SideExit`. It contains captured Machine arguments, matching
deferred parameters, a clone of the sunk dependency DAG reachable by that exit,
and a final `ResumeInterpreter` terminator transformed from its Snapshot. The
lowered clones live only in the side exit; the original sunk instructions are
omitted from the executable Machine CFG, and the owning instruction names the
resulting object by `SideExitId`.

A `SideExit` is not a CFG block or edge. It has no successors, predecessors,
reachability, dominance, loop membership, or normal-path execution. It is an
attached parameterized instruction sequence whose logical position is exactly
the side-exit position of its owning instruction.

## Motivation

A Snapshot is currently represented as an ordinary SSA definition, but its
captured values are semantically required at each executable consumer, not at
the Snapshot instruction's stored position. This becomes problematic during
register allocation:

- liveness must keep each captured frontier value alive at the consumer;
- splitting may change the physical incarnation of a value between the stored
  Snapshot and its consumer;
- allocation materialization renames executable operands from an insertion
  point onward, but does not naturally reach backward into an earlier
  Snapshot;
- transition-only sunk instructions have SSA dependencies but no executable
  main-program position or register-allocated result.

Moving Snapshot instructions beside their consumers addresses only direct
captured values. It does not give a coherent position to a DAG of mutually
dependent sunk instructions, and one shared Snapshot may serve consumers at
different allocation positions.

Retaining a separate occurrence-to-location model through transition planning
could describe the physical frontier, but it would create a second
position-sensitive identity system beside the materialized Core graph. Side-exit
outlining instead makes every deferred sequence consumer-specific before the
position-sensitive allocation phases begin.

## Representation

The proposed Machine graph product contains executable CFG blocks and an
auxiliary table of side exits:

```cpp
struct SideExitIdTag;
using SideExitId = DenseId<SideExitIdTag>;

class SideExit
{
public:
    SideExitId id() const;
    InstructionRange parameters() const;
    std::span<const ProgramValueRef> arguments() const;
    InstructionRange instructions() const;

private:
    ControlFlowGraph *graph_;
    SideExitId id_;
    std::vector<InstructionId> parameter_ids_;
    std::vector<ProgramValueRef> arguments_;
    std::vector<InstructionId> instruction_ids_;
};
```

`ControlFlowGraph` owns the `SideExitId` namespace and the collection of
`SideExit` objects without adding them to its CFG block list.
`CompilationStorage` continues to own their instruction entries. A
side-exit-capable Machine instruction carries a
`SideExitId` attribute instead of the Core instruction's `SnapshotRef` operand.
The ID is structural metadata like a block-edge ID: it is not an SSA operand,
does not itself appear in use lists, and receives no allocation.

The `SideExit` arguments are different. They are logical source operands of the
owning Machine instruction even though they reside in the side-exit table rather
than its inline instruction payload. They use an explicit use-list category:

```cpp
struct SideExitArgumentUse
{
    SideExitId side_exit;
    size_t argument_index;
};
```

Machine use lists count these uses alongside ordinary instruction uses and
block-edge argument uses. Verification, liveness, and allocation materialization
resolve the owning instruction's `SideExitId` and handle its arguments at that
instruction's side-exit position. They do not make the arguments part of the
instruction's encoded operand range.

A side exit has these structural properties:

- it belongs to exactly one executable owner;
- `arguments().size() == parameters().size()`;
- argument and parameter representations match positionally;
- every argument names an executable Machine value available at the owner's
  side-exit position;
- its instructions are in dependency order;
- every body operand refers to a side-exit parameter or an earlier body
  instruction;
- its final instruction is a resultless `ResumeInterpreter` terminator;
- it has no branches, block edges, or ordinary CFG topology.

The parameter-to-argument relationship can be printed directly:

```text
check ... {side_exit = s0}

s0(%a = %4, %b = %7):
    %x = BoxF64 %a
    ResumeInterpreter %x, %b {resume_pc = 12}
```

Every side-exit argument is observed at the owner's Late liveness position. This
conservatively preserves recovery state through the selected operation and its
failure test. The operation's ordinary inputs may still be consumed Early.
Attaching the side exit to the exact Machine instruction that performs the
failure test keeps this timing meaningful after multi-instruction Core
lowering. The initial instruction set may have one side exit per owning
instruction; the representation should not silently assume that this is
sufficient for every future lowering.

## Optimization and Sinking

Before outlining, the main Core graph remains the analysis representation:

```text
ordinary Core computation
sinking candidates
Snapshot definitions
executable Snapshot consumers
```

The sinking analysis marks an instruction as transition-only only when:

- it has no executable main-path use;
- all transitive uses lead through other sunk instructions to Snapshots;
- moving its computation to every consuming side exit is semantically valid;
- its instruction kind is eligible for transition execution.

The sinking decision remains derived metadata while optimization continues.
The instruction is not immediately removed or rewritten into a second IR.
Consequently existing analyses continue to see ordinary SSA dependencies and
ordinary instruction effects.

## Core-to-Machine Instruction Transformation

Side-exit outlining is part of Core-to-Machine lowering. It consumes optimized
Core IR and produces an executable Machine CFG plus auxiliary Machine
`SideExit` objects. The optimized Core graph may remain intact as the lowering
input; sunk instructions are removed from the main program by omitting them
from the Machine CFG.

A Core instruction that can exit names recovery state through a `SnapshotRef`
operand:

```text
Core:
    %result = Operation %inputs..., %snapshot
```

Its Machine variant instead carries a `SideExitId` attribute:

```text
Machine:
    %result = SelectedOperation %inputs... {side_exit = s0}

    s0(%state0 = %machine_value0, ...):
        ...
        ResumeInterpreter %state0, ...
```

The Machine kind is distinct whenever this changes the instruction's schema.
One Core operation may lower to several Machine instructions, in which case the
`SideExitId` belongs to the exact selected instruction that can take the exit.
Several Core operations may also share one Machine kind when their selected
operation and side-exit behavior are identical.

Instructions whose shape is unchanged may remain shared between Core and
Machine. An instruction kind must not have one operand layout in Core and a
different layout in Machine: the kind must continue to determine the physical
schema unambiguously.

`SideExitId` is an attribute rather than an operand because it names auxiliary
structure, not a produced value. Resolving the ID exposes the attached
`SideExit::arguments()`, which are additional program-value sources of the
owning instruction.

The Core Snapshot itself is transformed rather than copied unchanged:

```text
Core:
    %snapshot = Snapshot %state_values... {resume_pc = pc}

Machine side exit:
    ResumeInterpreter %state_values... {resume_pc = pc}
```

Machine `ResumeInterpreter` has variadic state-value operands, the bytecode
resume PC as an attribute, no result, and terminating control-flow effects. It
is always the final instruction in its side exit. `SnapshotRef` and
`ResultClass::Snapshot` do not cross the Core-to-Machine boundary.

## Call-Boundary Snapshot Lowering

Not every Core Snapshot consumer represents a non-returning side exit.
`PythonCall` uses its Snapshot to make interpreter state canonical on the
successful path across a call boundary. It must not be lowered to a `SideExit`.

The promising Machine lowering is instead:

```text
%call_state = SynchronizeInterpreterState %state_values...
%result = PythonCall %callable, %call_state, %call_arguments...
```

`SynchronizeInterpreterState` is a zero-code Machine pseudo-instruction. Its
variadic operands follow `BytecodeStateOrder`, and allocation gives each one a
fixed constraint for its canonical interpreter-frame location. Register
allocation and allocation materialization then insert the parallel moves and
stores needed to establish that state.

The pseudo-instruction produces a compiler-only `CallState` token with no
physical allocation. `PythonCall` consumes the token so synchronization remains
ordered immediately before the call and cannot be removed merely because the
pseudo-instruction emits no machine code. `PythonCall` continues to carry its
`interpreter_return_pc`.

This lowering also draws a hard boundary around sinking: values required by
successful-path call synchronization must remain executable Machine
computation. They cannot remain deferred in a non-returning side exit.

The exact call-state design is not yet accepted. Before implementation it still
needs to settle:

- whether the accumulator participates in the synchronized state;
- how canonical-state publication interacts with outgoing call-argument setup;
- where the synchronized-state lifetime ends after the call.

## Outlining

The outlining pass runs after Core optimization and sinking decisions have
stabilized, as part of Core-to-Machine lowering immediately before
register-allocation preparation.

For each non-returning side-exit Snapshot consumer, it:

1. walks backward from the referenced Snapshot through instructions marked sunk;
2. identifies each distinct non-sunk dependency as a captured Machine argument;
3. creates one matching deferred parameter per captured argument;
4. clones the reachable sunk DAG in dependency order, referring only to those
   parameters and earlier cloned definitions;
5. transforms the Snapshot into the final `ResumeInterpreter` terminator;
6. assigns the new `SideExit` a `SideExitId`;
7. lowers the consumer to a Machine instruction carrying that ID as an
   attribute.

One logical sunk DAG may be shared by several Snapshots or consumers before
outlining. Each consumer receives its own clone because each side exit has
a different logical allocation position and may later receive different split
renamings.

The lowering does not emit the original Snapshot instructions or sunk
instructions into the executable Machine CFG. Omission is valid only after
proving that no unsunk Core instruction uses a sunk definition. The optimized
Core graph may remain intact as the source phase product; unsunk definitions
lower to Machine virtual values in the main graph.

The resulting boundary is:

```text
executable Machine CFG
    ordinary computation
    split and allocation candidates
    instructions carrying SideExitId

per-consumer SideExit objects
    captured arguments -> deferred parameters
    lowered deferred computation*
    ResumeInterpreter
```

## Register Allocation

The live-range scanner processes executable blocks in the usual program order.
When it encounters an instruction with a side exit, it scans the attached
arguments at that instruction's Late liveness position.

The scanner resolves the instruction's `SideExitId` and visits every
`SideExitArgumentUse`. Each argument creates an ordinary frontier use at the
owner's Late position. It does not scan the deferred body to rediscover its
frontier.

Side-exit parameters and body results receive no live range, bundle, register,
or spill slot. They exist only in the deferred sequence and are verified using
ordinary internal SSA ordering.

This makes allocation preserve precisely the main-program values needed to
enter the deferred computation. The same executable definition may have
different side-exit occurrences at different consumers and may therefore be
covered by different split bundles.

The allocator does not interpret interpreter-state positions, assign transition
scratch, or lower deferred computation. It only observes point uses of the
executable frontier.

## Split Materialization and Renaming

Allocation may insert a transfer before the owning instruction and rename a
main-program value from that point onward:

```text
%1 = Mov %0
check ... side_exit=s0
```

The attached side exit logically lives after that transfer. When
allocation materialization reaches the owning instruction, it rewrites the
captured arguments through the definition-remapping environment active at that
exact point:

```text
s0(%a = %1):
    %2 = BoxF64 %a
    ResumeInterpreter ..., %2
```

Captured arguments receive the active main-program identities. The deferred
body refers only to its parameters and internal definitions, so it requires no
point-specific rebuilding. Because each side exit has one owner, normalization
does not need to reconcile consumers on opposite sides of a split.

This is the same semantic operation as rebuilding ordinary instruction
operands through the graph rewriter. Source traversal exposes the arguments at
their owner, but the `SideExit` remains outside CFG topology.

After this step, ordinary post-materialization `LocationAssignments` are
sufficient for transition emission: every normalized argument names the
program value that physically exists at that point. No occurrence IDs or
allocator bundle identities need survive into the emitted transition program.

## Transition Emission

The backend emits an executable instruction and prepares its attached side
transition from the same normalized graph generation.

The side-transition emitter first binds each deferred parameter to the physical
location of its corresponding captured argument, then walks the side-exit body
in order:

- an eligible sunk Core instruction becomes the corresponding transition
  computation and writes transition scratch;
- a parameter operand reads the saved register-file or compiled-stack location
  obtained from its captured argument's `LocationAssignments`;
- an internal body operand refers to the scratch result of an earlier emitted
  instruction;
- the final `ResumeInterpreter` maps its ordered state values through
  `BytecodeStateOrder` to canonical interpreter destinations;
- interpreter-state publication is lowered as a parallel assignment into ordered
  `Transfer` instructions;
- emission ends with `ResumeInterpreter`.

The Machine terminator participates in the same dependency DAG as sunk
computation, but its lowering publishes logical state and transfers control
instead of producing a scratch result.

The emitted transition program retains no `SideExitId`, Snapshot,
`ProgramValueRef`, allocation occurrence, or Core instruction identity.

## Required Invariants

Verification at the outlining and allocation boundaries must establish:

- every side-exit consumer names one valid `SideExit`;
- every `SideExit` has exactly one owner;
- every `SideExit` has equal argument and parameter counts with compatible
  representations;
- every captured argument is available at the owner's side-exit position;
- every side-exit body is dependency ordered and ends in one
  `ResumeInterpreter` terminator;
- every body reference names a parameter or an earlier body instruction;
- every side-exit ID is structural metadata rather than an SSA use;
- every captured argument is exposed as one `SideExitArgumentUse` of the owner;
- every removed sunk instruction has no executable main-program use;
- every captured argument becomes a liveness use at the owner's Late position;
- no side-exit parameter or body result receives a main-program allocation;
- allocation materialization normalizes every captured argument using the
  remapping active at the owner without rebuilding the deferred body;
- transition emission handles every outlined instruction kind and interpreter
  state position.

## Proposed Implementation Slices

This proposal is not yet accepted. If adopted, the smallest implementation
sequence appears to be:

1. Add CFG-owned `SideExit` and `SideExitId` representation without changing
   existing Snapshot consumers.
2. Add the first Core-to-Machine lowering for non-returning side-exit consumers
   of direct Snapshots with no sunk computation, replace their Snapshot operands
   with side-exit-ID attributes, and terminate each side exit with
   `ResumeInterpreter`.
3. Expose captured side-exit arguments as `SideExitArgumentUse` records and
   teach liveness scanning to use them at the owning instruction's Late
   position.
4. Teach allocation materialization to rewrite captured arguments through the
   owner's active definition remapping.
5. Emit direct-Snapshot side exits as transition transfers and
   `ResumeInterpreter`.
6. Add sinking metadata, clone reachable sunk DAGs during outlining, and emit
   eligible transition computation.

Call-boundary Snapshot lowering is a separate adjacent design and is not part of
these slices. Before implementation, the exact graph-rewriter API used to
expose and normalize attached side-exit arguments requires a focused readiness
review.
