# JIT Transition Programs

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | In progress |
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
allocation objects, and Core graph identity after construction. Stack locations
still encode offsets in the execution convention being entered. A side-exit
planner consumes the compiler structures and produces one immutable transition
program for publication. During construction, the instruction sequence is
mutable only through the specific fix-ups its representation requires.

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
namespace. Semantic computation and transfers are ordered only by their
dependencies and may interleave. The representation records no phase boundary
and uses no separate executor or subprogram for publication.

The target side-exit thunk is intentionally mechanical. It saves the selected
machine register state into a fixed memory layout, passes the transition-program
offset and saved-state pointer to the transition executor, and does not know the
logical Snapshot shape.
Target-specific code owns the fixed register order and any platform calling
details.

The first side-exit use maps the three areas as follows:

```text
register file
    fixed register-save image
    read-only transition input

stack
    canonical frame slots, frame headers, and compiled spills
    may be read and written

scratch
    instruction-indexed transition-result and staging slots
    also used for parallel-move scratch space
```

The scratch area is not register allocated. Every dispatched body-instruction
index names one potential 64-bit scratch slot; the `BeginTransition` header is
not part of this index space. Eligible Core results use
`Scratch[instruction_index]`; a `Transfer` used for staging also uses its own
instruction index as the scratch destination. Resultless instructions that do
not stage a value use no scratch.

`TransitionExecutionContext` owns a reusable `std::vector<uint64_t>` transition
scratch buffer. A thread embeds one context for production execution, while
tests and other consumers may construct a context independently.
`BeginTransition.scratch_slot_count` records the actual capacity required by
the lowered program. The context grows the buffer to at least that many elements
and does not clear it: verification ensures that every scratch source was
initialized earlier in the current program. A program containing only direct
resultless transfers may request zero slots. A parallel-transfer cycle requests
the slots introduced for staging. The planner computes the count as zero when
the program contains no scratch locations, otherwise one plus the highest
scratch offset read or written. Resultless instructions after the final scratch
use do not increase it.

Transition execution is a no-safepoint region. No transition instruction may
invoke Python, trigger GC, or otherwise enter safepoint-capable VM code. Saved
registers, stack locations, and raw scratch words therefore remain stable and
need no transition-local root map. `ResumeInterpreter` first completes
canonical publication and then leaves this no-safepoint region. Any future
eligible allocation operation must use a mechanism guaranteed not to
safepoint.

Initial execution is a direct switch interpreter:

```cpp
struct TransitionExecutionInput
{
    std::span<const uint64_t> register_file;
    Value *frame_pointer;
};

struct InterpreterResumeState
{
    Value accumulator;
    BytecodePC resume_pc;
};

InterpreterResumeState execute_transition_program(
    TransitionExecutionContext &context,
    std::span<const TransitionInstruction> instructions,
    TransitionExecutionInput input);
```

`Transfer` copies one raw 64-bit word so tagged and unboxed representations use
the same path. Register-file locations are read-only; stack locations are
relative to the supplied frame pointer; scratch locations address the context.
`ResumeInterpreter` reconstructs the tagged accumulator and returns the
bytecode continuation to the future side-exit adapter. This initial executor
does not itself enter the bytecode interpreter.

## Instruction Representation

Most instructions in a transition program form a restricted straight-line
subset of Core IR. They read operands from the register file, stack, constants,
or earlier scratch slots. The result of an eligible Core instruction is
implicitly `Scratch[instruction_index]`, so it needs no stored destination.

It does not contain branches. It does not model arbitrary interpreter
bytecode. Interpreter bytecode is slot-state oriented and owns generic Python
fallback semantics; a transition program replays only the already-proven
straight-line fragment needed at its boundary.

Transition IR has its own directly stored 16-byte `TransitionInstruction`.
Unlike graph IR, it has no separate storage entry, storage pointer, instruction
ID, or typed view hierarchy. `TransitionInstructionKind` shares the underlying
values of eligible ordinary `InstructionKind`s, so translating eligible Core
computation does not remap kinds. Transition-only kinds occupy reserved values
in that enum.

Every physical source or destination is a four-byte `TransitionLocation`:

```cpp
enum class TransitionLocationArea : uint8_t
{
    RegisterFile,
    Stack,
    Scratch,
};

class TransitionLocation
{
public:
    static TransitionLocation register_file(int16_t index);
    static TransitionLocation stack(int16_t frame_offset);
    static TransitionLocation scratch(int16_t index);

    TransitionLocationArea area() const;
    int16_t offset() const;

private:
    TransitionLocationArea area_;
    int16_t offset_;
};
static_assert(sizeof(TransitionLocation) == 4);
```

Stack offsets may be signed; register-file and scratch offsets are
non-negative. A reference to an earlier eligible Core result is
`Scratch[instruction_index]`. A scratch source must have been initialized by an
earlier instruction.

The initial stored instruction exposes named construction and access only for
the three transition-specific kinds:

```cpp
class alignas(16) TransitionInstruction
{
public:
    static TransitionInstruction
    begin_transition(uint32_t scratch_slot_count);
    static TransitionInstruction
    transfer(TransitionLocation destination, TransitionLocation source);
    static TransitionInstruction
    resume_interpreter(TransitionLocation accumulator, BytecodePC resume_pc);

    TransitionInstructionKind kind() const;

private:
    uint32_t slots_[3];
    TransitionInstructionKind kind_;
    uint16_t reserved_;
};
```

Once sinking introduces several eligible Core computations, generated typed
transition access may be justified. The initial representation does not
generate graph-style instruction views speculatively.

`BeginTransition` is the first 16-byte entry and acts as the program header:

```text
BeginTransition
    attribute:
        scratch_slot_count : uint32_t
```

It has no operands or result. The executor reads it before dispatching the
remaining entries and grows the current thread's scratch buffer when necessary.
The count may be zero. Because the header occupies one aligned
`TransitionInstruction`, the first dispatched instruction begins at the next
16-byte-aligned address.

The builder emits `BeginTransition {scratch_slot_count = 0}` before any
position-derived result exists. Once the complete stream determines its scratch
requirement, each append immediately patches that instruction through
`set_scratch_slot_count()`. An instruction with an implicit result requires the
scratch slot named by its body position. A `Transfer` with an explicit scratch
destination requires that destination slot. The builder retains no duplicate
scratch-count field and `finalize()` performs no sizing scan.

Compiler-side construction follows the graph builder's ownership vocabulary:

```cpp
class TransitionProgramBuilder
{
public:
    TransitionProgramBuilder();

    void append_instruction(TransitionInstruction instruction);
    void emplace_transfer(TransitionLocation destination,
                          TransitionLocation source);
    void emplace_resume_interpreter(TransitionLocation accumulator,
                                    BytecodePC resume_pc);

    std::vector<TransitionInstruction> finalize() &&;

private:
    void require_scratch_slot(uint32_t slot);

    std::vector<TransitionInstruction> instructions_;
};
```

`append_instruction()` is the single placement and scratch-accounting path.
The `emplace` operations construct and append the transition-specific
instructions. `finalize()` verifies the completed stream and moves out the
compiler-side vector. This is the only initial instruction mutation API; other
fix-ups are added only when construction requires them.

`Transfer` is a resultless transition instruction with an explicit destination:

```text
Transfer
    attribute:
        destination : TransitionLocation
    operand:
        source : TransitionLocation
```

The source is an operand because it is read. The destination is an attribute
because it is a write target rather than a use. Executing the instruction
updates that location and produces no Core-style result. A `Transfer` that
stages a value in scratch explicitly names `Scratch[instruction_index]`.

`ResumeInterpreter` is the initial terminal handoff instruction:

```text
ResumeInterpreter
    operand:
        accumulator : TransitionLocation
    attribute:
        resume_pc : BytecodePC
```

It has no result. It reads the reconstructed accumulator from its explicit
source, ends transition execution after canonical interpreter state has been
published, and identifies the bytecode continuation. The initial non-inlined
side-exit context supplies the owning `CodeObject`; representing the active code
object for an inlined frame is deferred with inlining. The resume PC is part of
the continuous program rather than parallel side-exit metadata. Future
transition consumers may define different terminal handoff instructions.

Constants are not a fourth mutable storage area. Scalar constants remain inline
attributes where the schema permits. Pointer-shaped tagged values are addressed
through the compiled code object's constant pool and loaded by eligible
constant instructions.

The transition executor reads `TransitionInstruction` directly and dispatches
on its stored kind. It does not construct the storage-pointer-plus-ID typed
views used by graph IR.

## Schema Eligibility

`instruction.def` explicitly declares whether each ordinary instruction kind is
legal in a `TransitionProgram`. Eligibility is declared, not inferred from
current operands or effects. Execution semantics remain explicit executor code;
the initial implementation does not generate an interpreter body or typed
transition views from the schema.

An eligible Core instruction:

- has one result and only fixed operands encodable as `TransitionLocation`;
- has only inline scalar or constant-pool-index attributes;
- has no `SnapshotRef`, block edge, `Shape *`, or `ValidityCell *`;
- cannot branch, terminate a block, side exit, or invoke Python;
- has an explicit transition-executor implementation using the generated
  layout accessors.

Variadic instructions, resultless Core instructions, and instructions requiring
indirect operands are excluded initially. `BeginTransition`, `Transfer`, and
`ResumeInterpreter` are transition-only kinds described by
`TransitionInstruction` rather than the graph instruction schema.

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
    canonical frame slot, frame header, or compiled spill

Scratch + offset
    slot initialized by an earlier Core result or Transfer
```

The side-exit thunk and planner agree on the register-file layout. The
transition program therefore needs no embedded physical-register object or
target-specific register numbering. The side-exit register file is read-only.
Any temporary or computed value goes to the scratch area.

The Snapshot identifies the value required at each state position, and the
CFG's `BytecodeStateOrder` maps those positions to the accumulator and canonical
frame homes. `LocationAssignments` resolve non-sunk frontier values to registers
and spill locations. Transition planning combines these with the sinking
attachment; it does not ask register allocation to assign locations to sunk
defs.

The initial side-exit implementation supports only the outer bytecode frame.
`BytecodeStateOrder` supplies that frame's complete position mapping. Inlining
must extend the Snapshot's frame description with one corresponding mapping per
active logical frame before inlined exits are supported.

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

Transition planning reuses `order_parallel_assignments<TransitionLocation>()`.
Its cycle-scratch callback allocates the scratch location belonging to the
preservation `Transfer`; unlike physical register-allocation materialization,
it needs no second legalization pass because a transition `Transfer` directly
supports stack-to-stack locations.

For example:

```text
logical:
    home_a = home_b
    home_b = home_a

lowered:
    Transfer Scratch[0], home_a
    Transfer home_a, home_b
    Transfer home_b, Scratch[0]
```

The program does not distinguish compute scratch from transfer scratch;
execution sees one instruction-indexed scratch namespace.

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

The planner consumes a Snapshot, the CFG's `BytecodeStateOrder`,
post-allocation `LocationAssignments`, and the sinking attachment, but the
resulting plan retains none of them. It is self-contained immutable metadata
owned by one compiled code object. Core graph identity and graph generation end
at planning.

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

For eligible Core entries, generated reconstruction is mechanical once physical
inputs have been represented as frontier values. The reinflater walks the
entire stream while maintaining a symbolic value for each initialized
`TransitionLocation`. An eligible Core entry constructs the corresponding Core
instruction and binds its implicit scratch result. A `Transfer` updates the
symbolic destination without becoming a Core instruction. Attributes such as
constant-pool references resolve through the owning compiled code object's
metadata.

```text
RegisterFile / Stack
    -> frontier parameter or transition-entry load

Scratch
    -> earlier reinflated Core result or value propagated by Transfer

eligible TransitionInstruction(kind, transition operands, attributes)
    -> ordinary Core instruction of the same kind

Transfer(destination, source)
    -> symbolic destination now names symbolic source
```

Reinflation is not required for the first implementation. The proposed
representation keeps the option open by preserving:

- Core instruction kind and attributes for sunk computation;
- operand identity through scratch slots and frontier inputs;
- canonical destination positions encoded by `Transfer.destination`;
- aliasing of reconstructed object and boxed values within one exit;
- the distinction between semantic computation and physical transfer.

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
- every required Snapshot position either has a physical source that aliases its
  canonical destination or is written by the transition program;
- every encoded area and offset is valid for the owning transition;
- `BeginTransition` is the first entry and its scratch count covers every
  scratch location used by the program;
- every scratch source has been initialized by an earlier instruction;
- every implicit result targets its instruction's body position, while an
  explicit `Transfer` may target any declared scratch location;
- a side-exit transition never writes its register-file image;
- `ResumeInterpreter` reads an initialized tagged accumulator source;
- exactly one terminal handoff is present and it is the final instruction;
- `ResumeInterpreter.resume_pc` is valid for the owning bytecode code object.

These checks belong near transition planning and allocation-boundary
verification. A malformed transition program is a compiler bug, not a
recoverable runtime condition.

## Implementation Slices

The first slice establishes only the representation:

- define `TransitionLocation` and the three location areas;
- define the directly stored 16-byte `TransitionInstruction`;
- add named construction and access for resultless `BeginTransition`,
  `Transfer`, and `ResumeInterpreter`;
- permit patching the initial `BeginTransition` scratch count after the stream
  has been constructed.

The second slice builds and executes standalone programs:

- build, verify, and format self-delimiting transition sequences;
- add a reusable execution context with scratch storage sized by
  `BeginTransition`;
- interpret the three transition-only kinds without safepointing.

The third slice publishes programs and connects side exits:

- publish 16-byte-aligned transition sequences with compiled code-object
  metadata;
- embed a reusable transition execution context in each thread;
- expand Snapshot liveness at executable exit consumers;
- translate Snapshot positions through `BytecodeStateOrder` and combine them
  with the physical frontier to form parallel transfers;
- lower those transfers into transition `Transfer` entries;
- end each side-exit program with `ResumeInterpreter`;
- connect the target thunk and interpreter handoff.

Only after those slices should Core instruction eligibility, sinking, and
transition-local computation be added. This validates state translation before
extending the program into a restricted Core interpreter.
