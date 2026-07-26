# JIT Transition Programs

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Not started |
| Scope | Compact straight-line programs that transform values and machine state between execution conventions; the first consumer is JIT side exit |
| Owning layers | Core IR owns sunk operation semantics and Snapshot state; register allocation owns physical frontier locations; transition planning owns the continuous transition program and canonical publication; each thread owns reusable transition scratch storage; target thunks own fixed machine-state saves |
| Validated against | N/A |
| Supersedes | N/A |

This document defines a compact `TransitionProgram`: a straight-line program
that transforms values and machine state from one execution convention into
another. The first consumer is a JIT side exit, where a transition program
reconstructs canonical interpreter state from optimized compiled execution.
The same representation may later implement adapters between compiled function
calling conventions or other execution boundaries.

The representation is restricted Core IR plus transition-specific
`BeginTransition`, `Transfer`, and terminal handoff instructions. The first
terminal is `ResumeInterpreter`. It is independent of Snapshots, register
allocation, and interpreter frames after construction. A side-exit planner
consumes those compiler structures and produces one immutable transition
program.

## Execution Shape

A side exit follows one fixed execution sequence:

```text
compiled side-exit branch
    -> target side-exit thunk
       saves machine registers in a fixed layout
    -> transition program
       BeginTransition sizes reusable thread scratch
       computes required values and publishes canonical interpreter homes
       ResumeInterpreter identifies the bytecode continuation
    -> interpreter resumes
```

The transition program is one continuous instruction stream with one location
namespace. Semantic instructions naturally precede the transfers that publish
their results, but the representation records no phase boundary and uses no
separate executor or subprogram for publication.

The target side-exit thunk is intentionally mechanical. It saves the selected
machine register state into a fixed memory layout, passes the exit identifier
and saved-state pointer to the transition executor, and does not know the
logical Snapshot shape. Target-specific code owns the fixed register order and
any platform calling details.

The first side-exit use maps the three areas as follows:

```text
register file
    fixed register-save image
    read-only transition input

stack
    canonical frame slots, accumulator home, frame headers, and compiled spills
    may be read and written

scratch
    dense transition-result slots
    also used for parallel-move scratch space
```

The scratch area is not register allocated. Every instruction index names one
potential 64-bit scratch slot. Eligible Core results use
`Scratch[instruction_index]`; a `Transfer` used for staging also uses its own
instruction index as the scratch destination. Resultless instructions that do
not stage a value use no scratch.

Each thread owns a reusable `std::vector<uint64_t>` transition scratch buffer.
`BeginTransition.scratch_slot_count` records the actual capacity required by
the lowered program. The executor grows the buffer to at least that many
elements and does not clear it: verification ensures that every scratch source
was initialized earlier in the current program. A program containing only
direct resultless transfers may request zero slots. A parallel-transfer cycle
requests the slots introduced for staging. The planner computes the count as
zero when the program contains no scratch locations, otherwise one plus the
highest scratch offset read or written. Resultless instructions after the final
scratch use do not increase it.

Transition execution is a no-safepoint region. No transition instruction may
invoke Python, trigger GC, or otherwise enter safepoint-capable VM code. Saved
registers, stack locations, and raw scratch words therefore remain stable and
need no transition-local root map. `ResumeInterpreter` first completes
canonical publication and then leaves this no-safepoint region. Any future
eligible allocation operation must use a mechanism guaranteed not to
safepoint.

## Instruction Representation

Most instructions in a transition program form a restricted straight-line
subset of Core IR. They read operands from the register file, stack, constants,
or earlier scratch slots. The result of an eligible Core instruction is
implicitly `Scratch[instruction_index]`, so it needs no stored destination.

It does not contain branches. It does not model arbitrary interpreter
bytecode. Interpreter bytecode is slot-state oriented and owns generic Python
fallback semantics; a transition program replays only the already-proven
straight-line fragment needed at its boundary.

The stored instruction uses the compact 16-byte `InstructionEntry` physical
layout and ordinary `InstructionKind` values. Its references are not Core
`InstructionId`s. Every physical source or destination is a self-contained
32-bit `TransitionLocation`:

```cpp
enum class TransitionLocationArea : uint32_t
{
    RegisterFile,
    Stack,
    Scratch,
};

class TransitionLocation
{
public:
    TransitionLocationArea area() const;
    int32_t offset() const;

private:
    uint32_t bits_;
};
static_assert(sizeof(TransitionLocation) == sizeof(uint32_t));
```

The high tag bits select one of the three areas and the remaining bits encode
an offset within that area. Stack offsets may be signed; register-file and
scratch offsets are non-negative. The exact bit partition is an encoding
detail, but construction must check that every offset fits. A reference to an
earlier eligible Core result is `Scratch[instruction_index]`. A scratch source
must have been initialized by an earlier instruction.

`BeginTransition` is the first 16-byte entry and acts as the program header:

```text
BeginTransition
    attribute:
        scratch_slot_count : uint32_t
```

It has no operands or result. The executor reads it before dispatching the
remaining entries and grows the current thread's scratch buffer when necessary.
The count may be zero. Because the header occupies one aligned
`InstructionEntry`, the first dispatched instruction begins at the next
16-byte-aligned address.

`Transfer` is a resultless transition instruction with an explicit destination:

```text
Transfer
    operand:
        source : TransitionLocation
    attribute:
        destination : TransitionLocation
```

The source is an operand because it is read. The destination is an attribute
because it is a write target rather than a use. Executing the instruction
updates that location and produces no Core-style result. A `Transfer` that
stages a value in scratch explicitly names `Scratch[instruction_index]`.

`ResumeInterpreter` is the initial terminal handoff instruction:

```text
ResumeInterpreter
    attribute:
        resume_pc : BytecodePC
```

It has no operands or result. It ends transition execution after canonical
interpreter state has been published and identifies the bytecode continuation.
The resume PC is therefore part of the continuous program rather than parallel
side-exit metadata. Future transition consumers may define different terminal
handoff instructions.

Constants are not a fourth mutable storage area. Scalar constants remain inline
attributes where the schema permits. Pointer-shaped tagged values are addressed
through the compiled code object's constant pool and loaded by eligible
constant instructions.

The transition executor reads `InstructionEntry` directly. It dispatches on the
stored kind and decodes slots according to generated metadata; it does not
construct the storage-pointer-plus-ID typed views used by compiler code.

## Schema Eligibility

`instruction.def` explicitly declares whether each instruction kind is legal in
a `TransitionProgram`. The generator uses that declaration both to produce the
transition dispatch and to reject an invalid eligible schema at build time.
Eligibility is declared, not inferred from current operands or effects.

An eligible Core instruction:

- has one result and only fixed operands encodable as `TransitionLocation`;
- has only inline scalar or constant-pool-index attributes;
- has no `SnapshotRef`, block edge, `Shape *`, or `ValidityCell *`;
- cannot branch, terminate a block, side exit, or invoke Python;
- has a generated transition-executor handler.

Variadic instructions, resultless Core instructions, and instructions requiring
indirect operands are excluded initially. `BeginTransition` is transition-only,
has `ResultClass::None`, and carries one inline scratch-count attribute.
`Transfer` is transition-only and has `ResultClass::None` together with its
generated source-operand and destination-attribute layout. `ResumeInterpreter`
is transition-only, has `ResultClass::None`, carries one inline `BytecodePC`
attribute, and terminates the stream.

A Core instruction is initially sinkable only when all of these are true:

- it has no ordinary compiled-path use;
- every transitive use reaches a Snapshot capture through sunk instructions;
- it commutes from its original position to every consuming side exit;
- all operands are available through the non-sunk physical frontier, constants,
  canonical homes, or earlier scratch slots;
- the instruction kind is transition-program eligible;
- the instruction has no side exit;
- the instruction does not invoke Python dispatch;
- it cannot safepoint or trigger GC;
- any mutation, identity, and failure behavior is explicitly part of the
  transition contract.

Instructions such as `AddSMI` are not sinkable under this policy because their
overflow behavior is itself a side exit. The first scalar subset should be
small and total under the facts already established on the optimized path.

## Physical Inputs

For a side exit, the three `TransitionLocationArea` values resolve as follows:

```text
RegisterFile + offset
    slot in the register image saved by the target thunk

Stack + offset
    canonical frame slot, accumulator home, frame header, or compiled spill

Scratch + offset
    slot initialized by an earlier Core result or Transfer
```

The side-exit thunk and planner agree on the register-file layout. The
transition program therefore needs no embedded physical-register object or
target-specific register numbering. The side-exit register file is read-only.
Any temporary or computed value goes to the scratch area.

`LocationAssignments` resolve non-sunk frontier values to registers and spill
locations. `HomeState` identifies canonical homes that already contain the
Snapshot's desired values. Transition planning combines both with the sinking
attachment; it does not ask register allocation to assign locations to sunk
defs.

## Transfers and Publication

Canonical publication uses `Transfer` instructions in the same transition
stream. Source and destination area tags distinguish register-to-scratch,
stack-to-scratch, scratch-to-stack, and direct stack-to-stack transfers without
separate load and store opcodes.

The logical publication step has parallel-assignment semantics. A source
canonical home may also be a destination. Transition planning must therefore
resolve the parallel assignment into ordered `Transfer` instructions,
introducing a scratch transfer before a write would clobber a value still
needed later.

For example:

```text
logical:
    home_a = home_b
    home_b = home_a

lowered:
    Transfer home_a -> tmp0
    Transfer home_b -> tmp1
    Transfer tmp1 -> home_a
    Transfer tmp0 -> home_b
```

The program does not distinguish compute scratch from transfer scratch;
execution sees one dense scratch namespace.

## Transition Program Product

A published transition program is a self-delimiting sequence in immutable
code-object metadata:

```text
BeginTransition {scratch_slot_count = N}
...
terminal handoff
```

The compiled caller refers to the first entry by a code-object-relative offset.
`BeginTransition` supplies the scratch requirement, and the terminal handoff
ends execution. No separate program record, instruction count, completion
record, `std::vector`, or process-local pointer is retained. The enclosing
code-object metadata allocation provides the outer bounds used by verification.
A compiler-side builder may use ordinary growable containers before
publication.

The fixed saved-register layout belongs to the target thunk, while
saved-register slots, compiled-frame locations, canonical destinations, and
their source values are encoded by `TransitionLocation` operands and
attributes.

The planner consumes a Snapshot, post-allocation `LocationAssignments`, and
`HomeState`, but the resulting plan retains none of them. It is self-contained
immutable metadata owned by one compiled code object. Core graph identity and
graph generation end at planning.

The selected instruction sequence begins with `BeginTransition`, contains
schema-generated eligible Core operations and `Transfer`, and ends with one
terminal handoff. Its ordering supplies every dependency. There is no
publication offset, phase tag, secondary transfer vector, or separate
completion record.

Several exits may share structurally identical compute fragments, but sharing
is an optimization. The initial implementation may build one plan per side
exit. The representation must still preserve enough structure to allow later
interning or alternate-path compilation.

## Reinflating an Exit

The semantic entries in a transition program are deliberately closer to Core
IR than to interpreter bytecode. A hot side exit can later seed alternate-path
compilation by rebuilding ordinary Core instructions from those records and
connecting the published logical state to the resume point.

With generated fixed entries, reinflation is mechanical. Each eligible entry
names an ordinary Core `InstructionKind`; its fixed operands decode to
frontier values, constants, canonical-home inputs, or previously reinflated
scratch-slot values; and its metadata references resolve through the owning
compiled code object's metadata tables. The reinflater walks the straight-line
compute record sequence and constructs the corresponding ordinary Core
instruction for each record.

```text
RegisterFile / Stack
    -> frontier parameter or transition-entry load

Constant
    -> Const

Scratch
    -> ProgramValueRef produced by an earlier reinflated record

InstructionEntry(kind, transition operands, attributes)
    -> ordinary Core instruction of the same kind
```

Reinflation is not required for the first implementation. The proposed
representation keeps the option open by preserving:

- Core instruction kind and attributes for sunk computation;
- operand identity through scratch slots and frontier inputs;
- canonical destination positions encoded by `Transfer.destination`;
- aliasing of recovered object and boxed values within one exit;
- the boundary between Python-value computation and machine transfer
  publication.

A later backend may omit final publication transfers that are unnecessary when
alternate-path compilation continues without first materializing every
canonical home. This does not divide the stored transition stream into phases.
Transition-only transfers and terminal handoffs are not reinflated as ordinary
Core instructions.

Reinflation is one possible consumer, not part of the representation contract.
A function adapter may instead execute or compile the same transition program
and then jump directly to another compiled entry convention.

## Verification

Transition-program verification should check:

- every sunk def has no ordinary executable use;
- every sunk def captured by a Snapshot is reachable through the transition
  program for each consuming exit;
- every non-sunk frontier operand is live and has a physical source at the
  exit;
- every Core step is explicitly transition-program eligible and accepts the
  representations of its operands and result;
- no transition step contains branches, side exits, Python dispatch, or
  unsupported fallibility;
- transition execution contains no safepoint or GC-capable operation;
- transfer publication has parallel-assignment semantics after lowering;
- every required Snapshot position is either already current in `HomeState` or
  written by the transition program;
- every encoded area and offset is valid for the owning transition;
- `BeginTransition` is the first entry and its scratch count covers every
  scratch location used by the program;
- every scratch source has been initialized by an earlier instruction;
- every scratch write targets the current instruction's scratch slot;
- a side-exit transition never writes its register-file image;
- exactly one terminal handoff is present and it is the final instruction;
- `ResumeInterpreter.resume_pc` is valid for the owning bytecode code object.

These checks belong near transition planning and allocation-boundary
verification. A malformed transition program is a compiler bug, not a
recoverable runtime condition.

## Initial Slice

The first implementation should be intentionally narrow:

- add transition eligibility to `instruction.def` and generate the restricted
  dispatch metadata;
- define `TransitionLocation` and the three location areas;
- add resultless `BeginTransition` with an inline scratch-slot count;
- add transition-only `Transfer` with a source operand and destination
  attribute;
- add resultless terminal `ResumeInterpreter` with an inline `BytecodePC`;
- represent `TransitionProgram` and fixed saved-state inputs;
- create transition programs for Core `ResumeInInterpreter` without sunk
  computation, ending each with transition `ResumeInterpreter`;
- publish canonical Snapshot state through `Transfer` instructions in the same
  transition stream;
- expand Snapshot liveness at executable consumers;
- run programs with reusable per-thread scratch storage sized by
  `BeginTransition`.

This sequence validates the storage model and publication semantics before
adding any Core computation to transition programs.
