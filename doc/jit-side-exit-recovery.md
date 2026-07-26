# JIT Side-Exit Recovery

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | Not started |
| Scope | Side-exit state recovery, sunk Core instruction execution, saved machine-state inputs, canonical publication, and future exit reinflation |
| Owning layers | Core IR owns sunk operation semantics and Snapshot state; register allocation owns physical frontier locations; recovery planning owns the continuous side-exit program, scratch layout, and canonical publication; target thunks own fixed machine-state saves |
| Validated against | N/A |
| Supersedes | N/A |

This document defines the proposed representation for JIT side exits that
recover interpreter state from optimized compiled execution. It refines the
Snapshot and recovery direction in [JIT Compiler and IR](jit-compiler-and-ir.md)
and [JIT Register Allocation](jit-register-allocation.md).

The design goal is to make side exits cheap on the compiled path while keeping
the recovery representation structural enough to support later compilation of
hot alternate paths. Recovery must not become an independent second compiler
and interpreter matrix. It should reuse Core instruction kinds where it is
executing Python-value computation, and use a tiny machine-level transfer
language only for physical publication.

## Execution Shape

A side exit follows one fixed execution sequence:

```text
compiled side-exit branch
    -> target side-exit thunk
       saves machine registers in a fixed layout
    -> recovery program
       computes recovered values and publishes canonical interpreter homes
    -> interpreter resume at RecoveryPlan.resume_pc
```

The recovery program is one continuous instruction stream with one operand and
scratch-slot namespace. Semantic recovery instructions naturally precede the
physical publication instructions that consume their results, but the
representation records no phase boundary and uses no separate executor or
subprogram for publication.

The target side-exit thunk is intentionally mechanical. It saves the selected
machine register state into a fixed memory layout, passes the exit identifier
and saved-state pointer to recovery, and does not know the logical Snapshot
shape. Target-specific code owns the fixed register order and any platform
calling details.

Recovery receives three storage domains:

```text
canonical interpreter stack
    canonical frame slots, accumulator home, and frame headers
    may be read by recovery and written by publication

saved machine state
    fixed register-save area plus compiled-frame spill locations
    read-only recovery input

scratch area
    dense recovery SSA value slots
    also used for transfer scratch space
```

The scratch area is not register allocated. Each value-producing recovery
instruction writes one dense scratch slot, usually in program order. Lowering
physical copies may allocate as many additional scratch slots as necessary to
resolve publication safely. Avoiding coalescing and stack-slot packing keeps
exit execution simple and keeps the representation predictable for debugging
and future reinflation.

## Core Recovery Instructions

The semantic instructions in the recovery program form a restricted
straight-line subset of Core IR. They execute in SSA form: every instruction
reads operands from saved machine state, canonical homes, constants, or earlier
scratch slots, and writes its result to one scratch slot.

It does not contain branches. It does not model arbitrary interpreter
bytecode. Interpreter bytecode is slot-state oriented and owns generic Python
fallback semantics; side-exit recovery is Snapshot oriented and should replay
only the already-proven optimized recovery fragment.

The durable operation identity should reuse `InstructionKind` and the
`instruction.def` schema from Core IR. Recovery should generate a second
fixed-record representation for recovery-supported Core instructions rather
than hand-maintaining a parallel opcode set.

```cpp
struct RecoveryValueRef
{
    uint16_t kind;
    ValueRepresentation representation;
    uint32_t index;
};
static_assert(sizeof(RecoveryValueRef) == 8);

struct RecoveryInstructionHeader
{
    InstructionKind kind;
    ValueRepresentation result_representation;
    uint16_t flags;
    uint32_t result_scratch_slot;
};

struct RecoveryAndSMIInstruction
{
    RecoveryInstructionHeader header;
    RecoveryValueRef lhs;
    RecoveryValueRef rhs;
};
```

This is a sketch of the important ownership shape, not a required exact C++
API. The important points are:

- the recovery instruction kind is the ordinary Core `InstructionKind`;
- fixed operands are represented by `RecoveryValueRef`, not `ProgramValueRef`
  pointers;
- pointer-like attributes are represented by indexed metadata references;
- fixed-size recovery records are generated from the same schema that
  generates ordinary Core instruction classes.

Recovery excludes variadic Core instructions from this fixed-record form.
`Snapshot`, block terminators, block-edge users, and instructions with
`SnapshotRef` operands are likewise excluded. A generator should fail at build
time if a recovery-supported instruction has a schema shape the recovery format
cannot encode.

Attributes use a recovery-specific mapping. Pointer-free attributes such as
`BytecodePC` may remain inline integers. Pointer-bearing attributes such as
`Shape *`, `ValidityCell *`, strings, and managed constants become indexed
references into compiled-code-owned metadata tables. Recovery instruction
records must be copyable memory data: no `Instruction *`, no `ProgramValueRef`,
no raw heap or shape pointers.

A table derived near Core instruction metadata decides which Core instruction
kinds are recovery-supported and sinkable.

A Core instruction is initially sinkable only when all of these are true:

- it has no ordinary compiled-path use;
- every transitive use reaches a Snapshot capture through sunk instructions;
- it commutes from its original position to every consuming side exit;
- all operands are available through the non-sunk physical frontier, constants,
  canonical homes, or earlier scratch slots;
- recovery supports the instruction kind;
- the instruction has no side exit;
- the instruction does not invoke Python dispatch;
- any allocation, mutation, identity, and failure behavior is explicitly part
  of the recovery contract.

Instructions such as `AddSMI` are not sinkable under this policy because their
overflow behavior is itself a side exit. The first scalar subset should be
small and total under the facts already established on the optimized path.

## Versioned Object Recovery

The semantic part of the recovery stream is not limited to pure scalar
expressions. It must also support object materialization patterns where
optimized execution avoided allocating a short-lived object that is needed only
if an exit is taken.

The preferred representation is receiver versioning:

```text
object_s0 = NewObject(initial_shape=S0)
object_s1 = AddOwnProperty(object_s0, "a", value_a, next_shape=S1)
object_s2 = AddOwnProperty(object_s1, "b", value_b, next_shape=S2)
Snapshot(..., object_s2)
```

Each property-addition instruction consumes one receiver version and produces
the next version of the same runtime object. The def-use chain records the
observable initialization order and final shape transition without requiring a
general resultless effect chain.

On a taken exit, recovery allocates one object, performs the property additions
in order, and stores the same recovered object wherever the Snapshot aliases
the final version. Alias preservation is within one recovery execution; two
different exits may independently materialize equivalent objects because only
one exit is taken for one execution.

General Python attribute stores are not this operation. They may call
`__setattr__`, invoke descriptors, allocate, raise, or observe arbitrary
runtime state. A recovery-supported own-property transition must be an internal
object-layout operation with validity, shape, ordering, write-barrier, and
reference-ownership behavior made explicit before it is marked sinkable.

## Physical Inputs

Recovery operands name where the current value can be read at the exit:

```text
SavedRegisterSlot(register_class, register_number)
SpillSlot(offset)
CanonicalHome(state_position)
ScratchSlot(slot)
Constant(pool_index or immediate payload)
```

Saved machine state is read-only. If recovery or publication needs temporary
storage, it uses the scratch area. This keeps the physical frontier separate
from recovered values and avoids target-specific scratch-register policy in the
recovery interpreter.

`LocationAssignments` resolve non-sunk frontier values to registers and spill
locations. `HomeState` identifies canonical homes that already contain the
Snapshot's desired values. Recovery planning combines both with the sinking
attachment; it does not ask register allocation to assign locations to sunk
defs.

## Physical Publication Instructions

Canonical publication is not Core semantics. It uses physical-copy
instructions in the same recovery stream to make the canonical interpreter
stack match the Snapshot state.

These representation-aware instructions move tagged or unboxed machine values
between canonical homes, saved physical inputs, and scratch slots. They do not
express Python semantics and should not be added to ordinary Core lowering.

The logical publication step has parallel-assignment semantics. A source
canonical home may also be a destination. Recovery planning must therefore
lower publication through scratch slots when a store would clobber a value
still needed by a later store.

For example:

```text
logical:
    home_a = home_b
    home_b = home_a

lowered:
    tmp0 = load home_a
    tmp1 = load home_b
    store tmp1 to home_a
    store tmp0 to home_b
```

The scratch slots above are ordinary slots in the same scratch area as semantic
results. The program does not distinguish compute scratch from transfer
scratch; execution sees one dense scratch namespace.

## Recovery Plan Product

A side-exit recovery product owns one instruction stream:

```cpp
struct RecoveryPlan
{
    BytecodePC resume_pc;
    uint32_t scratch_slot_count;
    std::vector<RecoveryInstructionRecord> instructions;
};
```

`resume_pc` remains plan metadata. `scratch_slot_count` tells the recovery
executor how much dense temporary storage to reserve. The fixed saved-register
layout belongs to the target thunk, while saved-register slots, compiled-frame
locations, canonical destinations, and their source values are encoded directly
by instruction operands.

The planner consumes a Snapshot, post-allocation `LocationAssignments`, and
`HomeState`, but the resulting plan retains none of them. It is self-contained
immutable metadata owned by one compiled code object. Core graph identity and
graph generation end at planning.

`RecoveryInstructionRecord` covers both schema-generated Core recovery records
and recovery-specific physical-copy records. Their ordering supplies every
dependency. There is no stored publication offset, phase tag, or secondary
transfer vector.

Several exits may share structurally identical compute fragments, but sharing
is an optimization. The initial implementation may build one plan per side
exit. The representation must still preserve enough structure to allow later
interning or alternate-path compilation.

## Reinflating an Exit

The semantic records in the recovery program are deliberately closer to Core
IR than to interpreter bytecode. A hot side exit can later seed alternate-path
compilation by rebuilding ordinary Core instructions from those records and
connecting the published logical state to the resume point.

With generated fixed records, reinflation is mechanical. Each recovery record
names an ordinary Core `InstructionKind`; its fixed operands decode to
frontier values, constants, canonical-home inputs, or previously reinflated
scratch-slot values; and its metadata references resolve through the owning
compiled code object's metadata tables. The reinflater walks the straight-line
compute record sequence and constructs the corresponding ordinary Core
instruction for each record.

```text
SavedRegisterSlot / SpillSlot / CanonicalHome
    -> frontier parameter or recovery-entry load

Constant
    -> Const

ScratchSlot
    -> ProgramValueRef produced by an earlier reinflated record

RecoveryInstructionRecord(kind, operands, metadata)
    -> ordinary Core instruction of the same kind
```

Reinflation is not required for the first implementation. The proposed
representation keeps the option open by preserving:

- Core instruction kind and attributes for sunk computation;
- operand identity through recovery value slots and frontier inputs;
- canonical destination positions encoded by physical-copy records;
- aliasing of recovered object and boxed values within one exit;
- the boundary between Python-value computation and machine transfer
  publication.

Physical-copy records are not reinflated as ordinary Core semantics. A later
backend may compile them directly or omit publication that is unnecessary when
alternate-path compilation continues without first materializing every
canonical home. This record distinction does not divide the stored recovery
stream into phases.

## Verification

Recovery verification should check:

- every sunk def has no ordinary executable use;
- every sunk def captured by a Snapshot is reachable through the recovery
  program for each consuming exit;
- every non-sunk frontier operand is live and has a physical source at the
  exit;
- every recovery-supported Core step accepts the representations of its
  operands and result;
- no recovery step contains branches, side exits, Python dispatch, or
  unsupported fallibility;
- object materialization chains preserve receiver-version order and aliasing;
- transfer publication has parallel-assignment semantics after lowering;
- every required Snapshot position is either already current in `HomeState` or
  written by the recovery program;
- no saved machine-state slot is written by recovery.

These checks belong near recovery planning and allocation-boundary verification.
Malformed recovery is a compiler bug, not a recoverable runtime condition.

## Initial Slice

The first implementation should be intentionally narrow:

- represent side-exit records and fixed saved-register inputs;
- create recovery plans for `ResumeInInterpreter` without sunk computation;
- publish canonical Snapshot state through physical-copy instructions in the
  same recovery stream;
- expand Snapshot liveness at executable consumers;
- add sunk-def metadata as an attachment, not an instruction flag;
- add one or two total scalar recovery-supported Core operations;
- only then add receiver-versioned object materialization.

This sequence validates the storage model and publication semantics before
introducing allocation or mutation in recovery.
