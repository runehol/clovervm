# JIT Side-Exit Outlining

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | Not started |
| Scope | Attaching transition-only computation and Snapshot state to executable side-exit consumers immediately before register allocation |
| Owning layers | Core optimization owns sinking analysis while all instructions remain in the main CFG; side-exit outlining owns per-consumer `SideExit` records; use lists and register allocation own side-exit value uses at the owner position; allocation materialization owns point-correct side-exit use renaming; transition emission owns deferred computation and interpreter-state publication |
| Validated against | N/A |
| Supersedes | N/A; if accepted, this direction requires corresponding changes to [JIT Compiler and IR](jit-compiler-and-ir.md), [JIT Register Allocation](jit-register-allocation.md), and [JIT Transition Programs](jit-transition-program.md) |

This proposal introduces a side-exit outlining boundary between Core
optimization and register allocation. Before that boundary, Snapshots and
instructions selected for transition-only sinking remain ordinary Core
instructions in the main graph. This preserves the existing SSA, use-list,
code-motion, CSE, DCE, and sinking analyses.

Immediately before register allocation, each executable side exit receives an
attached `SideExit` record. It contains the non-sunk values that must be read at
the owner's side-exit position, an ordered list of original sunk instruction IDs
needed by that exit, and the original `Snapshot` instruction that describes the
interpreter state. The sunk instructions and Snapshot stay
`CompilationStorage`-owned and valid, but they are removed from executable block
order. The owning instruction names the resulting record by `SideExitId`.

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
outlining instead makes every deferred sequence consumer-specific enough for the
position-sensitive allocation phases: non-sunk inputs become explicit uses at
the owner, while sunk computation is kept out of the executable path and emitted
only by transition-program generation.

## Representation

The proposed graph product contains executable CFG blocks and an auxiliary
table of side-exit records:

```cpp
struct SideExitIdTag;
using SideExitId = DenseId<SideExitIdTag>;

class SideExit
{
public:
    SideExitId id() const;
    InstructionId owner() const;
    std::span<const ProgramValueRef> uses() const;
    std::span<const InstructionId> body() const;
    SnapshotRef snapshot() const;

private:
    ControlFlowGraph *graph_;
    SideExitId id_;
    InstructionId owner_;
    std::vector<ProgramValueRef> uses_;
    std::vector<InstructionId> body_;
    SnapshotRef snapshot_;
};
```

`ControlFlowGraph` owns the `SideExitId` namespace and the collection of
`SideExit` objects without adding them to its CFG block list.
`CompilationStorage` continues to own the ordinary instruction entries named by
`body()`. A side-exit-capable lowered instruction carries a `SideExitId`
attribute instead of the Core instruction's `SnapshotRef` operand. The ID is
structural metadata like a block-edge ID: it is not an SSA operand, does not
itself appear in use lists, and receives no allocation.

The `SideExit` uses are different. They are logical source operands observed at
the owning instruction's side-exit position even though they reside in the
side-exit table rather than in the instruction's inline operand payload. They
use an explicit use-list category:

```cpp
struct SideExitUse
{
    SideExitId side_exit;
    size_t use_index;
};
```

Use lists count these uses alongside ordinary instruction uses and block-edge
argument uses. Verification, liveness, and allocation materialization resolve
the owning instruction's `SideExitId` and handle its uses at that instruction's
side-exit position. They do not make the uses part of the instruction's encoded
operand range.

A side exit has these structural properties:

- it belongs to exactly one executable owner;
- every use names a non-sunk executable value available at the owner's
  side-exit position;
- its body contains non-owning `InstructionId` references to sunk instructions
  in original block order;
- a sunk instruction may appear in several side exits;
- every body operand refers to an earlier body instruction or one of the
  side-exit uses;
- every Snapshot operand refers to a body instruction or one of the side-exit
  uses;
- its Snapshot supplies the ordered interpreter state and resume PC;
- it has no branches, block edges, or ordinary CFG topology.

The side-exit use relationship can be printed directly:

```text
check ... {side_exit = s0}

s0(%4, %7):
    %x = BoxF64 %4
    Snapshot %x, %7 {resume_pc = 12}
```

Every side-exit use is observed at the owner's Late liveness position. This
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

The sinking analysis marks an instruction as transition-only only when:

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

A Core instruction that can exit names recovery state through a `SnapshotRef`
operand:

```text
Core:
    %result = Operation %inputs..., %snapshot
```

Its lowered variant instead carries a `SideExitId` attribute:

```text
Lowered executable graph:
    %result = SelectedOperation %inputs... {side_exit = s0}

    s0(%state0, ...):
        ...
        Snapshot %state0, ...
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
`SideExit::uses()`, which are additional program-value sources of the owning
instruction at its side-exit position.

The Core Snapshot itself moves into the side-exit record rather than being
copied or rewritten at this boundary:

```text
Core:
    %snapshot = Snapshot %state_values... {resume_pc = pc}

Side exit:
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
2. records every reachable sunk instruction ID in original block order;
3. identifies each distinct non-sunk dependency of the body or Snapshot as a
   side-exit use;
4. records the Snapshot as the source of interpreter state and resume PC;
5. removes sunk instructions from executable block order after proving no
   unsunk executable instruction uses them;
6. assigns the new `SideExit` a `SideExitId`;
7. lowers the consumer to an executable instruction carrying that ID as an
   attribute instead of a `SnapshotRef` operand.

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
    uses read at the owner exit position
    sunk instruction IDs in original block order
    Snapshot state
```

## Register Allocation

The live-range scanner processes executable blocks in the usual program order.
When it encounters an instruction with a side exit, it scans the attached
uses at that instruction's Late liveness position.

The scanner resolves the instruction's `SideExitId` and visits every
`SideExitUse`. Each use creates an ordinary frontier use at the owner's Late
position. It does not scan the side-exit body to rediscover its frontier.

Side-exit body results receive no live range, bundle, register, or spill slot.
They exist only in the deferred sequence and are mapped to transition scratch
while emitting the transition program.

Use-list construction after outlining must therefore distinguish executable
defs from side-exit-only defs. Executable defs can receive ordinary `Uses`
entries and side-exit use records. Sunk body defs and moved Snapshots do not
need executable `Uses` entries merely because a side-exit body references them;
their internal ordering is verified by the side-exit verifier instead.

This makes allocation preserve precisely the main-program values needed to
enter the deferred computation. The same executable definition may have
different side-exit occurrences at different consumers and may therefore be
covered by different split bundles.

The allocator does not interpret interpreter-state positions, assign transition
scratch, or lower deferred computation. It only observes point uses of the
executable frontier.

## Dead Code Elimination

Before outlining, ordinary DCE uses the main Core graph and sinking metadata.
Sunk candidates are still ordinary instructions until the outlining boundary,
so existing use-list and effect rules keep them live through Snapshots exactly
as they do before sinking is introduced.

After outlining, DCE must treat side exits as attached roots rather than
detached dead metadata:

- an executable instruction carrying a `SideExitId` keeps that side-exit record
  reachable;
- every side-exit use keeps its non-sunk executable definition live;
- every body instruction ID keeps that storage-owned sunk instruction valid;
- every body operand that names an earlier body instruction keeps that earlier
  sunk instruction in the same side-exit body;
- every Snapshot operand keeps either its side-exit use or its body instruction
  live.

Side-exit-only body instructions and moved Snapshots are not reinserted into
executable block order merely to satisfy DCE. They are live because the side
exit references them, and they become transition-program input rather than
main-path code. Conversely, if no executable owner references a side exit, the
side exit and its body references are unreachable and may be discarded together.

## Split Materialization and Renaming

Allocation may insert a transfer before the owning instruction and rename a
main-program value from that point onward:

```text
%1 = Mov %0
check ... side_exit=s0
```

The attached side exit logically lives after that transfer. When
allocation materialization reaches the owning instruction, it rewrites the
side-exit uses through the definition-remapping environment active at that exact
point:

```text
s0(%1):
    %2 = BoxF64 %1
    Snapshot ..., %2
```

Side-exit uses receive the active main-program identities. The body still names
the same sunk instruction IDs, but transition emission resolves their non-sunk
operands through the normalized side-exit use list. Because each side exit has
one owner, normalization does not need to reconcile consumers on opposite sides
of a split.

This is the same semantic operation as rebuilding ordinary instruction
operands through the graph rewriter. Source traversal exposes the side-exit
uses at their owner, but the `SideExit` remains outside CFG topology.

After this step, ordinary post-materialization `LocationAssignments` are
sufficient for transition emission: every normalized side-exit use names the
program value that physically exists at that point. No occurrence IDs or
allocator bundle identities need survive into the emitted transition program.

## Transition Emission

The backend emits an executable instruction and prepares its attached side
transition from the same normalized graph generation.

The side-transition emitter first maps every normalized side-exit use to its
physical location through `LocationAssignments`, then walks the side-exit body
in order:

- an eligible sunk Core instruction becomes the corresponding transition
  computation and writes transition scratch;
- a non-sunk operand reads the saved register-file or compiled-stack location
  obtained from that value's `LocationAssignments`;
- an internal body operand refers to the scratch result of an earlier emitted
  instruction;
- the Snapshot maps its ordered state values through `BytecodeStateOrder` to
  canonical interpreter destinations;
- interpreter-state publication is lowered as a parallel assignment into ordered
  `Transfer` instructions;
- emission ends with `ResumeInterpreter`.

The emitted transition program retains no `SideExitId`, Snapshot,
`ProgramValueRef`, allocation occurrence, or Core instruction identity.

## Required Invariants

Verification at the outlining and allocation boundaries must establish:

- every side-exit consumer names one valid `SideExit`;
- every `SideExit` has exactly one owner;
- every side-exit use is available at the owner's side-exit position;
- every side-exit body is in original block order;
- every body reference names a side-exit use or an earlier body instruction;
- every Snapshot reference names a side-exit use or a side-exit body
  instruction;
- a sunk instruction referenced by several side exits remains storage-owned and
  valid;
- every side-exit ID is structural metadata rather than an SSA use;
- every side-exit use is exposed as one `SideExitUse` of the owner;
- every removed sunk instruction has no executable main-program use;
- every side-exit use becomes a liveness use at the owner's Late position;
- no side-exit body result receives a main-program allocation;
- allocation materialization normalizes every side-exit use using the remapping
  active at the owner without rebuilding the side-exit body;
- transition emission handles every outlined instruction kind and interpreter
  state position.

## Proposed Implementation Slices

This proposal is not yet accepted. If adopted, the smallest implementation
sequence appears to be:

1. Add CFG-owned `SideExit` and `SideExitId` representation without changing
   existing Snapshot consumers.
2. Add the first outlining pass for non-returning side-exit consumers of direct
   Snapshots with no sunk computation, replace their Snapshot operands with
   side-exit-ID attributes, and keep Snapshot state on the side-exit record.
3. Expose side-exit uses as `SideExitUse` records and teach liveness
   scanning to use them at the owning instruction's Late position.
4. Teach allocation materialization to rewrite side-exit uses through the
   owner's active definition remapping.
5. Emit direct-Snapshot side exits as transition transfers and
   `ResumeInterpreter`.
6. Add sinking metadata, record reachable sunk instruction IDs in original block
   order during outlining, and emit eligible transition computation.

Call-boundary Snapshot lowering is a separate adjacent design and is not part of
these slices. Before implementation, the exact graph-rewriter API used to
expose and normalize attached side-exit uses requires a focused readiness
review.
