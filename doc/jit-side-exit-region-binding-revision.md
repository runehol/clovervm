# JIT Side-Exit Region/Binding Revision

| Field | Value |
|---|---|
| Document type | Design revision |
| Status | Proposed |
| Scope | Replacing side-exit records that reference main-graph instructions with closed side-exit regions and per-owner bindings |
| Owning layers | Side-exit lowering owns region construction and owner bindings; graph rewriting owns executable owner operands; register allocation owns physical binding locations; transition emission owns late transition-program/cold-stub deduplication |
| Revises | [JIT Side-Exit Lowering](jit-side-exit-lowering.md) |

This revision changes the shape of lowered side exits. A side exit should no
longer retain existing main-graph instruction IDs as its body. Instead,
side-exit lowering creates a closed, block-like `SideExitRegion` containing
side-exit-local parameters and cloned instructions. Each executable
side-exiting owner carries a `SideExitBinding`: the region ID plus ordinary
program-value arguments that bind the owner position to the region parameters.

The goal is to keep side-exit recovery state out of executable block order
without requiring graph rewriting to normalize a second set of non-CFG operand
references.

## Problem

The current `SideExit` model stores:

- external `inputs()`;
- existing retained `instructions()` ending in an existing `Snapshot`.

Those retained instructions are not executable block members, but they still
name main-graph instruction IDs. This is fragile when graph rewriting replaces
an executable instruction and poisons the original storage entry. Owner
`side_exit_arguments()` are ordinary operands and get normalized, but side-exit
inputs and retained instruction operands do not participate in that
normalization. A side exit can therefore keep a stale reference to a poisoned
mainline instruction even though the executable owner has been rewritten
correctly.

Instruction poisoning makes this failure direct and useful. Without poisoning,
the same bug could survive until transition-program construction or physical
location binding, where it would look like a recovery-state mismatch rather
than a stale graph reference.

## Revised Model

The revised representation has two named artifacts:

```text
SideExitRegion
    parameter_ids[]
    instruction_ids[]

SideExitBinding
    region_id
    arguments[]
```

`SideExitRegion` is a closed recovery region. It owns no CFG edges,
predecessors, successors, dominance, loop membership, or normal-path execution.
It is block-like only in storage shape: it has parameter instructions and a
linear instruction list. Its final instruction is a cloned `Snapshot`.

`SideExitBinding` is a specific executable owner use of a region. The owning
Machine instruction carries the region ID as metadata and a parallel
`side_exit_arguments()` operand range. The binding invariant is:

```text
owner.side_exit_arguments[i] binds to region.parameter_ids[i]
```

The region itself does not reference mainline instruction IDs. Its body operands
may refer only to region parameters or earlier region-local cloned
instructions.

## Construction

Side-exit lowering discovers regions only from side-exiting instruction kinds
that explicitly use a Snapshot for non-returning recovery. It must not scan all
`Snapshot` instructions globally.

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

When a package for `S` does not yet exist, lowering constructs it by cloning
the retained side-exit body and final Snapshot. While cloning, it discovers
external mainline values in first-use order. For each first-seen external
value, lowering:

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

## Snapshot Deduplication

Early deduplication happens by original Snapshot instruction ID:

```text
Snapshot InstructionId -> SideExitRegion + argument environment
```

This is intentionally a semantic/source-level deduplication point. Multiple
side-exiting owners that use the same original Snapshot get the same region and
the same argument environment. This preserves parameter/argument alignment and
avoids building several equivalent regions during lowering.

The old model allowed one retained instruction ID to appear in multiple side
exits. The revised model should not rely on that sharing. Retained side-exit
computation is cloned into the region. If two different Snapshots require the
same computation, they get separate cloned instructions unless they share the
same original Snapshot package.

## Graph Ownership

`ControlFlowGraph` should not keep a side-exit registry for semantic
reachability. Side exits are discovered by walking executable instructions,
just as block edges are discovered from branch terminators.

The storage layer may still own side-exit region records and provide dense
`SideExitRegionId` lookup. Reachability is derived from owner instruction
references:

- executable CFG blocks own normal instruction order;
- owner instructions reference side-exit regions and carry binding arguments;
- storage owns allocated side-exit regions;
- an unreferenced region is dead metadata.

This removes graph-wide side-exit iteration from the CFG model and makes side
exits closer to block edges: referenced by instructions, owned by storage, and
validated by walking the executable graph.

## Snapshot Consumers

`Snapshot` remains generic recovery/resume metadata. Not every Snapshot becomes
a side exit.

Function-call snapshots have different semantics: they describe
interpreter-visible state for successful-path call synchronization and return
metadata. Side-exit lowering must not generate regions for these snapshots
unless they are explicitly referenced by a non-returning side-exiting
instruction kind.

The rule is:

- side-exiting operations create `SideExitRegion`/`SideExitBinding` records;
- call-boundary snapshots remain call metadata;
- a standalone Snapshot in the graph is not itself a side exit.

## Register Allocation And Late Deduplication

Early region sharing is not the final executable sharing point. Register
allocation can split live ranges and assign different physical locations to the
same logical argument environment at different owners. Because transition
programs encode physical input locations, two owners of the same region may
need different transition programs after allocation.

After allocation, each owner-side use becomes a physical `SideExitBinding`:

```text
SideExitBinding
    region_id
    argument_refs[]
    argument_locations[]
```

Transition emission consumes the binding and the region:

```text
SideExitRegion + SideExitBinding -> TransitionProgram
```

Late deduplication happens over the altered physical binding or over the
generated transition bytes. Equivalent post-allocation bindings may share one
published transition program and one cold side-exit block. Non-equivalent
bindings must remain separate even when they refer to the same
`SideExitRegion`.

## Invariants

- `owner.side_exit_arguments().size() == region.parameter_ids().size()`.
- `owner.side_exit_arguments[i]` satisfies the representation required by
  `region.parameter_ids[i]`.
- Every region instruction operand refers to a region parameter or an earlier
  region instruction.
- A region instruction must not reference a mainline instruction directly.
- The final region instruction is a `Snapshot`.
- Snapshots before the final region instruction are invalid.
- Side-exit regions have no CFG edges, block predecessors, block successors, or
  normal-path execution position.
- Side-exit regions are created only for side-exiting instruction kinds, not
  for all Snapshot consumers.
- The snapshot-to-region package map is shared across one lowering rewrite so
  every owner of the same original Snapshot receives the same region and
  argument environment.
