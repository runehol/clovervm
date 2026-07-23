# JIT Compiler and IR

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | Not started |
| Scope | JIT pipeline, Core IR, recovery state, effects, backend lowering, and compiled execution contracts |
| Owning layers | The JIT owns IR and compiled execution; bytecode, runtime frames, object semantics, and reclamation remain authoritative contracts |
| Validated against | N/A |
| Supersedes | N/A |

This document defines the durable architecture and IR contracts for a future
clovervm JIT compiler. Its purpose is to keep compiled execution compatible
with the existing bytecode, object model, inline caches, calling convention,
and reclamation machinery.

Implementation order and temporary runtime policies belong to the
[JIT Compiler Bring-up Plan](jit-compiler-bring-up-plan.md). The optional
higher-effort inference frontend belongs to
[Semantic IR and Specialization](jit-semantic-ir-and-specialization.md). The
fixed instruction storage, typed read-only views, and restricted analysis
mutation API belong to
[JIT Instruction Representation](jit-instruction-representation.md). Target
instruction encoding, code fragments, label resolution, and branch sizing are
defined by [JIT Machine-Code Emission](jit-machine-code-emission.md).

The mandatory compiler pipeline has one principal compiler IR:

```text
encoded bytecode + IC snapshots -> Core IR -> target backend -> machine code
```

Core IR makes speculative checks, narrowed guard results, effects, and control
flow explicit in an ordered, list-based SSA CFG. A target backend assigns
physical locations and emits machine code. It may lower through a separate
Machine IR when that representation pays for itself, but Machine IR is not a
mandatory whole-function phase.

Semantic IR is an optional optimization frontend, not a prerequisite for
compiled execution:

```text
encoded bytecode + IC snapshots -> Semantic IR -> Core IR
    -> target backend -> machine code
```

It preserves atomic bytecode semantics so that type inference,
caller-context-sensitive inlining, polymorphic reasoning, and other
higher-effort optimization can happen before checks and actions are expanded.
The baseline JIT may lower directly into Core IR, without function inlining or a
semantic type system. This is an intentional compilation mode rather than an
incomplete form of the Semantic pipeline.

The following choices are design guardrails rather than tentative suggestions:

- bytecode is the canonical execution and recovery model;
- every safepoint provides precise root discovery through a supported frame
  scanning policy;
- interpreter resumption always receives exact canonical bytecode frame state;
- interpreted and compiled Python calls share the existing managed
  calling convention;
- inline caches drive specialization, and misses return to the interpreter;
- the baseline JIT operates on tagged `Value`s and does not require unboxing.

Semantic type inference and type partitions remain optional. The detailed
effect taxonomy and several backend policies remain open architectural work.

## Terminology

Use distinct terms for operations that are easily conflated:

- **load** or **cache**: move a canonical frame value into a machine register;
- **spill**: move a register value into a machine spill slot;
- **frame synchronization**: write current interpreter-visible values into
  canonical frame homes;
- **frame activation**: make a preallocated canonical frame region the current
  logical Python frame and initialize its structural metadata;
- **frame reconstruction**: create an interpreter frame that has no existing
  canonical backing region; active inlined frames do not require this;
- **boxing**: create a tagged heap object for an unboxed scalar value;
- **reification**: create runtime object state for a virtual semantic value;
  boxing is its simplest instance;
- **root publication**: expose roots to reclamation machinery; the bring-up
  policy does this by synchronizing canonical frames and the accumulator;
- **safepoint map**: metadata identifying the current machine locations of
  managed roots at one compiled safepoint;
- **deoptimization translation**: metadata mapping optimized locations,
  constants, and recipes to complete logical interpreter frame state.

`Materialization` is too ambiguous to be the precise name for these operations.

## Runtime and Recovery Contract

### Runtime invariant: bytecode is canonical

The existing bytecode is the canonical execution and recovery model. JIT code
is a speculative execution of that bytecode, not a replacement language with
independent state or semantics.

Compiled execution must be able to return to the interpreter at essentially
any speculative point, including:

- a failed shape or shape-key check;
- an invalid validity cell;
- SMI arithmetic overflow;
- an inline-cache miss;
- a tripped safepoint poll;
- any other failed assumption used by compiled code.

At such an exit, the JIT must recover the interpreter-visible state at the
appropriate bytecode position. Designs that make bytecode state expensive or
impossible to reconstruct are out of scope.

### Runtime invariant: precise discovery and exact recovery

At every safepoint, all managed roots in every active interpreted and compiled
frame are precisely discoverable through a supported frame-scanning policy.
Before execution transfers from compiled code to the interpreter, every
interpreter-visible value, accumulator, bytecode PC, and structural frame field
is reconstructed in canonical bytecode frame form. Reification produces each
required virtual object once per semantic identity.

This is the permanent runtime contract. It does not require optimized values to
occupy canonical homes at all times. Canonical publication and precise compiled
stack maps are alternative implementations of root discovery; generated
recovery code and an interpreted deoptimization translation are alternative
implementations of exact interpreter reconstruction.

The two choices are related but independent:

```text
safepoint root discovery:
    CanonicalPublished | PreciseStackMap

deoptimization recovery:
    GeneratedRecovery | InterpretedTranslation
```

Safepoint maps need describe only managed roots. Deoptimization translations
must describe all interpreter-visible values, including non-roots, constants,
inlined logical frames, and future reification recipes. They should share
location encodings and frame-state construction without being forced into one
metadata format.

### Initial runtime policy

The first implementation uses canonical frame publication for root discovery
and generated recovery code for interpreter reconstruction. Generated Python
executes on the existing Clover stack, while the hand-written interpreter and
all C or C++ targets execute on the host stack through reentrant transition
thunks. These are staging policies rather than permanent runtime invariants.

Their implementation order, consequences, and migration toward precise stack
maps and a future mixed platform stack are owned by the
[JIT Compiler Bring-up Plan](jit-compiler-bring-up-plan.md). The durable IR
contract is that Snapshots, managed-value liveness, and post-allocation
locations contain enough information to generate both exact recovery and,
later, precise root maps independently of which runtime policy consumes them.

### Interpreter-visible state

The bytecode uses an accumulator and stack-backed registers. Registers contain,
in order:

1. arguments to the current function;
2. local variables;
3. temporaries;
4. outgoing arguments for the function about to be called.

Moving the frame pointer changes the interpretation of these slots, similarly
to register windows. Each bytecode register retains a stable canonical frame
home even when its current value is cached in a machine register.

The accumulator already has special safepoint treatment: it is published
through `ThreadState` separately from the scanned frame range. The JIT reuses
that mechanism rather than adding an accumulator frame slot.

Inlining does not remove the corresponding Python frame activation. Every
active inlined function has a stable canonical backing region laid out
contiguously after its caller's region. Entering it advances the managed frame
pointer to that region and initializes its structural metadata. Returning from
it restores the caller's frame pointer. Canonical values in both regions may
remain stale until publication is required.

### Deoptimization and commit boundaries

Each semantic bytecode operation has a commit boundary. Before its final
effectful action runs, failed speculation can reconstruct the state before the
bytecode and retry it in the interpreter. Once an effect has occurred, retrying
the bytecode may duplicate Python-visible behavior.

Exit state must therefore distinguish:

- pre-effect exits that resume at the current bytecode;
- exits after a committed result that resume at a later bytecode state.

The current JIT does not model general compiled exception unwinding. A generic
or otherwise replayable operation that would raise exits before taking the
exception and retries the same bytecode in the interpreter. A native trusted
handler is different: it may return `Value::exception_marker()` with pending
exception state already installed on `ThreadState`, matching the interpreter's
trusted-handler path. Such an exit must hand off to interpreter exception
handling for the already-pending exception; it must not replay the bytecode or
construct a replacement exception. The exact handoff encoding is backend policy,
but the semantic state is the existing pending exception plus the recovered
interpreter frames.

Every deoptimizing check retains a bytecode origin and consumes a Snapshot of
its logical resume state. Moving or commoning checks must preserve a legal
Snapshot and replay point, not merely the same successful-path computation.

### Calls and runtime boundaries

Interpreted and compiled Python execution share the managed calling convention:
arguments occupy the outgoing argument window, moving the managed frame pointer
activates the callee, and ordinary results travel through the accumulator.
Every frame retains both its canonical interpreted continuation and an
executable compiled return target. Cross-mode dispatch selects an engine and
continuation without introducing a second Python argument ABI.

The exact frame header, target return sequences, post-return stack position,
and interpreter-return thunk are defined by the
[Function Calling Convention](function-calling-convention.md). Host-stack
switching, re-entry, native result transport, and root publication across C or
C++ calls are defined by
[Native/Managed Boundary Contracts](native-managed-boundaries.md).

Compiled-to-compiled performance should initially come from inlining rather
than a separate fast-entry convention. Inlining still creates a logical Python
frame with canonical backing and ordinary caller metadata, so a real call from
inside an inline instance uses the same managed ABI.

A Python call may be the successful action of any overloaded operator IC. State
publication required before such a call is continuing successful-path work,
not a non-returning speculative exit. A trusted handler certified
`NoSafepoint`, `NoReclaim`, and `NoCallPython` requires only native ABI
preservation; an unknown or safepoint-capable call uses the active root-discovery
policy.

Core IR uses one Python-call operation. The call records the interpreter return
pc as an instruction attribute, not as a dataflow operand. That pc is used if
the callee safepoints, crosses a native boundary, deoptimizes, or otherwise
returns to interpreter-visible caller state. For ordinary calls and single-step
protocols, it is the next bytecode pc after the logical call. For a step in a
multi-step operator protocol, it is the pc of the paired continuation bytecode,
such as `CheckOperatorNotImplemented`. `PythonCall` has no optional
NotImplemented-specific attribute or variant.

Such a call is followed by an explicit `CheckNotImplemented` Core instruction.
The check consumes the call result and forwards the same tagged value on the
successful path. If the result is the `NotImplemented` singleton, compiled code
exits through its own Snapshot to the paired continuation bytecode. That
Snapshot's `resume_pc` is the check's continuation pc, and its accumulator action
records the call result required by the interpreter continuation. The check
therefore does not rely on the preceding call's installed return pc or on the
call having left every interpreter-visible value in canonical storage. This is
intentionally redundant with the initial fully canonical call-boundary policy:
it keeps all exit-capable Core instructions on the same recovery path and
continues to work if calls later use precise safepoint metadata instead.
Single-step protocols such as unary operators, membership, subscription, and
ordinary calls do not add that check. The `NotImplemented` check must not be
hidden inside the generic call node, because the continuation byte is a semantic
resume target distinct from ordinary deoptimization retry and from successful
operator completion.

The cost of publishing dirty canonical homes at small non-inlined calls is a
runtime-policy question rather than an IR ambiguity. Core IR and its backend
metadata must support both generated publication and future precise stack maps.
The initial choice and measurements that may replace it belong to the
[JIT Compiler Bring-up Plan](jit-compiler-bring-up-plan.md).

### Exceptions, unsupported operations, and observability

An ordinary speculative failure resumes a state in which the failed semantic
operation may be retried. Once a terminal effect has committed, generated code
must continue normally, enter the appropriate exception or post-commit state,
or recover a later bytecode boundary; it must not replay the effect.

Core IR therefore makes the exit kind and Snapshot explicit. The runtime policy
may initially return to the interpreter for exception-table dispatch and every
unsupported bytecode, while a later compiler may represent those paths in
generated control flow. Both policies use the same commit-boundary rule.

Tracing, traceback construction, stack inspection, and similar facilities may
request exact logical frames and bytecode PCs without a failed speculation.
Such requests use the same Snapshot and recovery model. The initial set of
unsupported bytecodes and observability fallbacks belongs to the
[JIT Compiler Bring-up Plan](jit-compiler-bring-up-plan.md).

## Compiler Pipeline and Phase Ownership

### Direct Core compilation

The direct path lowers decoded bytecode and IC snapshots into Core IR. The
shared frontend discovers bytecode basic blocks and captures
the semantic content of relevant ICs. Core construction then creates SSA for
the accumulator and bytecode registers while expanding each supported IC case
into explicit checks, actions, and side exits.

This path deliberately omits:

- Python function inlining;
- general semantic type inference;
- caller-to-callee fact propagation;
- relational type partitions and polymorphic code duplication.

It can still generate useful code. Monomorphic IC cases already identify the
predicates and successful action, while unknown or unsupported cases can use a
conservative Python call or return to the interpreter. Core IR can perform
ordinary SSA optimization, dominator-based redundant-check elimination, and
effect-aware local code motion without a general Python type lattice.

The zero-opcode first vertical slice, conservative initial optimizer, and order
in which opcode families enter this path are specified by the
[JIT Compiler Bring-up Plan](jit-compiler-bring-up-plan.md).

### Optional Semantic IR frontend

Semantic IR is a parsed SSA representation of bytecode semantics, not a wrapper
around the encoded bytes in `CodeObject::code`. Its atomic operations resemble
the semantic bytecode operations emitted by the high-level compiler. It is
inserted before Core IR only when the compiler chooses to spend more effort on
inference, inlining, or polymorphic specialization.

When present, Semantic IR owns:

- SSA construction for the accumulator and bytecode registers;
- compilation-local inline-cache specialization plans;
- type and shape inference;
- context-sensitive inlining;
- logical bytecode frame states.

Its inference lattice, feedback selection, correlated partitions, and lowering
rules are defined separately in
[Semantic IR and Specialization](jit-semantic-ir-and-specialization.md).

### Core IR

Core IR expands atomic semantic operations into checks and smaller actions.
It may instead construct those checks and actions directly from decoded
bytecode and IC snapshots when Semantic IR is absent.
It owns:

- value-refining guards and deoptimization exits;
- realization of demanded type partitions as control flow;
- explicit operation effects and dependencies;
- redundant-guard elimination and effect-aware code motion;
- target-independent value representations and explicit representation
  conversions;
- ordered calls, operations, and commit points.

When Semantic IR is present, lowering consumes its frozen facts and partition
identities to choose explicit Core checks, control flow, and operations. Core
carries only distinctions materialized in that structure and any concrete
Core attachment required by a later Core pass; the Semantic analysis attachment
may then be discarded. A general semantic type system is not required by the
direct compilation path.

Core IR is the stable SSA CFG carried through progressively stronger verified
forms rather than mandatory replacement representations:

```text
constructed Core IR       -> explicit checks, effects, Snapshots, tagged values
represented Core IR       -> selected operations and explicit representation conversions
backend-prepared Core IR  -> lowering choices, legalized constants, AllocationConstraints
allocated backend plan    -> locations, spills, and edge-move tables
recovery plans            -> Snapshots resolved through locations and HomeState
```

Phase verification prevents arbitrary mixtures of these forms. Common
instructions remain target-neutral; target constraints and allocated locations
belong to backend side tables. If those tables grow into an implicit machine
instruction graph, the backend may introduce Machine IR instead.

### Optional backend Machine IR

A target backend owns register classes, target operations, calls, flags,
addressing constraints, spills, branches, register allocation, scheduling, and
final encoding. A simple backend may assign locations to Core program values
in side tables and emit directly from the Core schedule. A harder target may
introduce a machine-oriented SSA or virtual-register representation for one
block, one region, or the complete function.

A direct backend may maintain tables such as:

```text
ProgramValueRef -> register | spill | canonical slot
BlockEdge *      -> parallel move bundle
Instruction *    -> AllocationConstraints and lowering choice
```

A target-specific `AllocationConstraints` record describes the input, output,
temporary, and clobber requirements of one selected lowering, including fixed
registers, register classes, same-as-input constraints, and whether the
lowering contains a general call, a callee-safe call, a native leaf call, or a
call only on a slow path. It is backend analysis data rather than part of the
immutable common IR operation. The durable allocation contract is defined in
[JIT Register Allocation](jit-register-allocation.md).

Core ordinary operands are uniformly instruction-result references. Every
tagged constant becomes a normal `ProgramValue` through `Const`, whose
`ValueConstant` attribute may contain either a non-pointer value or a managed
pointer retained by the compilation session. Snapshots capture the resulting
`ProgramValueRef`.

Backend preparation or Machine IR classifies each surviving constant as an
immediate synthesis or traced-pool load. Pointer-valued constants must use the
pool; non-pointer constants may use either form according to target
encodability and profitability. The same phase assigns the final lowering and
`AllocationConstraints` for every executable instruction. Pool entries do not form
part of this phase product; the selected lowering retains the `Value` until
program-order emission.

Backend preparation is atomic as a phase contract even if implemented by local
selection and rewrite steps. Liveness, Snapshot point-use expansion, and
register allocation run only after its completed graph is verified. Any later
Core mutation invalidates the complete lowering-choice, legalization, and
`AllocationConstraints` attachment together; the compiler never pairs a rewritten
graph with stale backend preparation. The phase returns a frozen,
generation-checked `BackendPreparation` object, and the allocator accepts that
object rather than independent tables. This is a concrete phase product, not a
generic instruction property bag.

Core has no constant-producing `ResultClass`: `Const` produces an ordinary
`ProgramValue` from a constant attribute. This keeps immediate shape
rules, thresholds, GC relocation requirements, and operand-encoding quirks out
of Core IR.

A backend may later introduce a target-specific immediate pseudo-instruction,
for example `ArmImm12`, after it has selected a foldable use. Such a node must
have exactly one operand use, and that use must be a compatible use site into
which it will be folded. It has no independent liveness or location, cannot
appear in Snapshots or cross blocks, and must be consumed before final emission.
This restores target immediate selection without complicating common Core
operands.

The Core `ValueRepresentation` of a `ProgramValueRef` determines the compatible
target register and spill classes. The backend maps target-independent
representations to its own classes, for example:

```text
TaggedValue -> general-purpose register class
F64         -> floating-point/SIMD register class
```

Addresses used only while selecting or emitting one instruction are backend
temporaries, not a common Core representation. `Address` should become a
`ValueRepresentation` only if implementation demonstrates a need for addresses
to live across Core instructions as SSA program values.

`AllocationConstraints` may narrow that default to a fixed register or another
operation-specific constraint, but it may not assign an incompatible class.
`UnboxF64` therefore crosses from a general-purpose input to a floating-point
output, while `BoxF64` crosses in the opposite direction.

These are backend results, not mutations of Core instructions. Internal
temporaries belong to the selected lowering and may use reserved scratch
locations. If a backend needs enough independently allocated temporaries or
cross-instruction machine optimization that these tables become an implicit
instruction graph, that is evidence for introducing an explicit Machine IR at
the required scope.

After backend preparation, the register allocator and location tables accept
only `ProgramValueRef`s. Instructions with `ResultClass::None` and compiler-only
`SnapshotRef`s receive no location, although the program-value operands named by
a Snapshot remain point uses for liveness and recovery. Every live
`ProgramValueRef` has one authoritative allocated location at a particular
machine-code position.
Live range splitting may move it between locations over time; synchronized
canonical homes and temporary machine copies do not become additional
allocator-owned locations for the same SSA value. A move over a split live range
remains within the representation's compatible register or spill class;
changing class requires an explicit representation-conversion instruction.

This choice belongs to each target backend; the common compiler does not
require all targets to pay for another graph construction and traversal.
Backend-local machine copies, spills, and reloads change physical location, not
Python value identity. If a Machine IR is used, it should remain primarily a
lowering and allocation representation unless measurements justify broader
machine-level optimization.

Final program-order encoding does not itself require Machine IR. The target
backend may encode ordinary instructions directly into `CodeFragment`s while
retaining distance-dependent branches for final layout, as defined by
[JIT Machine-Code Emission](jit-machine-code-emission.md).

## Shared IR Foundations

### Ordered list-based SSA

The canonical function representation is a CFG of basic blocks with an ordered
parameter list and instruction list in each block. Instructions have SSA
operands and may have one result. Terminators pass an ordered argument list to
each normal successor, matching that successor's block parameters. Blocks end
with explicit normal-flow terminators. Instructions may also carry explicit
non-returning deoptimization side exits.

The list is the current schedule. SSA edges expose value and guard-result
dependencies, while effect annotations constrain movement. List order does not
create false dependencies between independent pure operations; it records their
chosen placement until a pass deliberately moves them.

Frame publication for deoptimization is not a Core IR side effect. Core IR
records the Snapshot and the point that may exit; target-specific side-exit code
materializes the interpreter frame state later from the Snapshot, allocated
locations, and canonical-home state.

This representation fits clovervm because:

- guards have bytecode locations and explicit Snapshot bailout operands;
- effectful operations create commit boundaries;
- shape and validity facts have control- and effect-bounded lifetimes;
- calls and safepoints require explicit root-discovery state;
- moving an instruction changes what is live at a deoptimization point;
- generic Python arithmetic may call overloaded methods, making source order a
  useful conservative default.

Out-of-order AArch64 and x86-64 cores recover substantial instruction-level
parallelism dynamically. A temporary DAG or sea-of-nodes form may still help a
narrow optimization or instruction-selection task, but it is not the canonical
whole-function representation.

Ordered lists are used at every instantiated level. Optional Semantic IR begins
in decoded bytecode order, Core IR orders checks and effects, and an optional
Machine IR records whatever target schedule its backend needs. A direct backend
uses the Core schedule plus backend location and edge-move side tables.

### Preserve runtime value identity without gratuitous SSA identities

Operations that only move an existing Python value do not create new semantic
SSA identities:

- `Ldar`, `Star`, and `Mov` update the accumulator/register environment to
  reference an existing value;
- expanding a bytecode preserves the semantic identity of its result;
- guards and shape-changing mutations may introduce a new statically refined
  identity for the same runtime `Value` bits;
- boxing, unboxing, and other representation conversions produce explicit new
  SSA identities;
- only genuine defs, refinements, shape-state successors, and non-trivial
  block parameters introduce identities;
- a block parameter whose incoming arguments are all the same value can be
  eliminated.

For example:

```text
LdaSmi 1       accumulator -> %v1
Star r2        r2          -> %v1
Ldar r2        accumulator -> %v1
Mov r2, r3     r3          -> %v1
```

All four interpreter locations refer to the same semantic value `%v1`.

An SSA value has one guaranteed static type. A value-refining guard therefore
returns a new SSA value rather than changing the facts attached to its input:

```text
%value: Value
%smi: Smi = ShapeKeyCheck %value, Smi
%result: Smi = AddSMI %smi, %other_smi
```

`%value` and `%smi` contain the same runtime bits, but they are distinct SSA
identities with different guaranteed types. Ordinary dominance determines
where `%smi` is available. At a merge, block parameters join the incoming
types, such as `Smi | Float`. This uses the same SSA mechanism for facts
established by explicit guards and facts established by visible CFG branches.

This refinement is not a machine copy and does not require a new register.
Backend coalescing should normally give the input and refined result the same
location when they have the same representation and their live ranges permit
it.

Canonical slots belong to logical frame states, not SSA values. One
`ProgramValueRef` may supply several logical slots or inlined frames, while one
canonical slot denotes different SSA values at different program points. A
logical Python value represented in several machine forms has a distinct
`ProgramValueRef` for each form rather than several locations for one SSA value.

### Logical frame states and Snapshot results

Function arguments are SSA definitions at entry even when their values remain
physically in canonical argument slots until first use. During construction, a
mutable environment maps the global accumulator and each active logical
frame's bytecode registers to `ProgramValueRef`s. Different live incoming values
receive block parameters at a join; dead interpreter locations need none.

Recoverable bytecode boundaries use immutable, structurally shared logical
state:

```text
FrameState:
    CodeObject
    bytecode pc
    parent FrameState       # inlined caller
    register 0 -> ProgramValueRef
    register 1 -> ProgramValueRef
    ...

ActiveState:
    innermost FrameState
    accumulator -> ProgramValueRef
```

There is one accumulator in thread execution state, analogous to a
distinguished machine register. Suspended parent frames do not own separate
accumulators. A `FrameState` describes interpreter meaning and does not claim
that its canonical homes are synchronized.

Core construction maintains transient state:

```text
BuilderContext:
    active logical frame chain
    (frame instance, register) -> ProgramValueRef
    accumulator -> ProgramValueRef
    current bytecode position
    optional inferred and available facts
```

Updating this mapping changes the logical value of an accumulator or bytecode
register without writing its canonical home.

Frame-state and block-transfer arrays use one positional logical-register
coordinate. Position zero is the function-arity-derived offset from `fp`;
increasing position means increasing logical register index and descending
physical stack address. Values appear in this order:

```text
parameters
frame-header holes
locals
temporaries
next inlined frame's parameters / caller's outgoing arguments
next frame-header holes
next frame's locals and temporaries
...
```

Outgoing arguments and the next inlined frame's parameters are the same slots.
They occur once. Each frame header contributes reserved positions for
interpreted PC, compiled PC, FP, and code object wherever that inlined frame
begins. These positions are structural and cannot mean a dead or unknown
`Value`. They are not represented by nullable program-value references; their
entry encoding is deferred to the same recovery mechanism that will represent
an ordinary destination whose desired value is already in its canonical slot.

The header fields are reconstructed from frame metadata rather than passed as
SSA values. In particular, the two PCs and FP are not `Value`-encoded fields;
recovery writes their native representations. This permits compiled execution
to omit inlined frame-header stores and sink them into side exits.

Core IR captures a recoverable state with a zero-code `Snapshot` instruction:

```text
%snapshot: Snapshot = Snapshot(
    resume = Add@17,
    accumulator = %acc,
    logical_slots = [%arg0, %arg1, <header>, <header>, <header>, <header>,
                     %local0, %temporary0, ...])
```

The four header entries above are structural recovery positions, not missing or
nullable program values. Generic use and liveness traversal skips them;
verification matches them against the logical frame boundaries.

A Snapshot records an accumulator action rather than requiring an accumulator
value unconditionally. Ordinary guards capture the live accumulator. At a call
boundary the input accumulator is dead because the call consumes it and either
produces a replacement result or transfers through an exception/exit
continuation; that Snapshot carries no accumulator operand, point use, or
register-allocation requirement for the dead value.

`Snapshot` has `ResultClass::Snapshot`. Its typed `SnapshotRef` receives no
machine location and carries no Python type evidence. A speculative check or
action consumes it as the failure continuation:

```text
%lhs_smi: Smi = ShapeKeyCheck %lhs, Smi
    deopt %snapshot

%result: Smi = AddSMI %lhs_smi, %rhs_smi
    overflow %snapshot
```

Snapshot operands are point uses for recovery liveness at every consuming
exit, even when that exit is not an ordinary CFG successor. The allocator must
make their transitive values recoverable from a register, spill, synchronized
home, or constant at that position.

Several failures may share one Snapshot when they replay the same bytecode from
the same logical state. A pre-effect and post-commit continuation require
different Snapshots. Snapshots are anchored to their exits rather than freely
scheduled: moving a check is legal only when every operand remains available
and replay from the retained state remains correct.

Core motion is constrained by Snapshot availability, effect ordering, and replay
semantics, not by a synthetic frame-publication instruction. The backend owns the
register shuffling, scratch use, and canonical-frame writes required by the taken
side exit.

A Snapshot may capture a recovery-only action such as boxing an unboxed float.
The action produces a recovery-local value that several logical homes may share,
so one taken exit creates one box and preserves Python object identity. It does
not change the normal-path representation of the unboxed SSA value or create an
allocator-visible program value.

### Instruction results, pointer references, and deterministic traversal

Instructions, blocks, and block edges use stable pointers for semantic
references. A client can follow an operand or CFG edge without also carrying a
container that translates an integer ID back into the referenced object. Each
arena pool nevertheless assigns a strongly typed, monotonically increasing
serial to every allocation. Serials identify objects for diagnostics, stable
tie-breaking, and deterministic ordering; pointer values do not.

Compilation-wide semantic identities remain integer IDs where they name
logical records rather than directly traversable IR objects:

```text
PartitionId
FrameStateId

# backend-interned recovery identities
ResumeStateId
RecoveryPlanId
```

Partition IDs are compilation-wide because Semantic and Core IR refer to the
same logical partitions. They are allocated monotonically in deterministic
construction order and are not reused during a compilation.

Snapshots do not have a separate ID namespace: a `SnapshotRef` is the typed
result of a Core instruction. A `FrameStateId` identifies the structurally
shared logical frame chain named by that Snapshot, including any inlined
frames. The initial backend builds recovery plans directly from Snapshots,
post-allocation locations, and `HomeState`; it does not introduce another
deoptimization-state identity. `ResumeStateId` and `RecoveryPlanId` exist only
where the backend interns the two independently shareable parts of generated
side-exit code. Future precise root maps are attached to machine-code positions
rather than given IR result identities.

Every instruction has a typed serial and an intrinsic `ResultClass`:

```text
ResultClass::None
ResultClass::ProgramValue
ResultClass::Snapshot
```

Operand slots use a separate `OperandClass` enum. `OperandClass::ProgramValue`
and `OperandClass::Snapshot` intentionally have the same numeric values as their
matching `ResultClass` cases, so validation can compare result-consuming
operands without a mapping switch. Every ordinary Core operand is an
instruction-result reference. Constants become ordinary values through `Const`,
including when captured by Snapshot. Structural editing therefore updates
uniform def-use relationships. Non-dataflow
payload such as `BlockEdge`, `Shape`, `ShapeKey`, `ValidityCell`, immediates,
bytecode origins, interpreter return PCs, and value constants
are instruction attributes rather than operands. The instruction schema records
the result class, the class and layout of every fixed or variable operand, and
the class and layout of every attribute as defined in
[JIT Instruction Representation](jit-instruction-representation.md).

The instruction pointer also names its result when it has one. Typed result
references may be zero-overhead pointer views:

```text
ProgramValueRef = ResultRef<ResultClass::ProgramValue, Instruction *>
SnapshotRef     = ResultRef<ResultClass::Snapshot, Instruction *>
```

Constructing a result reference requires the def's intrinsic `ResultClass`
to match. Graph ownership and IR-level verification prevent Semantic and Core
references from being mixed. A value-less instruction retains its pointer and
serial for traversal, diagnostics, effects, and rewriting, but cannot be used
as a result operand. A Snapshot can be used only through `SnapshotRef`, never as
a program value. This gives C++ analyses and backends useful static distinctions
without container-relative instruction IDs. Here, a program value is a value in
the compiled program's SSA semantics; it does not prescribe the concrete
`cl::Value` representation.

Core refines `ProgramValueRef` with the def's immutable
`ValueRepresentation`. Fixed-representation instruction APIs use zero-overhead
wrappers such as `TaggedValueRef` and `F64Ref`, while generic graph
infrastructure uses erased `ProgramValueRef` and Semantic IR remains
representation-free. The schema, typed-wrapper, and checked-erasure rules are
defined in [JIT Instruction Representation](jit-instruction-representation.md).

The initial IRs permit at most one result per instruction. Core block parameters
are output-producing pseudo-instructions with one kind per representation,
initially the tagged `Parameter` and `ParameterF64`, and are referenced
generically by `ProgramValueRef`; the block stores their references in its
ordered parameter vector. A genuine need for multi-result instructions would
justify revisiting this rule, but none is currently required.

A block stores parameter instructions separately from its ordinary instruction
sequence. Both are ordinary reclaimable vectors of arena-owned `Instruction *`
pointers. Parameter-vector position is the logical register index; frame-header
positions use the future non-value recovery entry rather than parameter
instructions or null pointers. Value parameters conceptually precede every body
instruction but are not schedulable members of the body vector. Blocks are
relatively few and are destroyed
normally so vector storage can be reclaimed during repeated graph rebuilding,
while the much more numerous fixed-size instructions remain trivially
destructible arena records.

A block owns one ordered parameter vector, and every incoming edge supplies an
equally sized argument vector. The entire edge transfer has parallel-copy
semantics. It is never interpreted as a sequence of assignments in which an
earlier destination can overwrite a source still needed by a later one. The
backend resolves the parallel copy after locations have been assigned, using an
edge block or scratch location to break cycles when necessary.

An instruction operand may reference only a parameter of its own block or an
earlier instruction in that block. Values cross block boundaries exclusively as
edge arguments received by successor block parameters. This makes local order
the body vector's order and avoids a graph-wide instruction-placement index.

Compilation behavior must not depend on pointer addresses or hash-table
iteration order. Passes traverse blocks, instructions, edges, and worklists in
defined orders, using typed serials as stable tie-breakers. If a hash table is
needed, any results that affect compiler output are ordered by serial before
use. Dense side tables may use a pool serial's numeric value as an index where
the table applies to every instruction. Dumps and diagnostics print typed
serials rather than addresses.

IR instructions, partition anchors, frame states, and related compilation
objects have compilation-scoped lifetime. Their pointers, serials, and IDs
remain valid for that lifetime. The fixed, trivially destructible instruction
storage and bulk arena lifetime are specified in
[JIT Instruction Representation](jit-instruction-representation.md).

### Compilation safepoints and retained constants

Initial JIT compilation runs without compiler safepoints. If the runtime
requests a safepoint, it waits for the current compilation to finish. This
depends on compilation remaining short and is monitored as part of JIT latency;
explicit yield points between completed phases may be added later if
measurements require them. Arbitrary safepoints in the middle of an editor
transaction are not supported.

Managed constants remain direct `ValueConstant` attributes on Core `Const`
instructions. Because instruction payloads are not GC-scannable relocation
slots, the compilation session retains every pointer constant embedded in a
graph. Graph construction calls `retain_and_pin_value()` when it encounters an
existing constant. A transformation creating a managed constant calls the same
operation immediately; the rewrite context exposes it directly. Each call
appends an `Owned<Value>` entry to the monotonic session vector. Core neither
constructs nor stores machine-code pool indices.

Instruction allocation itself does not inspect attributes or acquire managed
ownership. Cloning an existing pointer constant into another session registers
it with that destination session before construction. Detachment leaves the
retain in place until session teardown. In the current collector the ownership
retain is also the compilation pin. A future moving collector must not relocate
these entries while instruction payloads may contain their addresses.

Compiler allocation uses native compilation arenas and buffers. Managed
allocation may cross an allocation limit and request a future safepoint, but it
does not synchronously stop the allocating thread or run reclamation.
Compilation does not acknowledge safepoints mid-phase, so reclamation waits for
the compilation to finish. A constant folder may therefore create a managed
value and immediately retain it in the session before publishing it to later
compiler work. Any semantic validity dependencies created by folding remain a
separate obligation.

Emission submits only surviving constants to `MachineCodeEmitter`, which owns
them while assigning and deduplicating final pool entries. Successful code
publication creates the heap `JitCodeObject`, initializes its pool, and makes
those pool slots visible to its native-layout scanner before releasing
session-retained constants. The compilation session must therefore remain alive
until `JitCodeObject` creation is complete. Failed compilation releases the
emitter-owned values and session retains with the rest of the compilation
session. A future phase-boundary safepoint scheme must make every direct managed
compiler reference relocatable or explicitly pinned before acknowledging the
request.

### Mostly immutable instructions, mutable graphs, and analysis state

An instruction's operation kind, results, bytecode origin, semantic descriptor,
guard obligations, partition IDs, payload shape, and attributes are fixed when
the instruction is constructed. A transformation changes any of these semantic
properties by constructing a replacement instruction with a new serial.
Program-value and Snapshot operand slots are mutable only through the structural
IR editor, so ordinary result-use rewriting does not require cascading
replacement of every transitive user. Block edges,
runtime metadata, immediates, and return PCs embedded in an instruction remain
immutable attributes; changing them requires instruction replacement.

Graph structure remains mutable. Block parameter and instruction lists,
predecessor and successor sets, edge argument lists, placement, definition
indexes, and other graph-owned indexes are maintained by the IR editor after
publication. Use records are derived metadata rather than a permanently
maintained graph index. A replacement rewrites uses and updates graph-owned
structures transactionally; it does not mutate the old instruction in place.
Logical interpreter homes are tracked by FrameStates and Snapshots rather than
by preserving an SSA result identity across rewrites.

Initial construction uses a privileged `GraphBuilder` that takes the compilation
session and allocates its unpublished graph from the session-owned arena; blocks
expose their vectors read-only to ordinary clients. Builder finalization
verifies and publishes the graph, returns its stable arena-owned pointer, and
invalidates the builder. It is valid to abandon an unfinalized graph when the
enclosing compilation fails. Builder and rewriter APIs consistently use `make`
for allocation without attachment, `append` for attachment at the end of a
specified container, and `emplace` for allocation and attachment at the end.

Published block-body mutation uses a proposed `BlockRewrite`: the editor builds
a replacement ordinary vector from existing and new instruction pointers and
commits it by swapping the vector. This keeps partial rebuilds invisible and
makes traversal contiguous. Multi-block movement commits the affected rewrites
together before deciding which omitted instructions are actually detached.

Initial translation and major representation boundaries use a bulk graph
builder rather than paying incremental-editor costs for every appended
instruction. Schema-generated, IR-level-specific factories allocate
intrinsically valid, unplaced instructions from the compilation arena. Builder
append and emplace are amortized constant time and deliberately defer global
structural checks. Finalization validates the complete destination graph once
in linear time before publishing it to passes. This keeps type-safe construction
from making an otherwise linear JIT translation quadratic. The incremental
editor remains the mutation authority for an already published graph; large
edit transactions may likewise defer global verification until commit.

The instruction schema generates the generic operand walk used to build an
on-demand `UseLists`. The CFG owns this generation-tagged cache, while a
prepared `GraphQueries` façade gives a traversal or rewrite callback explicit
access to the queries it requested. The cache is reused while its graph
generation remains current and rebuilt on demand after structural mutation; it
is never incrementally maintained as permanent graph structure. The traversal
and rewrite contract is specified in
[JIT IR Graph Rewrites](jit-ir-graph-rewrites.md).

Compilation failure follows the detailed contract in
[JIT Instruction Representation](jit-instruction-representation.md): structural
invariant violations diagnose and hard-assert, while allocation or resource
failure abandons the arena-backed compilation and continues in the interpreter.
The enclosing session unwinds retained constants and normally destroyed state;
generated code, dependencies, assumptions, and cache entries become persistent
only at final successful publication. Editor rollback is not required after an
aborted compilation.

Inferred types, proven-absent effects, and other derived knowledge are not fields on
the physical instruction. Concrete phase-owned metadata objects index
instructions or typed results, for example:

```text
Semantic ProgramValueRef -> ValueFacts
Core Instruction*        -> ProvenAbsentEffects
PartitionId     -> ConditionalFacts
ProgramValueRef -> CorrelatedEvidence
```

Each metadata object has its own graph-level and generation contract. Mutable
analysis updates its private storage and publishes a frozen view;
transformations consume only a view whose source generation still matches the
graph. Structural mutation makes old views stale. The baseline is to invalidate
and recompute any analysis a later pass still needs; mutation-aware preservation
and incremental recomputation can be added later when broad invalidation is
measurably too expensive. Metadata is discarded when no later phase consumes it.
Immutable instruction-kind metadata supplies both a `MustEffects` lower bound
and a conservative `MayEffects` upper bound. Selecting a genuinely different
semantic operation, such as replacing a generic call with recognized float
addition, creates a replacement instruction with the corresponding kind
contract.

### Mutable CFG and control-flow-producing lowering

The bytecode CFG is an initial scaffold, not a fixed graph. Every lowering level
may introduce, remove, clone, and restructure control flow. This is required for
polymorphic ICs, overflow exits, inlining, out-of-line calls and reification,
future exception handling, and machine slow paths.

A lowering may replace one operation with an arbitrary CFG region. The CFG API
therefore needs first-class operations to:

- split a block at an instruction;
- insert branches and joins;
- add, remove, and redirect edges;
- update block parameters and edge arguments;
- clone and splice regions;
- attach bytecode origins and Snapshot references to new exits.

Major representation boundaries normally build fresh destination CFGs.
Semantic-to-Core lowering leaves its source graph intact and naturally
supports one-to-region translation. A backend that chooses Machine IR follows
the same rule; a direct backend instead records locations, edge moves, and
emission metadata in side tables. Optimizations within one IR may use an
in-place CFG editor.

Deoptimization exits must remain visible to correctness and frame-state
analysis, but need not be block successors in the CFG used for loops
and dominance. An ordered guard may own an explicit non-returning side exit and
Snapshot operand while successful execution falls through.

### Analysis invalidation

Dominance, loop structure, reverse postorder, propagated facts, refined effects,
and partition state are derived from the current IR. Each concrete analysis
records the graph generation from which it was derived.

A CFG has an IR mutation generation, and cached analyses record their source
generation. Inserting, removing, or replacing an instruction; changing a
definition or operand; associating a new partition anchor through instruction
replacement; or structurally editing the CFG advances the applicable
generation and makes prior frozen views stale. The baseline is broad
invalidation and recomputation before the next consuming pass. A later analysis
may consume editor mutation descriptions to preserve and cheaply update facts
whose validity is locally provable, but this is optional optimization
machinery. It publishes a new frozen view only after its state is valid for the
new graph generation.
Querying a stale frozen view asserts. The owning analysis must explicitly update
or recompute and publish a current view before a pass resumes querying it.
Passes must not mutate instructions or graph structures directly.

Verification at pass boundaries should require:

- every fixed and variable operand slot to match the `OperandClass`, def
  representation, and layout declared for its instruction kind, and every
  attribute slot to match its declared
  `AttributeClass` and layout;
- every result reference to match its def's intrinsic `ResultClass`, and no
  result reference to be formed from a value-less
  instruction;
- exactly one live definition for every reachable SSA value;
- for every phase carrying semantic type analysis, exactly one guaranteed
  static fact for each SSA value in that analysis's domain, compatible with its
  producing instruction;
- exactly one intrinsic `ValueRepresentation` for each Core `ProgramValueRef`,
  with all representation changes expressed by explicit conversion
  instructions;
- every allocated register or spill location to belong to a class compatible
  with that representation and the relevant `AllocationConstraints`;
- every specialized use of a guard result to be dominated by that result's
  definition;
- every mutable-shape-sensitive use to consume a current receiver version whose
  shape observation has not been superseded or invalidated through an alias;
- every deoptimizing exit to consume one live Snapshot with a complete resume
  state for every active logical frame;
- every SSA value transitively named by that Snapshot to be defined and
  available at the consuming exit, even when it is dead on normal control flow;
- every Snapshot recovery action to accept the representation of its operands
  and produce the interpreter representation required by its destination;
- every sunk boxing action to have no remaining normal use, and every logical
  alias of its result within one Snapshot to share one recovery-local box;
- every block edge to supply exactly one argument of the required kind and
  representation for each destination block parameter;
- every edge argument definition to dominate that edge;
- every block parameter to be owned by exactly one reachable block;
- every consumed analysis result to match the current IR generation.

The optional Semantic frontend adds its own fact and partition verification as
defined in its design document.

Narrow preservation declarations or incremental maintenance may be added later
if broad invalidation is measurably expensive.

### Optional semantic facts

The direct-to-Core compiler does not require a general Python type lattice.
Value-refining guards create SSA results with narrowed guaranteed types, and
ordinary dominance controls their availability. Non-value guards constrain
control and effects without manufacturing a Python value.

If higher-effort inference is implemented, Semantic-to-Core lowering consumes
`ValueFacts`, evidence, and partition identities without translating them into
a second Python type system. Core realizes only the distinctions demanded by
executable lowering; it does not retain the whole Semantic value-analysis
attachment unless a concrete later Core pass requires an explicitly designed
Core attachment. Semantic program-value references remain representation-free;
Semantic-to-Core lowering creates fresh Core defs with intrinsic
`ValueRepresentation`s. Backend register classes and assigned locations remain
separate from those target-independent Core representations.

The complete optional design is in
[Semantic IR and Specialization](jit-semantic-ir-and-specialization.md).

## Core IR

### Expanding semantic operations

Core construction breaks an atomic bytecode operation into checks and its
selected action. It may consume an operation preserved in Semantic IR or lower
directly from decoded bytecode and its IC snapshot. For example:

```text
Add
    -> Snapshot(entry frame state)
    -> ShapeKeyCheck
    -> ShapeKeyCheck
    -> ValidityCellCheck          # when required
    -> AddSMI | recognized operation | TrustedFunctionCall | PythonFunctionCall
```

Pre-operation checks have no Python-visible side effects. Only the selected
action may perform the operation's effects. Failed checks return to the
original bytecode through their shared Snapshot so the interpreter can run the
generic protocol.

An action may have its own speculative exit. SMI overflow, for example, returns
to the original bytecode, which may create a heap integer or select another
Python path. The action and all guards required to justify it remain one
semantic unit.

Value-refining checks return narrowed SSA values. A specialized action consumes
those results, making its dependence on the successful guards an ordinary SSA
dependency:

```text
%lhs_smi: Smi = ShapeKeyCheck %lhs, Smi
%rhs_smi: Smi = ShapeKeyCheck %rhs, Smi
%result: Smi = AddSMI %lhs_smi, %rhs_smi
```

The checks retain their original bytecode PCs and bailout states. A check can
replace a later equivalent check only when its narrowed result dominates the
later use and remains valid across the intervening effects.

### Trusted handler descriptors

Selected ICs name trusted native handlers. Recognition is explicit and
semantic rather than opcode-based:

```text
trusted handler pointer + arity
    -> runtime-neutral semantic descriptor
    -> Core lowering
```

The owning runtime type declares the handler's operand convention, coercion case,
and result kind. Core maps selected descriptors to specialized operations.
Unknown handlers remain generic trusted calls, and a recognized handler retains
every shape and validity predicate required by its IC. During the main-codegen
bring-up, every trusted native handler uses the full trusted-handler
`MayEffects` envelope and empty `MustEffects`; this deliberately avoids relying
on per-handler precision before instruction selection, lowering, and side exits
are landed. A handler that returns `Value::exception_marker()` uses the
pending-exception handoff path described by the commit-boundary rules. Later
effect analysis may prove individual effects absent for resolved handlers when
optimization needs reordering precision.

Descriptor-sensitive operator paths are deferred with the current interpreter
surface. A candidate requiring descriptor dispatch, unusual callable adaptation,
or another not-yet-cacheable call shape remains a generic interpreter path until
that descriptor behavior exists in the interpreter and has an explicit JIT
lowering contract.

### Expanding attribute mutations

`StoreAttr` and `DelAttr` are semantic operations. Core construction inspects
the snapshotted mutation IC and lowers them to the operation selected by its
`AttributeMutationPlan`:

```text
StoreAttr -> StoreExisting | AddOwnProperty | ChangeClass
DelAttr   -> DeleteOwnProperty
```

The IC supplies the required input `receiver_shape`, lookup validity cell,
storage location, and mutation kind. `AddOwnProperty` and
`DeleteOwnProperty` also carry the plan's explicit `next_shape`; the compiler
does not need to infer the transition from a later IC.

`StoreExisting` does not change the receiver shape and need not create a new
receiver SSA value. Shape-changing operations produce a successor receiver:

```text
%receiver_s0: Shape<S0> = ShapeKeyCheck %receiver, S0

%receiver_s1: Shape<S1> = AddOwnProperty(
    %receiver_s0, %stored_value, location, next_shape=S1)

%receiver_s2: Shape<S2> = DeleteOwnProperty(
    %receiver_s1, location, next_shape=S2)
```

The input and output receivers contain the same object pointer. Their distinct
SSA identities describe that mutable object before and after the known shape
transition. `ChangeClass` likewise creates a successor receiver, but unless its
selected semantics provide an exact result shape, that result has an unknown
mutable-object shape or the lowering deoptimizes instead.

When a receiver comes from an interpreter slot, construction updates that
slot's current SSA binding to the successor. Bindings known to contain exactly
the same input `ProgramValueRef` may be updated together. Unknown heap aliases
remain conservative.

Shape-sensitive uses of mutable objects must consume the current shape-bearing
version. A shape-changing operation supersedes its input receiver's mutable
shape refinement. Verification and effect analysis must prevent a rewrite from
using the old shape-bearing value after the transition. Operations that may
change the shape through an unknown alias invalidate affected mutable-shape
refinements and require a new guard.

An operation that may change a mutable shape without describing the resulting
transition weakens each affected live receiver value. `WeakenShape` is a
zero-code SSA operation that preserves the runtime `Value` bits while replacing
an exact mutable-shape guarantee with a broader guaranteed type. When optional
type evidence is active, its side table may retain the old shape as likely
persistence evidence:

```text
%self_s1: Shape<S1> = AddOwnProperty(
    %self, %a, location, next_shape=S1)

StoreExisting %self_s1, %incremented, location
    # No guard: no intervening shape clobber.

%method = LoadMethod %self_s1, method_plan
%result = PythonFunctionCall %method, %self_s1
%self_after: Object = WeakenShape %self_s1
    # likely Shape<S1>, but no longer guaranteed

%self_s1_again: Shape<S1> = ShapeKeyCheck %self_after, S1
StoreExisting %self_s1_again, %multiplied, location
```

The call does not change `%self_s1`'s static type. Its shape-clobbering effect
ends the region in which that refined receiver may be used for shape-sensitive
operations, and construction rebinds subsequent interpreter locations to the
weakened successor. Bindings containing the same exact `ProgramValueRef` share
that successor. A general Python call conservatively weakens every live
mutable-shape-refined value that remains live afterward; constant values and
lifetime-stable shapes survive unchanged.

`WeakenShape` does not claim that the shape changed. A direct compiler may
discard the optional prediction and let the next use select its recorded
IC case normally. With evidence analysis, likely `S1` is a cheap persistence
prediction used only when the use site's IC permits it. More applicable
use-site or inline-context evidence may displace that prediction without
changing `%self_after`'s guaranteed `Object` type.

A later optimization may recognize an uninterrupted canonical transition
chain, particularly in `__init__`:

```text
AddOwnProperty S0 -> S1, a
AddOwnProperty S1 -> S2, b
AddOwnProperty S2 -> S3, c
```

and fuse it into one initialization operation that installs the properties and
produces `Shape<S3>`. This is deferred. It is legal only when intermediate
shapes cannot be observed, value evaluation order is preserved, all guards run
before mutation begins, and the commit sequence cannot safepoint, fail, or
deoptimize partway through. Reference-counting and future write-barrier effects
must remain equivalent.

### Optional partition realization

When optional Semantic inference retains a correlated union, Core lowering
realizes it only when a type-sensitive use needs distinct executable
operations. Realization creates ordinary branches, narrowed SSA values, and
block parameters; union-transparent uses leave the partition latent.
Existing predecessor edges may already provide the required split.

Partition identity, recursion, profitability, and demand propagation are owned
by [Semantic IR and Specialization](jit-semantic-ir-and-specialization.md).

### Operation effects and dependencies

An effect summary says what an operation may change. A dependency summary says
what it observes or assumes. Moving an operation is legal only when its
dependencies do not intersect crossed effects and when commit and control
ordering remain valid.

Relevant properties include whether an operation:

- reads or writes memory;
- may change object shapes, including through aliases;
- may invalidate lookup assumptions or validity cells;
- may call Python;
- may allocate, reclaim, or reach a safepoint;
- may raise or deoptimize;
- has an irreversible Python-visible effect;
- is pure arithmetic.

Operation definitions provide precise defaults where possible.
`ShapeKeyCheck`, for example, has a standard dependency and deoptimization
shape. Python calls, unknown operations, and recognized trusted native handlers
begin maximally conservative. Trusted-handler descriptors may name the selected
operation and result shape without weakening effects; later analysis records
effects proven absent when optimization needs that precision.

The authoritative instruction schema declares each kind's `MustEffects` and
`MayEffects` bounds. New instructions default to the conservative `MayEffects`
envelope, and passes may use that envelope directly. When an optimization needs
more precision, a concrete phase-owned analysis can prove and publish a current
per-instruction `ProvenAbsentEffects` set; clients derive effective effects as
`MayEffects - ProvenAbsentEffects`. The bound checks, stale-view behavior, and
prohibition on inferring purity from `MustEffects` alone are specified in
[JIT Instruction Representation](jit-instruction-representation.md).
Specialization that selects a different semantic operation constructs a
replacement instruction and therefore adopts the new kind's bounds.

Effect implications are centralized. `MayCallPython`, for example, implies
broad heap access, possible shape mutation, validity invalidation, raising, and
safepoint behavior. Unless the call has been eliminated by inlining or replaced
by a certified no-safepoint entry, it also requires the active safepoint policy:
continuing canonical publication initially, or a precise compiled safepoint map
later. An omitted effect is a correctness bug, not merely a missed
optimization.

Core defines motion in terms of one directional adjacent-swap predicate. Given
two consecutive instructions `A` then `B`, `can_swap(A, B)` asks whether their
order may become `B` then `A`:

```text
can_swap(A, B) =
    structurally_movable(A, B)
    and preserves_ssa(A, B)
    and effects_commute(A, B)
    and preserves_recovery(A, B)
    and preserves_safepoint_roots(A, B)

effects_commute(A, B) =
    not conflicts(EffectiveEffects(A), Dependencies(B))
    and not conflicts(EffectiveEffects(B), Dependencies(A))
    and not noncommuting(EffectiveEffects(A), EffectiveEffects(B))
```

`conflicts` and `noncommuting` are centralized relations over the effect and
dependency taxonomy after effect implications have been expanded. They are not
raw bit-set intersection. An aliasing heap write conflicts with a heap read or
write; shape mutation conflicts with shape dependencies; validity invalidation
conflicts with validity-cell dependencies; and raising, deoptimizing, or
leaving JIT conflicts with visible writes and with other observable exits whose
order it could change. Coarse or unknown aliases conflict conservatively.

The other clauses have equally concrete meanings:

- `structurally_movable` requires both instructions to be in the same block and
  excludes block parameters, terminators, Snapshot definitions, and any other
  position-pinned instruction.
- `preserves_ssa` rejects moving a use before its definition. Snapshot-expanded
  operands count as transitive uses at each Snapshot use.
- `preserves_recovery` requires every guard or exit to remain in the same
  replay-valid region, every captured value to dominate its use, and no
  committed Python-visible effect to cross the exit.
- `preserves_safepoint_roots` rejects a swap that makes a tagged managed value
  live across a safepoint unless the active root-publication policy can
  represent that value there.

Two guards or other side exits are noncommuting by default, even if their only
effective operation is deoptimization: reversing them may change which
Snapshot and bytecode continuation wins. A future optimization may commute
them only with an explicit proof that their exits are equivalent.

Longer-range motion is legal only when the same instruction can be swapped
across every intervening instruction in order. Hoisting, sinking, CSE, and guard
elimination therefore share this predicate rather than maintaining independent
notions of safe motion. A pass may use conservative kind `MayEffects` or a
current generation-checked effect analysis; it may never use stale refinement
metadata.

As a possible future optimization for generational GC, Core IR may distinguish
establishing remembered-set coverage for an object from renewing that coverage
after a possible collection. `EnsureRememberedIfOld` is idempotent within one
GC epoch, so dominated instances for the same object identity can be removed.
After a safepoint, previously established coverage can be renewed immediately
and guarded by a shared runtime epoch comparison: when no collection occurred,
all renewals are skipped; when the epoch changed, they execute before later
barrier-free stores. First-time establishment remains unconditional, and shape
transitions that produce new SSA receiver values must preserve the underlying
object identity for this analysis.

### Shape facts

Shape facts have different lifetimes.

**Inline values.** For an SMI or another inline value, the shape follows from
the bits. It remains valid while the SSA value is unchanged.

**Mutable-shape heap values.** General objects may change shape through property
mutation, supported `__class__` assignment, or aliases. Dominance alone is
insufficient: a shape fact can cross only operations proven not to change that
object's shape, including indirectly. A recognized `AddOwnProperty`,
`DeleteOwnProperty`, or `ChangeClass` consumes the current receiver version and
produces its successor. Exact `next_shape` metadata gives add and delete
successors a guaranteed shape. An imprecise mutation produces a weakened
unknown-shape successor, or deoptimizes when the compiler cannot represent the
required clobber safely.

**Stable-shape heap values.** Some exact heap values, such as tuples, have
lifetime-stable instance shapes. Once proved, that fact survives calls for the
unchanged value. Stability must be a runtime invariant, not merely the absence
of an ordinary transition flag.

Shape stability does not imply lookup stability. A tuple instance can retain
its shape while mutation of its class or MRO invalidates a cached lookup. Shape
and validity facts remain independent.

### Validity-cell facts

Validity cells capture assumptions about non-local mutable runtime state. A
check can cross only operations whose effects prove that they cannot trip the
cell. Python calls, arbitrary helpers, and possible non-local mutation are
barriers unless a stronger semantic descriptor proves otherwise. The bring-up
optimizer begins with local redundancy elimination rather than assuming an
elaborate memory model.

### Guard-result optimization

A value guard produces the narrowed runtime value consumed by specialized
operations. Redundant-guard elimination therefore uses ordinary SSA
replacement: a later guard can be removed when an equivalent earlier guard
result dominates all rewritten uses and no intervening effect invalidates the
observed property.

The retained guard keeps its own bytecode bailout location. Equivalent guards
in sibling blocks do not dominate one another. Hoisting them is a separate CFG
transformation that must choose a legal bailout state and prove that replaying
crossed work in the interpreter is safe.

Inline and lifetime-stable properties need only the guarded value and
dominance. Mutable heap properties additionally depend on relevant effect
state, even though the narrowed value has one fixed static type. The verifier
rejects a shape-sensitive use of a superseded mutable receiver version or one
whose observation has been invalidated through a possible alias.

Validity-cell checks do not refine a Python value. They remain value-less guard
operations whose reuse is governed by dominance, effect dependencies, and
their bailout state.

## Backend Lowering and Value Representation

### Embedded runtime references

Compiled code distinguishes stable compiler-facing metadata from movable
managed constants. `Shape` and `ValidityCell` objects are allocated from
non-moving stable pools. A backend may embed their addresses directly in
machine instructions, but every such pointer must also appear in the owning
compiled code object's stable-metadata array so the GC can keep the pool entry
alive and trace any managed references reachable through it.

Every Python `Value` constant whose bits identify a managed pointer resides in
the compiled code unit's constant pool. The pool is a separate stable-addressed
array of naturally aligned, GC-rewritten `Value` slots residing with the machine
code. Machine code never embeds a pointer-valued `Value` directly; it loads the
corresponding slot through a PC-relative reference. The slot address remains
fixed relative to the code while collection may rewrite its contents. Immediate
non-pointer `Value` bit patterns, such as SMIs and singleton immediates, may be
embedded directly when the target instruction accepts that immediate shape, or
submitted to the emitter's pool when a load is more profitable.
They are never forced into the pool merely because they are `Value`s.
Core IR retains a pointer-valued constant directly as a `ValueConstant`
attribute on `Const`, and the compilation session retains the referenced object
while the graph exists. Snapshots refer to the resulting `Const` definition.
Only constants that survive to emission are submitted
to `MachineCodeEmitter::add_value_to_constant_pool()`, which deduplicates their
raw `Value` bit patterns and returns final `ValuePoolEntry` handles. Core never
observes those handles or pool indices.

Code generation records both reference classes during emission. Verification
rejects a directly embedded Shape or ValidityCell pointer missing from the
stable-metadata array, any pointer-valued `Value` embedded as an instruction
immediate, or any pool load that lacks an emitter-owned `Value`. The
graph-building and rewrite APIs require pointer constants to be registered with
the session before construction. Backend relocation metadata remains for code
targets and native symbols rather than managed object movement.

A heap `JitCodeObject` owns the stable code-cache allocation, final entry
address, and precisely sized `Value` pool. It has its own native layout and
exposes the external pool as mutable `Value` slots to the collector, allowing a
moving collection to trace and rewrite them without decoding machine code. A
`CodeObject` may publish a nullable atomic reference to it after all code, pool
slots, and metadata are initialized.

The compilation session remains alive through that construction and
publication step. Its retains protect every direct compiler reference until the
`JitCodeObject` scanner is responsible for the initialized pool; only then may
the session be destroyed. Initial generated code is installed once and remains
alive until its owning heap objects and code-cache lifetime policy permit
reclamation.

### Snapshots, recovery, and future root maps

A Core `Snapshot` is the authoritative logical description of
interpreter-visible state. It already names the resume state, active logical
frame chain, program values, and boxing or reification actions needed to leave
compiled execution. Constants appear through ordinary `Const` defs. The
backend consumes the Snapshot directly when generating recovery and may
rematerialize those defs; managed values then use the emitter-owned pool.

After allocation, recovery planning combines the Snapshot with two physical
inputs:

```text
Snapshot + LocationAssignments + HomeState -> RecoveryPlan
```

Location assignments resolve each captured `ProgramValueRef` to its register,
spill, or canonical slot at the exit. `HomeState` identifies canonical frame
homes that already contain the required value. The resulting `RecoveryPlan`
contains the parallel assignments, inline-constant materialization, traced-pool
loads, F64 boxing, and reification operations needed before interpreter handoff.
This adds physical execution choices but no second semantic description of the
Snapshot.

Precise GC root maps are a separate future backend projection. They select all
managed values live at a continuing safepoint, including any compiler-only
temporaries, and resolve them through the same post-allocation location
vocabulary. They omit non-root F64 values, inline immediates, boxing, and
interpreter resume state. The initial compiler instead uses canonical frame
publication and emits no general root-map artifact. Its only post-allocation
recovery work is direct expansion of Snapshots into generated recovery
sequences, with identical sequences optionally interned as `RecoveryPlan`s.

Compiled code and its metadata remain alive as one code object. Safepoint lookup
from a compiled PC is deterministic, and a call safepoint can be identified
from the compiled caller return PC while walking a suspended callee chain. Code
retirement must preserve every active return continuation; the initial immortal
code policy is specified by the bring-up plan.

### Generated side exits and recovery plans

Each Core IR failure retains an explicit non-returning exit consuming a
`SnapshotRef` until the backend has determined the location of every value
needed for recovery. A backend may carry the exit through Machine IR or consume
it directly from Core IR. Side-exit frame publication is target-specific backend
work, not an effectful Core instruction. The emitter may generate the main path
first, attach labels and Snapshot state for side exits, and emit the recovery
tails at the end of the code unit using architecture-specific register juggling
and scratch-state conventions. Post-allocation exit expansion combines three
inputs:

```text
logical Snapshot and FrameState:
    resume state
    active frame chain
    (frame instance, canonical slot) -> Direct(ProgramValueRef)
                                      | RecoveryAction(ProgramValueRef, ...)
    innermost accumulator            -> Direct(ProgramValueRef)
                                      | RecoveryAction(ProgramValueRef, ...)
                                      | Dead

machine location state at the exit:
    ProgramValueRef -> register | spill | canonical slot
                   | inline constant

canonical HomeState:
    (frame instance, canonical slot) -> ProgramValueRef currently stored there
```

The resulting recovery plan is the parallel assignment needed to make logical
and synchronized state agree. It includes dirty canonical-slot writes, the
accumulator source, and any boxing or reification actions captured by the
Snapshot. Resume-specific bytecode PCs, code objects, exit kinds, and return
metadata belong to the resume-state stub instead. Active inline frames already
have canonical backing regions and do not require allocation or layout
reconstruction. Machine liveness at the exit includes the transitive closure of
SSA operands named by the Snapshot, whether or not normal compiled control flow
uses them afterward.

Location assignment and `HomeState` answer different questions. Location
assignment says where a `ProgramValueRef` can be obtained at the exit.
`HomeState` records which `ProgramValueRef`, if any, is already synchronized in
each canonical home. Changing the builder's slot binding does not update
`HomeState`; only an explicit publication store does. Exit planning skips a
destination whose home already contains the Snapshot's desired value and
otherwise obtains the source from its allocated location and evaluates any
captured recovery action before publishing it.

These tables may be compiler inputs to generated cold code or serialized
metadata interpreted at runtime. Their declarative form deliberately supports
both generated recovery and later deoptimization translations.

Side-exit code is factored into independently shareable value-recovery and
resume-state parts:

```text
guard or speculative failure
    -> exit-selector stub
    -> shared recovery-plan block
    -> resume-state stub
    -> common interpreter handoff
```

A resume state contains more than a numeric bytecode PC:

```text
ResumeState {
    CodeObject
    bytecode pc
    inline instance
    exit kind              # retry, unsupported bytecode, post-commit, ...
}
```

The small selector for a `(ResumeStateId, RecoveryPlanId)` pair arranges the
eventual resume target in the dedicated exit scratch state and jumps to the
shared recovery block. After synchronizing values, that block transfers to the
selected resume-state stub. All exits returning to the same logical bytecode
state may share the resume stub, while exits with identical post-allocation
value recovery may share the recovery block even when their bytecode PCs or
exit kinds differ.

A recovery plan is interned from a canonical, deterministic signature containing
its destination homes, physical or rematerialized sources, representations,
accumulator action, and reification requirements. Resume-specific frame
metadata is deliberately excluded.
Interning assigns a typed `RecoveryPlanId`; it never depends on pointer hashes
or hash-table iteration order. Identical plans share one emitted block, and all
resume-state stubs tail into a common interpreter-dispatch handoff after
installing their code objects, bytecode PCs, return metadata, and exit kind.

Canonical-slot writes are parallel assignments. A source home may be overwritten
before its old value has been copied elsewhere, so exit expansion uses ordinary
parallel-copy scheduling.

Each backend provides one dedicated exit scratch general-purpose register. It is
excluded from ordinary allocation, never appears as a live recovery source, and
may be clobbered by resume stubs and recovery blocks. It is available for
constructing bytecode PCs, breaking parallel-copy cycles, forming addresses and
constants, and reaching the final dispatcher. The AArch64 backend may reserve
this register globally; avoiding that reservation is unlikely to justify added
complexity in cold exits on a register-rich target.

### Continuing call state

Calls use the same logical-versus-synchronized frame analysis as exits but not
the same non-returning code shape. The selected lowering determines the
required boundary strength:

```text
cannot safepoint and must return to JIT
    no publication

may safepoint but must return to JIT
    publish canonical managed roots

may throw or otherwise leave JIT without a generated recovery continuation
    materialize complete canonical frame state before entry
```

Root publication stores every live tagged managed value in the canonical slots
seen by the initial stack scanner. It clears a canonical slot to a non-pointer
sentinel when stale contents could otherwise be mistaken for a root, and clears
the dead input accumulator's published slot rather than preserving its value.
It does not box F64 values or materialize constants. Full eager materialization
additionally stores constants and boxes any F64
value needed by interpreter-visible frame state. It covers the complete
outer-to-inner active frame chain, but the call-boundary Snapshot marks the
input accumulator dead.

Initially, calls that may throw or leave JIT use full eager materialization.
This is deliberately conservative while those outcomes can bypass a generated
continuation. The intended call convention eventually returns every outcome to
a generated JIT continuation. That continuation dispatches a normal result back
to compiled code, enters a compiled exception handler when the active exception
table has one, or expands the same Snapshot into exact recovery only when the
exception or other exit must return to the interpreter. Once that convention is
available, the normal pre-call path needs only root publication.

The planner may share location, frame-difference, and parallel-copy machinery
with exit recovery. Publication instructions themselves remain on the normal
path because execution returns to compiled code. They cannot be delegated to a
deduplicated cold recovery tail. Full eager materialization also remains on the
normal path until the return-through-JIT convention is available.

Eager F64 boxing may allocate. Unboxed values must not be enabled across such a
boundary until the boxing path can publish its pre-existing roots and handle
allocation failure without losing the Snapshot state. This does not block the
initial tagged-only compiler.

### Tagged `Value` baseline

The existing `Value` representation is the initial JIT representation:

- heap pointers stay tagged and can be dereferenced in their existing form;
- SMIs remain shifted left by five bits;
- SMI addition and subtraction usually operate directly on encoded values and
  use native overflow flags;
- multiplication and address indexing shift only where required;
- tagged values move directly between registers and canonical frame homes.

The initial JIT uses tagged `Value`s exclusively. It does not require general
unboxing to execute ordinary compiled code.

### One representation and location per Core SSA value

Every Core `ProgramValueRef` has exactly one immutable
`ValueRepresentation`. The first schema supports `TaggedValue` and `F64`,
although bring-up produces only tagged values until unboxing is implemented.
Representation is an intrinsic def and operand contract declared by
`instruction.def`, not an analysis or register-allocation attachment. Semantic
IR, when present, keeps representation-free references; its lowering creates
the represented Core values. Boxing, unboxing, and any other representation
changes are explicit Core SSA instructions that produce new values:

```text
%boxed: Tagged<Float>
%raw:    F64           = UnboxF64 %boxed
%sum:    F64           = AddF64 %raw, %other_raw
%result: Tagged<Float> = BoxF64 %sum
```

Representation also determines the value's default backend register class and
spill layout. On AArch64 a tagged `Value` normally occupies an `X` register and
an unboxed `F64` a scalar lane of a NEON/FP register; x86-64 uses its
corresponding general-purpose and XMM classes. These target classes belong to
the backend, while `TaggedValue` and `F64` remain common Core
representations.

Several representations of one logical Python value may therefore coexist, but
they are separate SSA values connected by visible conversion operations. This
lets ordinary use lists, dominance, CSE, and liveness describe exactly which
form each use requires. Optimizations may eliminate inverse boxing and
unboxing pairs only in the identity-safe direction: an
`UnboxF64(BoxF64(%raw))` may simplify to `%raw` when the intermediate box
has no other use and removing its allocation has no observable effect. The
optimizer may thereby connect arithmetic directly in unboxed form.

Core block parameters also have one representation. Every incoming edge must
supply the representation fixed by the concrete parameter kind, inserting an
explicit conversion in the predecessor or edge block when necessary.

At each machine-code position, a live SSA value has one authoritative allocated
location. Live-range splitting may move that value between a register, spill,
canonical slot, or constant location over time. A synchronized canonical home
or temporary machine copy does not give the same `ProgramValueRef` a second
allocator-owned location. If tagged and unboxed forms must both remain live,
each has its own `ProgramValueRef` and location.

Machine liveness remains separate from logical availability. A function
argument is defined at entry and may initially use its canonical argument slot
as its authoritative location until a guard or use makes loading it profitable.

### Future unboxed floats and reification

Unboxed floats are an advanced optimization, not an initial requirement. An
`UnboxF64` of an existing tagged float produces a separate `F64` SSA value
while the original tagged value preserves the existing object identity. If
interpreter state still denotes that object, its Snapshot entry uses the
original tagged `ProgramValueRef`; the compiler must not discard it and later
manufacture a replacement box from the unboxed value.

`BoxF64(UnboxF64(%boxed))` must not simplify to `%boxed`: the explicit
boxing operation creates a new Python object, and reusing the input box would
change observable identity. Only `UnboxF64(BoxF64(%raw))` cancels, and only
when the newly allocated box has no other use or observable effect.

A new unboxed arithmetic result has no box until compiled execution or recovery
needs one. A normal-path `BoxF64` explicitly produces the tagged SSA value
used by later compiled operations. It may be sunk into Snapshots only when it
has no normal uses, deferring its allocation has no observable effect, and
every affected Snapshot preserves one shared recovery result for all logical
homes that require the object.

After sinking, each Snapshot captures a boxing recovery action over the unboxed
operand. The backend emits that action on the cold exit path and keeps the hot
path unboxed. Separate exits may contain separate recipes because only one exit
can be taken in an execution; within one exit, every alias of the virtual result
must use the same recovery-local box.

If one unboxed result appears in multiple bytecode slots or inlined frames, its
recovery actions must allocate one box and place that same `Value` everywhere.
Boxing each occurrence independently would break `is`. Equivalent normal-path
boxing operations may likewise be commoned only when doing so preserves the
required Python object identity and effects.

Boxing or reification allocation may initially be treated as infallible because
the runtime requests a reclamation safepoint before memory exhaustion. The
first JIT needs no unboxed-float instructions or recovery boxing implementation;
its IR must only preserve the representation, location, and Snapshot rules that
allow them to be added later.

## Inlined Frame Backing and Deoptimization

Inlining preserves the Python frame stack structurally. The compiled outer
frame reserves stable, contiguous backing regions for its active inline depth:

```text
outer frame A
inline frame B
inline frame C
...
```

Each inline instance has a fixed base offset and the ordinary interpreter frame
layout for its arguments, locals, temporaries, outgoing arguments, return FP,
return PC, return `CodeObject`, and other required metadata. The managed frame
pointer always identifies the innermost active logical frame.

Inline entry advances FP to the preallocated region and initializes the
structural callee and return metadata, including both interpreted and compiled
continuations. Slot values may remain represented only by SSA values in machine
registers until publication. Inline return places the result in the accumulator,
restores the recorded return FP, and branches to the compiled caller
continuation. A real call made from an inline frame uses the same return ABI as
any other call: the callee pops exactly its own frame, leaves its remaining
metadata addressable from the backend-defined post-return stack position, and
returns to the inline frame's compiled continuation. It does not require a
depth-specific or double-pop return thunk.

Deoptimization distinguishes:

```text
logical frame state:
    the SSA value each interpreter-visible location denotes

synchronized frame state:
    the Value currently committed to each canonical frame home
```

A bailout inside an inlined callee may expose several bytecode frames. Once the
callee has performed effects, the caller's call bytecode generally cannot be
retried. Exit machinery therefore synchronizes every dirty active frame region,
sets the appropriate interpreted PCs and return metadata, reifies virtual
objects once per semantic identity, publishes the innermost accumulator, and
hands the already-backed frame chain to the interpreter.

Outer and inner regions may both remain stale until that boundary. This does
not require frame allocation or arbitrary per-exit layouts: only value
synchronization and metadata finalization are exit-specific. Inactive sibling
inline sites may eventually share backing regions when their lifetimes cannot
overlap. Their backing-region stack cost contributes to the inlining budget.

## End-to-End Example

### Direct Core compilation: monomorphic Add

The direct path requires neither Semantic IR nor type inference:

1. The shared frontend decodes `Add`, records its bytecode PC and logical frame
   state, and snapshots its monomorphic IC case.
2. The trusted handler descriptor identifies the selected semantic action and
   the predicates that justify it.
3. Core IR construction captures the current `BuilderContext` as one Snapshot
   value shared by the pre-effect shape checks, validity checks, and overflow
   exit, then emits the narrowed results and selected action.
4. Snapshot operands make every interpreter-visible value required by the
   failed continuation live and recoverable at those exits.
5. Failed pre-effect checks return to the original `Add`; committed exits, when
   present, use a distinct post-effect Snapshot.
6. Core optimization may remove a redundant dominating check when effects,
   Snapshot availability, and replay semantics permit it.
7. The target backend assigns locations, combines each Snapshot with physical
   and canonical-home state, interns recovery plans, and encodes the function,
   optionally through Machine IR.

Unsupported or polymorphic cases may conservatively call Python or return to
the interpreter. The direct compiler need not infer a type merely to reproduce
an IC specialization that already names its predicates and action.

The corresponding higher-effort polymorphic example is in
[Semantic IR and Specialization](jit-semantic-ir-and-specialization.md).

## Deliberately Open Architectural Questions

### Core IR and optimization

- validation of the 48-byte fixed-record and side-data experiment specified in
  [JIT Instruction Representation](jit-instruction-representation.md);
- concrete implementations of the accepted graph-builder and mutation-editor
  contracts;
- optimizer and register allocator organization;
- whether a narrow pass benefits from a temporary graph representation;
- when direct backend side tables have become an implicit Machine IR and should
  be replaced by an explicit backend-local representation;
- when broad analysis invalidation becomes expensive enough to justify
  preservation or incremental maintenance;
- how much construction machinery direct Core and optional Semantic compilation
  should share without becoming independent compilers.

### Effects and runtime assumptions

- the precise effect taxonomy and alias model;
- the runtime classification of stable-shape values;
- how aggressively mutable-shape and validity checks can be optimized;
- compiled exception handling and cross-frame unwinding;
- final tracing, traceback, and stack-observability policy.

### Recovery, maps, and backend lifecycle

- concrete encodings for machine locations, recovery actions, safepoint maps,
  and deoptimization translations;
- recovery-plan interning and code-size policy beyond exact-plan deduplication;
- mixed-frame stack-walker contracts for frame kinds, PC lookup, unwind state,
  and callee-saved registers;
- whether certified no-safepoint entries remain useful alongside precise maps;
- active-frame-aware generated-code retirement, relocation, and reclamation.

Bring-up sequencing and temporary runtime-policy questions are tracked in the
[JIT Compiler Bring-up Plan](jit-compiler-bring-up-plan.md). Optional inference,
partition, feedback, and contextual-inlining questions are tracked in
[Semantic IR and Specialization](jit-semantic-ir-and-specialization.md).

These questions must be answered without weakening bytecode compatibility.
Every safepoint policy must provide explicit and verifiable root discovery, and
every recovery policy must reconstruct the same canonical interpreter state.

## Related Documents

- [JIT Control-Flow Graph](jit-control-flow-graph.md)
- [JIT Register Allocation](jit-register-allocation.md)
- [JIT Machine-Code Emission](jit-machine-code-emission.md)
- [JIT Code Cache and Publication](jit-code-cache.md)
- [JIT Compiler Bring-up Plan](jit-compiler-bring-up-plan.md)
- [Semantic IR and Specialization](jit-semantic-ir-and-specialization.md)
- [Function Calling Convention](function-calling-convention.md)
- [Native/Managed Boundary Contracts](native-managed-boundaries.md)
- [Decision Log](decision-log.md)
