# JIT Side-Exit Lowering

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Partial; Core-to-Machine owner lowering, side-exit regions and bindings, allocation-visible owner arguments, allocation materialization, IR printing, CFG verification, `ExitToInterpreter` publication emission, and deterministic AArch64 binding deduplication are implemented; general sinking analysis, sunk computation emission, target thunks, and interpreter handoff remain |
| Scope | Moving non-returning recovery state out of executable block order while preserving a normal operand surface for allocation and emission |
| Owning layers | Core optimization owns sinking analysis while instructions remain in the main CFG; side-exit lowering owns region construction and executable owner bindings; ordinary instruction rewriting, use lists, and register allocation own the owner argument operands; transition emission owns deferred computation and interpreter-state publication |
| Validated against | `tests/test_jit_side_exit_lowering.cpp`, `tests/test_jit_cfg.cpp`, `tests/test_jit_register_allocator.cpp`, `tests/test_transition_program_emitter.cpp`, and `tests/test_aarch64_cfg_emitter.cpp` |
| Supersedes | The direct-Snapshot backend model previously described by [JIT Compiler and IR](jit-compiler-and-ir.md) |

This design introduces a side-exit lowering boundary between Core optimization
and register allocation. Before that boundary, Snapshots and instructions
selected for sinking remain ordinary Core instructions in the main CFG. This
preserves the existing SSA, use-list, code-motion, CSE, DCE, and sinking
analyses.

Immediately before register allocation, each executable non-returning side
exit is lowered into two artifacts:

```text
SideExitRegion
    parameter_ids[]
    instruction_ids[]

SideExitBinding
    region
    arguments[]
```

A `SideExitRegion` is a closed, block-like recovery region. It owns
side-exit-local parameters and cloned body instructions. It has no CFG edges,
predecessors, successors, dominance, loop membership, or normal-path execution
position. Its final instruction is the Machine terminator `ExitToInterpreter`.

A `SideExitBinding` is one executable owner use of a region: the region ID plus
the owner's ordinary program-value arguments. The binding invariant is:

```text
owner.side_exit_arguments[i] binds to region.parameter_ids[i]
```

This keeps recovery state out of executable block order without retaining live
references from a side exit back into replaced mainline instructions. A live
instruction is placed in exactly one owner: a CFG block or a side-exit region.
Removed instructions may remain only as poisoned storage for diagnostics.

## Motivation

A Snapshot is represented as an ordinary SSA definition, but its captured values
are semantically required at each executable consumer, not at the Snapshot
instruction's stored position. This becomes problematic during register
allocation:

- liveness must keep each captured frontier value alive at the consumer;
- splitting may change the physical incarnation of a value between the stored
  Snapshot and its consumer;
- allocation materialization renames executable operands from an insertion
  point onward, but does not naturally reach backward into an earlier Snapshot;
- sunk instructions have SSA dependencies but no executable main-program
  position or register-allocated result.

Moving Snapshot instructions beside their consumers addresses only direct
captured values. It does not give a coherent position to a DAG of mutually
dependent sunk instructions, and one shared Snapshot may serve consumers at
different allocation positions.

The side-exit region/binding split gives each deferred sequence a lightweight
argument binding. The region owns the cloned deferred computation. The owner
exposes the current executable frontier as ordinary operands, so use lists,
DCE, graph rewriting, liveness, register allocation, and allocation
materialization can continue to use the main instruction machinery.

## Representation

`SideExitRegion` records two ordered lists:

- `parameter_ids()`, ordinary parameter instructions whose role is determined
  by placement in the region parameter list;
- `instruction_ids()`, cloned region-local instructions in dependency order,
  ending in `ExitToInterpreter`.

Region parameters use the same parameter instruction kind as block parameters.
The design does not introduce a separate side-exit parameter instruction unless
implementation later shows real resistance to the placement-based rule.

The region itself does not reference mainline instruction IDs. Region body
operands may refer only to region parameters or earlier region-local cloned
instructions. The final `ExitToInterpreter` carries the recovery payload:
captured values in `BytecodeStateOrder` and the bytecode resume PC. It is
Machine IR, terminates the side-exit region, and is distinct from executable
main-block `ResumeInInterpreter` instructions.

The executable Machine owner carries:

- a `side_exit_region` attribute naming the region;
- a `side_exit_arguments()` operand range parallel to
  `region.parameter_ids()`.

Side-exit arguments are ordinary program-value operands observed at the
owner's Late liveness position. This conservatively preserves recovery state
through the selected operation and its failure test. The operation's ordinary
inputs may still be consumed Early. Attaching the side exit to the exact
lowered instruction that performs the failure test keeps this timing meaningful
after multi-instruction Core lowering.

Example:

```text
check ... side_exit_arguments(%4, %7) {side_exit_region = r0}

r0 parameters(%p0, %p1):
    %x = BoxF64 %p0
    ExitToInterpreter %x, %p1 {resume_pc = 12}
```

The parameter and argument order has no interpreter-state meaning. It only
needs to be canonical and shared by construction, verification, and transition
emission. First external use in region instruction order and schema operand
order is sufficient.

## Optimization and Sinking

Before lowering, the main Core graph remains the analysis representation:

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

Side-exit lowering runs after Core optimization and sinking decisions have
stabilized, immediately before register-allocation preparation. It discovers
regions only from side-exiting instruction kinds that explicitly use a Snapshot
for non-returning recovery. It must not scan all Snapshot instructions
globally.

For each side-exiting operation using original Snapshot `S`, lowering uses a
rewrite-local map:

```text
original Snapshot InstructionId -> region package
```

The region package contains:

- the created `SideExitRegionId`;
- the ordered external argument environment;
- the side-exit parameter IDs in the same order;
- the cloned side-exit instruction IDs.

When a package for `S` does not yet exist, lowering constructs it from the
retained side-exit closure. It walks backward from the Snapshot through
instructions marked sunk, orders those instructions by their original main-block
instruction order, clones them into the region, and replaces the final Snapshot
with `ExitToInterpreter`.

While cloning, lowering discovers external mainline values in first-use order.
For each first-seen external value, lowering:

- creates one region parameter instruction with the required representation;
- records external value to region parameter in the clone map;
- appends the external value to the package argument environment.

When cloning an instruction operand:

- an external mainline value is rewritten to the matching region parameter;
- a retained local value is rewritten to the cloned retained instruction;
- any operand that cannot be classified this way is an invariant failure.

Every side-exiting owner that used `S` is then rewritten with the package's
region ID and exact ordered argument environment. Owners do not independently
rediscover or reorder arguments.

If lowering expands one Core operation into several Machine instructions, the
exact instruction that can take the exit carries both the `side_exit_region`
attribute and the parallel side-exit argument range.

`side_exit_region` is an attribute rather than an operand because it names
auxiliary structure, not a produced value. The matching side-exit arguments are
a named operand range because they are genuine program-value sources of the
owning instruction at its side-exit position.

## Snapshot Consumers

`Snapshot` remains generic recovery/resume metadata. Not every Snapshot becomes
a side exit.

Function-call snapshots have different semantics: they describe
interpreter-visible state for successful-path call synchronization and return
metadata. Side-exit lowering must not generate regions for these snapshots
unless they are explicitly referenced by a non-returning side-exiting
instruction kind.

Call-boundary lowering is deferred. If call state needs a different shape, it
may get a separate call-environment instruction rather than overloading
`Snapshot` or side-exit regions.

The rule is:

- side-exiting operations create `SideExitRegion`/`SideExitBinding` records;
- call-boundary snapshots remain call metadata;
- a standalone Snapshot in the graph is not itself a side exit.

## Graph Ownership

`ControlFlowGraph` does not keep a side-exit registry for semantic
reachability. Side exits are discovered by walking executable instructions,
just as block edges are discovered from branch terminators.

The storage layer owns side-exit region records and provides dense
`SideExitRegionId` lookup. Reachability is derived from owner instruction
references:

- executable CFG blocks own normal instruction order;
- owner instructions reference side-exit regions and carry binding arguments;
- storage owns allocated side-exit regions;
- an unreferenced region is dead metadata.

There is no third live placement state for detached instructions. The graph
rewriter may poison replaced instructions after their executable uses have been
rewritten, but later passes must not rely on a detached instruction as a valid
recovery value, retained side-exit body, or lowering input. If an instruction is
still semantically needed outside the main CFG, the owning pass must clone or
move it into an explicit non-CFG owner such as `SideExitRegion`.

Dead regions are harmless storage garbage. Passes that need live side exits
discover them from executable owner instructions rather than requiring region
compaction or a graph-owned live registry.

## Register Allocation

The live-range scanner processes executable blocks in the usual program order.
The owning Machine instruction exposes its side-exit arguments as an ordinary
operand range whose allocation timing is Late. The scanner processes that range
without traversing the `SideExitRegion` or its cloned instruction sequence.

Side-exit body results receive no live range, bundle, register, or spill slot.
They exist only in the deferred sequence and are mapped to transition scratch
while emitting the transition program.

Use-list construction needs no side-exit-specific operand traversal. The owner
arguments are ordinary executable operands and keep their definitions live.
Region-local definitions are outside executable traversal; their internal
dependencies are verified by the CFG verifier when reachable from executable
owner instructions.

This makes allocation preserve precisely the main-program values needed to
enter the deferred computation. The same executable definition may have
different side-exit occurrences at different consumers and may therefore be
covered by different split bundles.

The allocator does not interpret interpreter-state positions, transition
scratch, or deferred computation. It only observes the owner's executable
argument operands.

## Dead Code Elimination

Before lowering, ordinary DCE uses the main Core graph and sinking metadata.
Sunk candidates are still ordinary instructions until the lowering boundary, so
existing use-list and effect rules keep them live through Snapshots exactly as
they do before sinking is introduced.

After lowering, DCE also needs no side-exit-specific operand traversal. Every
frontier value is an ordinary operand of the executable owner, so the existing
marking algorithm keeps it live. The owner structurally names the
`SideExitRegionId`; the region in turn retains the storage-owned cloned
instructions and `ExitToInterpreter` terminator. Those region instructions do
not re-enter executable block order merely to satisfy DCE.

Dead regions can remain as harmless storage garbage. A cleanup pass may compact
them later if allocation pressure justifies it, but reachability should be
computed from executable owner instructions.

## Split Materialization and Renaming

Allocation may insert a transfer before the owning instruction and rename a
main-program value from that point onward:

```text
%1 = Mov %0
check ... side_exit_arguments(%1) {side_exit_region = r0}

r0 parameters(%p0):
    %2 = BoxF64 %p0
    ExitToInterpreter ..., %2 {resume_pc = 12}
```

The region continues to name parameter `%p0`. The owner's parallel side-exit
argument is an ordinary operand, so allocation materialization rewrites it to
`%1` through the existing graph-rewriter path. No region field is mutated.

After this step, ordinary post-materialization `LocationAssignments` are
sufficient for transition emission: each owner argument names the program value
that physically exists at that point. No occurrence IDs or allocator bundle
identities need survive into the emitted transition program.

## Transition Emission and Late Deduplication

The backend emits an executable instruction and prepares its attached side
transition from the same normalized graph generation. The transition emitter
takes a `SideExitBinding`:

```text
SideExitBinding
    region
    arguments[]
```

It resolves each `arguments[i]` through `LocationAssignments` and binds that
physical source to `region.parameter_ids[i]`. It then walks the region
instructions in order:

- an eligible region computation becomes the corresponding transition
  computation and writes transition scratch;
- a region parameter reads the saved register-file, compiled-stack location, or
  canonical slot supplied by the corresponding owner argument;
- an internal body operand refers to the scratch result of an earlier emitted
  region instruction;
- the final `ExitToInterpreter` maps its ordered captured values through
  `BytecodeStateOrder` to canonical interpreter destinations;
- interpreter-state publication is lowered as a parallel assignment into
  ordered `Transfer` instructions;
- emission ends with transition-program `ResumeInterpreter`.

For one emission run, `SideExitBinding` identifies the side-exit code because
physical locations are resolved from the final location assignment. The AArch64
CFG emitter deduplicates by binding while retaining deterministic output order:
it keeps a vector of pending side exits in first-use order and a hash map from
binding to vector index. Emission iterates the vector, never the hash map.

Equivalent bindings share one published transition program and one cold
side-exit block. Non-equivalent bindings remain separate even when they refer
to the same `SideExitRegion`.

The emitted transition program retains no `SideExitRegionId`, `ProgramValueRef`,
allocation occurrence, or compiler instruction identity.

## Required Invariants

Verification at the lowering and allocation boundaries must establish:

- every live side-exit owner names one valid `SideExitRegion`;
- every owner side-exit argument has one corresponding region parameter;
- every owner argument is an ordinary program-value operand observed Late;
- every owner argument satisfies the representation required by the matching
  region parameter;
- region parameters use ordinary parameter instructions; placement in the
  region parameter list, not instruction kind alone, defines their role;
- every region instruction operand refers to a region parameter or an earlier
  region instruction;
- a region instruction must not reference a mainline instruction directly;
- a live instruction is placed in exactly one owner: a CFG block or a side-exit
  region;
- detached-but-live instructions are invalid; removed instructions may only be
  observed as poisoned diagnostic storage;
- the final region instruction is `ExitToInterpreter`;
- `ExitToInterpreter` before the final region instruction is invalid;
- side-exit regions have no CFG edges, block predecessors, block successors, or
  normal-path execution position;
- side-exit regions are created only for side-exiting instruction kinds, not
  for all Snapshot consumers;
- the snapshot-to-region package map is shared across one lowering rewrite so
  every owner of the same original Snapshot receives the same region and
  argument environment;
- late transition emission consumes the same `SideExitBinding` structure that
  late deduplication keys on: region ID plus argument program-value refs;
- transition emission handles every lowered instruction kind and interpreter
  state position.

## Implementation Progress

The structural implementation slices are complete:

- storage-owned `SideExitRegion` records own parameter IDs and cloned
  instruction IDs;
- Machine side-exit owners carry a `side_exit_region` attribute plus a named
  heterogeneous side-exit argument tail;
- the backend boundary lowers direct-Snapshot exits, including terminating
  interpreter resumes and value-producing inline tag guards, and changes the
  CFG from Core to Machine;
- allocation observes side-exit arguments at the owner's Late position and
  materialization rewrites those executable arguments normally;
- the CFG verifier checks reachable side-exit regions from executable owner
  instructions;
- `ExitToInterpreter`-only exits emit transition transfers and
  `ResumeInterpreter`;
- the AArch64 CFG emitter deduplicates side-exit bindings during emission in
  deterministic first-use order.

The remaining slice is substantive rather than structural: add sinking
analysis, clone the reachable sunk instruction closure in original block order,
discover its complete external frontier, and emit each eligible transition
computation kind. Machine constraints and target emission for individual
nonterminal side-exit owners are added as those owners become executable.

Call-boundary Snapshot lowering is a separate adjacent design and is not part of
these slices.
