# JIT Side-Exit Outlining

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | Not started |
| Scope | Attaching transition-only computation and Snapshot state to executable side-exit consumers immediately before register allocation |
| Owning layers | Core optimization owns sinking analysis while all instructions remain in the main CFG; side-exit outlining owns per-consumer `SideExit` records and the matching argument ranges on executable owners; ordinary instruction rewriting, use lists, and register allocation own those arguments thereafter; transition emission owns deferred computation and interpreter-state publication |
| Validated against | N/A |
| Supersedes | N/A; if accepted, this direction requires corresponding changes to [JIT Compiler and IR](jit-compiler-and-ir.md), [JIT Register Allocation](jit-register-allocation.md), and [JIT Transition Programs](jit-transition-program.md) |

This proposal introduces a side-exit outlining boundary between Core
optimization and register allocation. Before that boundary, Snapshots and
instructions selected for transition-only sinking remain ordinary Core
instructions in the main graph. This preserves the existing SSA, use-list,
code-motion, CSE, DCE, and sinking analyses.

Immediately before register allocation, each executable side exit receives an
attached `SideExit` record. It contains the original non-sunk input identities
named by the deferred computation and an ordered list of existing Core
instruction IDs ending in the original `Snapshot`. The sunk instructions and
Snapshot stay `CompilationStorage`-owned and valid, but they are removed from
executable block order. The owning Machine instruction names the resulting
record by `SideExitId` and carries a parallel range of executable argument
values.

A `SideExit` is not a CFG block or edge. It has no successors, predecessors,
reachability, dominance, loop membership, or normal-path execution. It is an
attached recovery record whose logical position is exactly the side-exit
position of its owning instruction. Its body is direct input to transition
program generation, not a second optimized or register-allocated IR region.

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
outlining instead gives each deferred sequence a lightweight argument binding:
immutable body inputs remain on the `SideExit`, while matching executable
arguments are ordinary operands of the owner. Sunk computation stays out of the
executable path and is emitted only by transition-program generation.

## Representation

The proposed graph product contains executable CFG blocks and an auxiliary
table of side-exit records:

```cpp
using SideExitId = DenseId<SideExit>;

class SideExit
{
public:
    std::span<const ProgramValueRef> inputs() const;
    std::span<const InstructionId> instructions() const;

private:
    std::vector<ProgramValueRef> inputs_;
    std::vector<InstructionId> instructions_;
};
```

`ControlFlowGraph` owns the `SideExitId` namespace and the collection of
`SideExit` objects without adding them to its CFG block list.
`CompilationStorage` continues to own the ordinary instruction entries named by
`instructions()`. A side-exit-capable lowered instruction carries a
`SideExitId` attribute instead of the Core instruction's `SnapshotRef` operand.
The ID is structural metadata: it is not an SSA operand and receives no
allocation.

The same Machine instruction exposes a `side_exit_arguments()` operand range.
That range is parallel to `SideExit::inputs()`: input `i` is the identity named
by the retained Core instructions, while argument `i` is the executable value
available at the owner. Initially they name the same definition. Ordinary graph
rewriting may later change the argument without modifying the shared retained
instructions.

Side-exit arguments are ordinary program-value operands. Existing use-list,
DCE, graph-rewriting, and allocation-materialization machinery therefore
handles them without a side-exit-specific use category or traversal path.
Liveness observes this named operand range at the owner's Late position.

The input and argument order has no interpreter-state meaning. It is only a
binding convention between one `SideExit` and its owner. Outlining may use
first occurrence in retained-instruction order, provided it builds both lists
in the same order.

A side exit has these structural properties:

- it is named by exactly one executable owner;
- its input count equals the owner's side-exit argument count;
- every input names a non-sunk value referenced by the retained instruction
  sequence;
- its instruction sequence contains non-owning `InstructionId` references to
  sunk instructions in original block order followed by one `Snapshot`;
- a sunk instruction may appear in several side exits;
- every retained operand refers to an earlier retained instruction or one of
  the side-exit inputs;
- every distinct operand not defined by an earlier retained instruction appears
  exactly once in `inputs()`, whether it is used by sunk computation or by the
  final Snapshot;
- the final Snapshot supplies the ordered interpreter state and resume PC;
- it has no branches, block edges, or ordinary CFG topology.

The side-exit input/argument binding can be printed directly:

```text
check ... side_exit_arguments(%4, %7) {side_exit = s0}

s0 inputs(%4, %7):
    %x = BoxF64 %4
    Snapshot %x, %7 {resume_pc = 12}
```

Every side-exit argument is observed at the owner's Late liveness position. This
conservatively preserves recovery state through the selected operation and its
failure test. The operation's ordinary inputs may still be consumed Early.
Attaching the side exit to the exact lowered instruction that performs the
failure test keeps this timing meaningful after multi-instruction Core lowering.
The initial instruction set may have one side exit per owning instruction; the
representation should not silently assume that this is sufficient for every
future lowering.

## Optimization and Sinking

Before outlining, the main Core graph remains the analysis representation:

```text
ordinary Core computation
sinking candidates
Snapshot definitions
executable Snapshot consumers
```

The sinking analysis marks an instruction as sunk only when:

- it has no executable main-path use;
- all transitive uses lead through other sunk instructions to Snapshots;
- moving its computation to every consuming side exit is semantically valid;
- its instruction kind is eligible for transition execution.

The sinking decision remains derived metadata while optimization continues. The
instruction is not immediately removed, poisoned, cloned, or rewritten into a
second IR. Consequently existing analyses continue to see ordinary SSA
dependencies and ordinary instruction effects.

## Lowering Boundary

Side-exit outlining is part of the lowering boundary before register
allocation. It consumes optimized Core IR and annotates the executable graph
with auxiliary `SideExit` records. The optimized Core graph may remain intact
as the lowering input; sunk instructions are removed from executable block order
but remain valid storage-owned instruction records referenced by side exits.

The Core-to-Machine outlining pass owns the graph-level transition once all
executable instructions are Machine-compatible. Retained sunk Core instructions
are storage-owned side-exit metadata rather than members of the Machine graph.

A Core instruction that can exit names recovery state through a `SnapshotRef`
operand:

```text
Core:
    %result = Operation %inputs..., %snapshot
```

Its lowered variant instead carries a `SideExitId` attribute:

```text
Lowered executable graph:
    %result = SelectedOperation %inputs...
        side_exit_arguments(%state0, ...) {side_exit = s0}

    s0 inputs(%state0, ...):
        ...
        Snapshot %state0, ...
```

If lowering expands one Core operation into several Machine instructions, the
exact instruction that can take the exit carries both the `SideExitId` and its
parallel side-exit argument range.

`SideExitId` is an attribute rather than an operand because it names auxiliary
structure, not a produced value. The matching side-exit arguments are a
separate named operand range because they are genuine program-value sources of
the owning instruction at its side-exit position.

The Core Snapshot becomes the final retained instruction rather than being
copied or rewritten at this boundary:

```text
Core:
    %snapshot = Snapshot %state_values... {resume_pc = pc}

Side-exit instructions:
    Snapshot %state_values... {resume_pc = pc}
```

The side-exit Snapshot supplies variadic state values and the bytecode resume
PC. Transition emission lowers that state into canonical publication transfers
and a transition-program `ResumeInterpreter`. `SnapshotRef` and
`ResultClass::Snapshot` do not enter executable allocation.

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
stabilized, immediately before register-allocation preparation.

For each non-returning side-exit Snapshot consumer, it:

1. walks backward from the referenced Snapshot through instructions marked sunk;
2. records every reachable sunk instruction ID in original block order and
   appends the Snapshot ID;
3. walks the complete retained sequence, not only the Snapshot, and identifies
   every distinct operand not defined by an earlier retained instruction;
4. records those identities as `SideExit::inputs()` and supplies the same
   ordered values as the lowered owner's side-exit arguments;
5. removes sunk instructions and the Snapshot from executable block order after
   proving that no unsunk executable instruction uses them;
6. assigns the new `SideExit` a `SideExitId`;
7. lowers the consumer to a Machine instruction carrying that ID and argument
   range instead of a `SnapshotRef` operand.

Input discovery must visit operands of every retained sunk instruction. If the
Snapshot captures a sunk `BoxF64` result, for example, the unboxed source of the
box is an input even though it does not appear directly in the Snapshot.

One logical sunk DAG may be shared by several Snapshots or consumers before
outlining. Each consumer records the instruction IDs it needs in its own
side-exit body. A sunk instruction may therefore appear in several side exits
without being cloned. If later implementation pressure requires per-exit
specialization, cloning remains an optimization, not the default
representation.

The lowering does not emit the original Snapshot instructions or sunk
instructions into executable block traversal. Omission is valid only after
proving that no unsunk executable instruction uses a sunk definition. The sunk
instruction records themselves remain alive because side exits still reference
them.

The resulting boundary is:

```text
executable graph
    ordinary computation
    split and allocation candidates
    instructions carrying SideExitId

per-consumer SideExit records
    immutable input identities
    sunk instruction IDs in original block order
    final Snapshot instruction ID
```

## Register Allocation

The live-range scanner processes executable blocks in the usual program order.
The owning Machine instruction exposes its side-exit arguments as an ordinary
operand range whose allocation timing is Late. The scanner processes that range
without traversing the `SideExit` or its retained instruction sequence.

Side-exit body results receive no live range, bundle, register, or spill slot.
They exist only in the deferred sequence and are mapped to transition scratch
while emitting the transition program.

Use-list construction needs no side-exit special case. The owner arguments are
ordinary executable operands and keep their definitions live. Retained sunk
definitions are outside executable traversal; their internal dependencies are
verified by the side-exit verifier instead.

This makes allocation preserve precisely the main-program values needed to
enter the deferred computation. The same executable definition may have
different side-exit occurrences at different consumers and may therefore be
covered by different split bundles.

The allocator does not interpret `SideExit::inputs()`, interpreter-state
positions, transition scratch, or deferred computation. It only observes the
owner's executable argument operands.

## Dead Code Elimination

Before outlining, ordinary DCE uses the main Core graph and sinking metadata.
Sunk candidates are still ordinary instructions until the outlining boundary,
so existing use-list and effect rules keep them live through Snapshots exactly
as they do before sinking is introduced.

After outlining, DCE also needs no side-exit-specific operand traversal. Every
frontier value is an ordinary operand of the executable owner, so the existing
marking algorithm keeps it live. The owner structurally names the `SideExitId`;
the CFG-owned record in turn retains the storage-owned sunk instructions and
Snapshot for transition emission. Those retained instructions do not re-enter
executable block order merely to satisfy DCE.

## Split Materialization and Renaming

Allocation may insert a transfer before the owning instruction and rename a
main-program value from that point onward:

```text
%1 = Mov %0
check ... side_exit_arguments(%1) {side_exit=s0}

s0 inputs(%0):
    %2 = BoxF64 %0
    Snapshot ..., %2
```

The retained instructions continue to name the immutable input identity `%0`.
The owner's parallel side-exit argument is an ordinary operand, so allocation
materialization rewrites it to `%1` through the existing graph-rewriter path.
No `SideExit` field is mutated and shared retained instructions remain valid for
other exits.

After this step, ordinary post-materialization `LocationAssignments` are
sufficient for transition emission: each owner argument names the program value
that physically exists at that point. No occurrence IDs or allocator bundle
identities need survive into the emitted transition program.

## Transition Emission

The backend emits an executable instruction and prepares its attached side
transition from the same normalized graph generation.

The side-transition emitter pairs `SideExit::inputs()[i]` with the physical
location of `owner.side_exit_arguments()[i]`, then walks the retained
instructions in order:

- an eligible sunk Core instruction becomes the corresponding transition
  computation and writes transition scratch;
- a retained operand matching a side-exit input reads the saved register-file or
  compiled-stack location supplied by the corresponding owner argument;
- an internal body operand refers to the scratch result of an earlier emitted
  instruction;
- the final Snapshot maps its ordered state values through `BytecodeStateOrder`
  to canonical interpreter destinations;
- interpreter-state publication is lowered as a parallel assignment into ordered
  `Transfer` instructions;
- emission ends with `ResumeInterpreter`.

The emitted transition program retains no `SideExitId`, Snapshot,
`ProgramValueRef`, allocation occurrence, or Core instruction identity.

## Required Invariants

Verification at the outlining and allocation boundaries must establish:

- every side-exit consumer names one valid `SideExit`;
- every `SideExit` has exactly one owner;
- every side-exit input has one corresponding owner argument;
- every owner argument is an ordinary program-value operand observed Late;
- every retained sequence is nonempty, contains sunk instructions in original
  block order, and ends in one Snapshot;
- every retained operand names a side-exit input or an earlier retained
  instruction;
- the inputs are exactly the distinct external operands of the complete
  retained sequence, including operands needed only by sunk computation;
- input and owner-argument ordering agrees, although that ordering has no
  semantic meaning;
- a sunk instruction referenced by several side exits remains storage-owned and
  valid;
- every side-exit ID is structural metadata rather than an SSA use;
- every removed sunk instruction has no executable main-program use;
- no side-exit body result receives a main-program allocation;
- ordinary graph rewriting and allocation materialization normalize owner
  arguments without rebuilding or mutating the side exit;
- transition emission handles every outlined instruction kind and interpreter
  state position.

## Proposed Implementation Slices

This proposal is not yet accepted. If adopted, the smallest implementation
sequence appears below. The prerequisite for a CFG to declare and verify one
current IR level is already implemented.

1. Add CFG-owned `SideExit` and `SideExitId` representation containing immutable
   input identities and retained instruction IDs, without changing existing
   Snapshot consumers.
2. Add Machine owner forms with `SideExitId` and a named side-exit argument
   range; make ordinary instruction traversal, use lists, DCE, and rewriting see
   those arguments.
3. Add the first outlining pass for non-returning side-exit consumers of direct
   Snapshots with no sunk computation, and change the CFG level from Core to
   Machine once all executable instructions are compatible.
4. Teach allocation constraints and liveness to observe side-exit arguments at
   the owner's Late position.
5. Emit direct-Snapshot side exits as transition transfers and
   `ResumeInterpreter`.
6. Add sinking metadata, record reachable sunk instruction IDs in original
   block order, discover inputs across the complete retained sequence, and emit
   eligible transition computation.

Call-boundary Snapshot lowering is a separate adjacent design and is not part of
these slices.
