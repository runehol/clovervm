# JIT Register Allocation

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | Not started |
| Scope | Allocation constraints, allocator-local numbering, liveness, live-range splitting, block-argument moves, clobbers, spills, and post-allocation location assignments |
| Owning layers | Target backends own allocation-constraint construction; the generic register allocator owns numbering, liveness, intervals, splitting, coalescing, spill decisions, and move resolution; machine-code emission consumes final assignments |
| Validated against | N/A |
| Supersedes | The open register-allocation direction in [JIT Compiler and IR](jit-compiler-and-ir.md) and [JIT Compiler Bring-up Plan](jit-compiler-bring-up-plan.md) |

This document defines the register-allocation contract for the clovervm JIT. It
fits the Core IR and CFG contracts in [JIT Compiler and IR](jit-compiler-and-ir.md)
and [JIT Control-Flow Graph](jit-control-flow-graph.md). Core IR remains a
target-independent SSA CFG with block parameters and edge arguments. Target
backends describe physical requirements through `AllocationConstraints`; the
generic allocator computes liveness, splits ranges, assigns locations, and
produces `LocationAssignments`.

The allocator is target-independent. It may know about register classes,
physical registers, stack slots, clobber masks, and operand constraints, but it
does not know the semantics of AArch64, x86-64, or any concrete Core
instruction kind.

## Phase Products

The allocation pipeline has three main products:

```text
BackendPreparation
    selected lowerings, constant decisions, and target constraints

AllocationConstraints
    target-produced pre-allocation requirements for operand occurrences

LocationAssignments
    allocator-produced post-allocation locations and move bundles
```

`AllocationConstraints` replace the earlier `LocationSummary` terminology. They
describe requirements, not chosen locations.

`LocationAssignments` are the durable post-allocation result. They map program
value occurrences to registers, spill slots, canonical slots, constants, or
other backend-supported physical locations at the points where code emission or
recovery needs them. They also contain resolved split moves and block-edge
parallel-move bundles.

Allocator-local position numbers, live intervals, split children, spill weights,
and coalescing state are scratch data. They do not survive the allocator pass
and are not stored in Core IR, backend preparation, or machine-code emission
metadata.

## Allocation Constraints

An allocation constraint is anchored to a structural occurrence in prepared
Core, not to a numeric program position. The target backend may group
constraints by instruction, block, and edge:

```text
InstructionAllocationConstraints
    input constraints
    output constraints
    temporary constraints
    clobber masks

BlockAllocationConstraints
    entry parameter constraints
    exit constraints when needed by the backend

EdgeAllocationConstraints
    edge-argument transfer constraints and affinities
```

The anchor determines the SSA value and whether the occurrence is a use or a
definition:

```text
Instruction input      -> use of that input's ProgramValueRef
Instruction result     -> definition of that result's ProgramValueRef
Block edge argument    -> use at the source edge
Block parameter        -> definition at target block entry
Instruction temporary  -> non-SSA temporary
Instruction clobber    -> physical register mask
```

The constraint attached to an occurrence may require:

- any legal location;
- a register from a target register class;
- a specific physical register;
- a stack or spill location;
- a target-supported immediate or constant-pool form;
- output reuse of a specific input location;
- a temporary register from a class;
- avoidance of a clobbered physical-register set.

The exact C++ spelling is deferred, but the conceptual constraint kinds are:

```text
Any
Register(class)
FixedRegister(register)
Stack
Immediate
ConstantPool
SameAsInput(input_index)
Temporary(class)
Clobber(register_set)
```

`Register(class)` and spill compatibility must agree with the Core
`ValueRepresentation` of the value. A constraint may narrow that representation
to a target class or fixed register, but it must not change representation
semantics. Representation changes remain explicit Core instructions.

## Durable Anchors and Ephemeral Positions

Constraints are durable within a backend preparation and allocation attempt
because they are anchored to instructions, blocks, block parameters, and block
edges. They are not anchored to integer positions.

Immediately before allocation, the allocator builds an ephemeral numbering from
the current prepared CFG order. The numbering uses two positions per executable
instruction and two positions for each relevant block boundary:

```text
position = 2 * index + phase

phase 0: use / incoming / before
phase 1: def / outgoing / after
```

Within an instruction, uses precede definitions. This lets a fixed-register call
argument use and a fixed-register call result definition share the same physical
register at different phases of the same instruction. It also makes
same-as-input and destructive-operation lowering explicit without treating two
SSA values as one value.

Block entry and block exit each provide use and definition phases. This gives
block parameters, edge arguments, and edge moves stable allocation points
without making integer positions durable:

```text
predecessor block exit use   -> edge arguments are live here
successor block entry def    -> block parameters are defined here
```

Any mutation of the prepared CFG invalidates allocator numbering, liveness,
intervals, and assignments. It does not by itself invalidate structurally
anchored constraints unless the mutation replaces the anchored instruction,
block parameter, or edge.

## Liveness and Splitting

The register allocator owns liveness for allocation. It walks generic Core
def/use information, block parameters, edge arguments, and CFG edges. It also
consumes target `AllocationConstraints` to attach constrained use and definition
positions to allocator-local intervals.

The allocator may internally build:

```text
ProgramValueRef -> parent live range
parent live range -> split child intervals
child interval -> assigned register or spill slot
```

Splitting refines allocation intervals; it does not mutate the Core SSA graph or
require regenerating durable constraints. Constraints remain anchored to their
original structural occurrences. After a split, the child interval covering a
constrained position must satisfy that constraint.

The allocator may use any suitable strategy, including linear scan with splitting
or a later graph-coloring allocator. The public contract is independent of that
choice.

## Block Parameters and Edge Moves

Block parameters and edge arguments are not the same SSA value. A target block
parameter is a new definition at block entry; each incoming edge argument is a
use at the predecessor edge. The transfer has parallel-copy semantics.

For:

```text
then:
    branch join(a)

else:
    branch join(b)

join(p):
    use p
```

`a`, `b`, and `p` are distinct SSA values. The edge transfers create copy
affinities:

```text
then -> join: a -> p
else -> join: b -> p
```

Allocation may coalesce an edge argument and its target parameter when their
live ranges do not interfere and their constraints permit a shared location. If
the assigned locations differ, the allocator records a parallel move bundle on
the corresponding `BlockEdge`.

Critical-edge splitting is an implementation choice made when a move bundle has
no legal insertion point on the original edge. The semantic CFG retains
first-class `BlockEdge` objects and ordered edge arguments.

## Calls, Clobbers, and Temporaries

Calls are represented by ordinary allocation constraints:

```text
argument uses      -> fixed ABI registers or stack slots
result definitions -> fixed return registers
temporaries        -> target register classes
clobbers           -> caller-saved register masks
```

A value live across a call must be assigned to a non-clobbered location or split
around the call. The target describes clobbers; the allocator decides whether to
keep the value in a callee-preserved register, spill it, or insert split moves.

Hidden scratch registers are not allowed. If a selected lowering needs a
temporary, its `AllocationConstraints` must expose that temporary. A reserved
global scratch during bring-up is still modeled as unavailable or clobbered in
the allocator problem rather than being consumed invisibly by emission.

## Recovery and Safepoints

Snapshots remain semantic recovery descriptions. Allocation does not rewrite
Snapshot contents. Instead, Snapshot operands become point uses at the
instructions that consume them for exits or safepoints.

After allocation, recovery planning combines:

```text
Snapshot + LocationAssignments + HomeState -> RecoveryPlan
```

`LocationAssignments` identify where each captured `ProgramValueRef` lives at
the exit position. `HomeState` identifies canonical frame homes that already
contain required values. Recovery planning may then emit register moves, spill
loads, canonical-home stores, constant materialization, F64 boxing, and future
reification work without adding a second semantic state model.

Safepoint maps and deoptimization translations may share physical location
encodings, but they remain separate consumers. Safepoint maps need only managed
roots; deoptimization translations need complete interpreter-visible state.

## Verification

The verifier should be able to check the allocation boundary at three levels:

- every prepared executable instruction has matching allocation constraints for
  its allocatable inputs, outputs, temporaries, and clobbers;
- every constraint anchor resolves to a live prepared Core occurrence with a
  compatible `ValueRepresentation`;
- every post-allocation assignment satisfies the relevant constraint at its
  occurrence position, including fixed registers, clobbers, reuse constraints,
  split moves, and block-edge parallel-copy bundles.

Verification should reject stale numeric positions. Diagnostics should report
the durable anchor: instruction serial and operand index, block parameter, block
entry or exit, or block-edge argument.

## Implementation Staging

The initial executable tests may use fixed, trivial `LocationAssignments` for
`Parameter`, `Const`, and `Return`. Those assignments should still be produced
through the same `AllocationConstraints` vocabulary so the early backend does
not introduce a second convention.

The first real allocator should support:

- target register classes;
- instruction input and output constraints;
- fixed registers;
- same-as-input reuse;
- temporaries;
- clobber masks;
- block parameters and edge arguments;
- spill slots;
- split moves and edge parallel moves.

More advanced policies such as spill-cost tuning, rematerialization,
caller-context-sensitive register pressure, and Machine IR scheduling are later
backend and allocator work.

## External Model

Cranelift and `regalloc2` are the closest external model. Cranelift IR uses
block parameters instead of phi instructions. `regalloc2` consumes block
parameters, branch arguments, operand constraints, register classes, clobbers,
and CFG structure through a target-independent allocator interface. Clover
keeps the same conceptual separation while using direct Core/CFG anchors instead
of making opaque integer program points durable.
