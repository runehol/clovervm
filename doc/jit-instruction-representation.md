# JIT Instruction Representation

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Partial; the naturally aligned 48-byte record and instruction arena, schema-generated kind metadata and concrete subclass definitions, generated construction, typed CFG terminators, homogeneous PythonCall side data, annotated Snapshot captures, and basic operand traversal are implemented; detachment and editor integration remain |
| Scope | Physical instruction storage, typed instruction access, Core value representations, IR-level legality, phase metadata, effects, matching, and arena lifetime for Core and Semantic IR |
| Owning layers | The JIT instruction representation owns storage, schema-generated construction, and typed access; the bulk graph builder owns deferred-validation construction; concrete analyses own attached inferred facts and proven-absent effects; the CFG editor owns incremental structural mutation |
| Validated against | Working tree implementation and full debug test suite (2026-07-21) |
| Supersedes | The open instruction-representation alternatives in [JIT Control-Flow Graph](jit-control-flow-graph.md) and the integer-only instruction reference direction in [JIT Compiler and IR](jit-compiler-and-ir.md) |

Core IR and the optional Semantic IR use a fixed-size, type-erased
`Instruction` allocated with compilation lifetime. Instruction-result operands
are pointers, while non-dataflow attributes use their schema-declared
pointer-sized encodings. Each allocation also carries a typed serial for
deterministic identity, diagnostics, and ordering. Fieldless concrete
instruction subclasses provide kind-specific read-only access without virtual
dispatch or C++ RTTI.

The representation has deliberately different roles:

```text
Instruction
    fixed-size stored object
    stable address and serial
    instruction kind and intrinsic result class
    Core value representation when applicable
    encoded kind-specific payload

AddInstruction, CallInstruction, ...
    fieldless concrete subclasses with typed read-only accessors

SemanticValueAnalysis, CoreEffectAnalysis, ...
    concrete phase-owned metadata indexed by instruction
    attached, frozen, invalidated, recomputed, and discarded as required

typed graph-builder construction
    schema-safe allocation of intrinsically valid, unplaced instructions

bulk graph builder
    cheap append/emplace during translation and one-shot publication validation

CFG editor
    checked incremental insertion, removal, and instruction replacement
```

## Storage and Lifetime

The physical `Instruction` is a 48-byte fixed-size record: an 8-byte header and
five pointer-sized payload slots. The header contains a 32-bit serial, a 16-bit
kind, and a 16-bit operand-storage word. The high bit of the operand-storage
word says whether operands are indirect; the low 15 bits hold the total operand
count. Conceptually it contains:

```cpp
class Instruction
{
public:
    using Serial = TypedSerial<Instruction>;

    Instruction(const Instruction &) = delete;
    Instruction &operator=(const Instruction &) = delete;
    Instruction(Instruction &&) = delete;
    Instruction &operator=(Instruction &&) = delete;

    Serial serial() const;
    bool is_detached() const;
    // Requires !is_detached().
    InstructionKind kind() const;
    ResultClass result_class() const;
    // Requires a Core ProgramValue result.
    ValueRepresentation value_representation() const;
    uint16_t operand_count() const;
    bool operands_are_indirect() const;

    template<typename TypedInstruction>
    const TypedInstruction *as() const;

private:
    uint32_t serial_;
    uint16_t kind_;
    uint16_t operand_storage_;
    InstructionSlot payload_[5];
};
```

The instruction arena uses the natural alignment of `Instruction` and advances
by the 48-byte record size. An instruction reference is an ordinary, directly
dereferenceable `Instruction *`; its address carries no operand discriminator.

The physical storage tag normally represents one live `InstructionKind`. The
exact encoding may also reserve a poison tag used only after the CFG editor has
removed an instruction from a published graph. `InstructionKind` remains the
closed semantic enum generated from `src/jit/instruction.def`; the poison tag is
not an instruction kind and is not a state ordinary passes handle. `kind()`,
`result_class()`, typed conversion, and payload traversal all assert that the
tag is live. Only `serial()` and `is_detached()` remain meaningful on poisoned
storage, for diagnostics.

Instruction results, instruction operands, and instruction attributes use
distinct enum types. Ordinary operands are always instruction-result references
that participate in SSA use lists, dominance, liveness, and bulk replacement.
Constants become ordinary program values through explicit `Const`
instructions. Snapshot's annotated recovery payload is the sole
heterogeneous exception and may capture constants directly.
Attributes are semantic or structural payload such as block edges, shapes,
immediates, bytecode origins, and return PCs; they affect instruction semantics
but are not dataflow uses. The operand cases intentionally have the same numeric
values as the matching result classes, so validation can compare result
references cheaply without a mapping switch:

```cpp
enum class ResultClass : uint8_t
{
    None = 0,
    ProgramValue = 1,
    Snapshot = 2,
};

enum class OperandClass : uint8_t
{
    ProgramValue = 1,
    Snapshot = 2,
};

enum class AttributeClass : uint8_t
{
    BlockEdge = 3,
    Shape = 4,
    ShapeKey = 5,
    ValidityCell = 6,
    BytecodePC = 7,
    Immediate = 8,
    ValueConstant = 9,
};

static_assert(static_cast<uint8_t>(OperandClass::ProgramValue) ==
              static_cast<uint8_t>(ResultClass::ProgramValue));
static_assert(static_cast<uint8_t>(OperandClass::Snapshot) ==
              static_cast<uint8_t>(ResultClass::Snapshot));

constexpr bool operand_accepts_result(OperandClass operand, ResultClass result)
{
    return static_cast<uint8_t>(operand) == static_cast<uint8_t>(result);
}
```

`ResultClass` describes what an instruction may produce. `OperandClass`
describes which result class an SSA operand slot may consume. Keeping them
separate makes invalid states such as a
`BlockEdge` instruction result unrepresentable in the API, while the aligned
`ProgramValue` and `Snapshot` values keep result-operand compatibility checks
simple. `AttributeClass` describes non-dataflow payload. `BlockEdge` is an
attribute instead of an operand because CFG edge maintenance is not SSA use
tracking; the CFG still represents every control-transfer occurrence with a
first-class edge. Operand classes control structural compatibility, physical
decoding, and generic use handling. Core program-value operands apply the
additional `ValueRepresentation` constraint described below.

`Instruction::result_class()` is decoded directly from the upper bits of
`InstructionKind`; it is not another field in the record or an entry duplicated
in the metadata table. `ProgramValue` says that the instruction defines an SSA
program value, not that analysis knows its precise Python type.
Such inferred facts belong to a concrete phase-owned metadata object such as
`SemanticValueAnalysis`. Changing an instruction's result class requires
replacing the instruction with a different kind. There is no constant
`ResultClass`: `Const` produces an ordinary `ProgramValue` from its immutable
`ValueConstant` attribute. Snapshot may instead retain a constant directly as
recovery payload. Core does not decide how either form will be materialized.

Core program values have one additional intrinsic refinement:

```cpp
enum class ValueRepresentation
{
    TaggedValue,
    F64,
};
```

`ValueRepresentation` describes the target-independent encoding of a Core SSA
value. It is not an operand or result class, Python type fact, register class,
or assigned location. `Int64` or another representation is added only when an
implemented Core instruction requires it. `Address` remains backend-local unless addresses
demonstrably need to live across Core instructions as SSA program values.

Every Core instruction producing a `ProgramValue` has exactly one immutable
representation. For most kinds it is fixed by `instruction.def` and occupies no
instruction space. Instruction kinds are not representation-parametric: when
the same operation is required for more than one representation, the schema
defines one kind for each representation. In particular, Core has one `Mov`
kind and one block-parameter kind per `ValueRepresentation`, initially
`Mov`, `MovF64`, `Parameter`, and `ParameterF64`. The unsuffixed kinds
have the common `TaggedValue` representation. Semantic IR does not assign
representations to its `ProgramValueRef`s; Semantic-to-Core lowering creates a
fresh graph whose program values all have concrete representations. Generic
Core construction starts with `TaggedValue` and introduces another
representation only through explicit conversion or specialized instructions,
so Core never contains an unknown representation.

Every `ValueRepresentation` must have exactly one corresponding `Mov` kind.
The schema generates the representation-to-`Mov` mapping and rejects a missing
or duplicate entry. The tagged kind retains the unsuffixed `Mov` name; other
representations use `Mov` followed by their representation suffix.

The record may contain raw pointers, serials, enums, flags, scalar immediates,
and other trivially destructible values. It must not directly contain an
owning `std::vector`, `std::string`, `std::unique_ptr`, reference-counted
handle, or any value whose destructor releases resources.

Payload slots are pointer-sized words. The schema, rather than a runtime tag
stored beside each word, determines their C++ interpretation:

```text
ProgramValue   -> Instruction* wrapped as ProgramValueRef
Snapshot       -> Instruction* wrapped as SnapshotRef
BlockEdge      -> BlockEdge*
Shape          -> Shape*
ShapeKey       -> inline ShapeKey value
ValidityCell   -> ValidityCell*
BytecodePC     -> compact bytecode offset or pointer representation
Immediate      -> inline integer or enum value
ValueConstant  -> directly embedded Value retained by the compilation session
```

`ValueConstant` is the schema classification of that attribute, not a pool
handle or a second runtime wrapper; its typed accessor and constructor use
`Value` directly.

`ValueConstant` may contain either a non-pointer tagged value or a managed
pointer. Construction paths pin the latter through the compilation session.

Ordinary operand words are uniformly direct instruction pointers. Dereferencing
one and decoding its producer kind identifies whether it is a `ProgramValue` or
`Snapshot`; a `ProgramValue` producer kind also directly identifies its
representation. The schema declares what each operand position is permitted to
reference and describes fixed and variable ranges. Only Snapshot's annotated
payload needs a per-entry descriptor because it additionally admits constants.

The implementation enforces that every encoded class fits the slot size and
alignment. Every fixed-arity instruction places all operands first and all
attributes immediately after them in the five inline slots. The schema-derived
layout must fit completely; schema generation rejects a fixed kind requiring
more than five combined operand and attribute words.

Every variable-operand instruction instead places a pointer to indirect storage
in slot zero. Its fixed attributes begin in slot one and continue inline. The
operand-storage high bit is set, and its low bits hold the total logical operand
count, including the kind's fixed operands. This avoids a split operand
representation for kinds such as `PythonCall`, and lets generic traversal select
one contiguous operand source.

This layout deliberately does not inline a small variable operand array even
when it would fit in the remaining payload slots. Attribute positions must be
compile-time constants determined solely by the instruction kind. For a
fixed-arity kind, attribute `i` occupies slot `fixed_operand_count + i`; for a
variable-operand kind, it occupies slot `1 + i`. Typed attribute accessors can
therefore use constant offsets without inspecting the runtime operand count or
branching on whether a particular instance happened to fit inline.

A variable operand range is always the final operand declaration. All fixed
operands therefore occupy kind-constant leading indices in the indirect array,
followed by the variable tail. For example, `PythonCall` stores its callable at
operand zero, its Snapshot at operand one, and arguments beginning at operand
two. Typed access to the fixed operands needs no runtime argument count.

The indirect array uses compilation-arena side data. For ordinary homogeneous
variadic kinds such as `PythonCall`, every indirect slot is an operand and the
physical slot count equals the header's logical operand count:

```cpp
using InstructionSlot = uintptr_t;

const InstructionSlot *indirect_operands = ...;
```

Snapshot is the annotated heterogeneous exception. It stores all capture
payloads first, followed by one parallel descriptor word per capture:

```text
payload[0..N)
descriptor[0..N)
```

The header stores logical operand count `N`; Snapshot allocates `2 * N`
physical indirect slots. Payload-first layout preserves the invariant that the
indirect pointer addresses operand zero. Generic traversal uses descriptor `i`
to distinguish a `ProgramValueRef` from a `ValueConstant` before interpreting
payload `i`.

Each generated variadic class exposes hidden construction machinery
`n_indirect_slots_for(constructor arguments...)`. The compilation arena uses it
to allocate the physical side-data span before placement construction. It is
deliberately separate from `operand_count()`: the former sizes storage, while
the latter counts logical operands.

The 15-bit count is an explicit representation limit. Exceeding it aborts that
compilation and falls back to the interpreter; it is not a partially valid
instruction state.

Construction may use a mutable buffer before completing the instruction, but
stored and typed access decodes the declared operand or attribute class and
exposes immutable typed values, for example `absl::Span<ProgramValueRef>`.
Clients cannot replace an operand by assigning through the span.

For an indirect operand sequence, the side-data pool allocates the final
arena-owned range first and returns a mutable span to the factory. The factory
constructs operand words directly in that range; it does not build a temporary
`std::vector` and then ask the pool to copy it. Publication retains only a const
view of the completed range.

Side-data types obey the same no-destruction contract. Instruction and
side-data allocation should enforce at least:

```cpp
static_assert(std::is_trivially_destructible_v<Instruction>);
static_assert(std::is_trivially_destructible_v<InstructionSideData>);
```

Trivial copyability is not initially required because instruction pointers are
stable and the arena does not relocate records. It may be imposed later if a
dense movable buffer demonstrates enough value to justify changing that
contract.

Trivial destruction is necessary but not sufficient: instruction side data may
own only allocations made from the same bulk arena. Other pointers are borrowed
from objects whose lifetimes cover the compilation. A trivially destructible
raw pointer must not hide ownership of an external allocation or resource.

The fixed payload is implemented as named, aligned pointer/integer/value slots,
or by copying trivially copyable payload values with `memcpy`. Typed accessors must
not reinterpret generic byte storage as an object whose lifetime has not begun,
read inactive union members, or assume alignment that the payload does not
provide. Side-data allocation checks alignment and allocation-size overflow.

The compilation arena releases instruction and trivial side-data storage in
bulk. It does not walk those objects to invoke destructors. The same common
arena may also own normally destroyed tables and pools; common compilation
ownership does not imply that every object occupies the destructor-free
allocation domain.

### Construction, Placement, and Publication

Allocation and graph placement are separate operations. The typed builder API
forwards allocation to the compilation arena and returns an intrinsically valid,
unplaced concrete instruction. It does not need an insertion position:

```cpp
AddSMIInstruction *sum =
    builder.make_instruction<AddSMIInstruction>(lhs, rhs, snapshot);
```

The constructor surface is generated from `instruction.def`.
`OperandTraits<OperandClass>` and `AttributeTraits<AttributeClass>` select the
C++ parameter type and encoding for each declared slot, representation traits
refine Core program-value operands, and the template returns the requested
concrete subclass. Consequently, ordinary callers cannot choose an
inconsistent kind, result class, operand class, attribute class, Core
representation, arity, or payload layout.
Future IR-level-specific wrappers may expose only instruction kinds permitted at
that level. The arena constructor itself still records each kind's allowed
levels for placement and verification.

The factory also owns managed-constant retention. Before returning an
unpublished instruction or arena side-data object containing a pointer-valued
`ValueConstant`, it calls the compilation session's pinning API for that
`Value`. This applies equally to fixed attributes and Snapshot arrays. Cloning
into another compilation session invokes the destination factory and therefore
repins each managed value there. Pinning is complete before the allocation is
observable; it is not deferred until graph attachment and is not a CFG-editor
responsibility. Failure to retain a value is a resource failure that aborts the
compilation.

This compile-time construction safety does not attempt to prove contextual
graph properties such as dominance or block-edge ownership. There are two
placement paths with deliberately different validation costs.

A translator or major lowering uses a bulk `GraphBuilder`. Construction and
rewriting APIs use `make` for arena allocation without attachment, `append` for
attaching an existing object at the end of a specified container, and `emplace`
for allocating and attaching at the end in one operation. The common append and
emplace operations perform only work naturally
local to that attachment, such as extending the block's instruction sequence.
They do not rescan dominance, repeatedly verify the partially built graph, or
otherwise turn a linear translation into a quadratic algorithm. Optional use
records are not a permanently maintained part of the graph and are built only
when a consuming pass requests them.

The builder's `finalize()` operation validates the completed graph in one
`O(instructions + edges + payload slots)` pass. It checks IR-level legality,
graph membership, result and operand classes, live producers, block-edge
ownership, terminator placement, local definition-before-use, and other
structural invariants. It does not build an optional `UseIndex` merely to
perform this validation. A graph under bulk construction is not published to
ordinary passes. If final verification finds an invalid graph, that is a
compiler logic error: it reports the structural diagnostic and hard-asserts
rather than turning the bug into an interpreter fallback.

Once a graph is published, local transformations use the CFG editor. The
editor attaches factory-created instructions, rewrites operands, updates active
graph-owned indexes and mutation generations, and detaches replaced
instructions. An on-demand `UseIndex` is updated only when the editing pass
explicitly retains it as mutation-aware working state; otherwise mutation makes
it stale. The editor may check contextual invariants eagerly when those checks
are constant-time or already maintained incrementally. A transformation that
performs many related edits may use an editor transaction that defers global
verification until commit; ordinary passes cannot observe the intermediate
graph. The editor is therefore the authority for mutating a published graph,
not a mandatory route through which every instruction must be allocated.

Placement and liveness are graph-owned state rather than another physical
instruction tag. The normal lifetime progression is:

```text
allocated and unplaced -> placed in one graph -> removed from graph
```

An unplaced instruction has a live instruction kind and a schema-valid payload,
but is not yet a member of any graph. It may be attached at most once. After
removal, the editor may poison the abandoned storage with the reserved detached
tag to catch stale references. Poisoning is not an allocation-reuse mechanism,
not graph membership, and not a semantic IR state.

### Compilation Failure and Runtime Publication

Graph publication above means making a finalized IR visible to compiler passes.
Runtime publication here means installing completed machine code and persistent
dependencies. The latter occurs only after all compiler phases succeed.

The JIT distinguishes compiler logic errors from resource failure. Violating an
instruction-schema, graph, editor, or pass invariant is a compiler bug. Such a
violation hard-asserts, with the verifier and stable-serial diagnostics used to
identify the responsible pass. It is not reported as an ordinary inability to
compile and must not silently fall back to the interpreter.

Allocation exhaustion and comparable resource failures are expected compilation
failures. Fallible arena, side-data, index, and code-buffer allocation propagates
an explicit compilation failure such as `CompileFailure::AllocationFailure` to
the JIT entry point. The entire compilation session, including any partially
built or partially edited graph, is then abandoned, and execution continues in
the interpreter. The editor does not need to roll a graph back into a usable
state after such a failure because no later pass may observe that compilation.

This cheap whole-compilation abort is a primary reason for arena ownership.
Instructions, blocks, edges, side data, and other bulk compiler storage are
allocated from the compilation arena; when compilation fails, allowing that
arena to leave scope releases the entire allocation domain at once. The
compiler does not need to discover and individually undo partially constructed
IR objects. Normally destroyed compilation tables and scoped external
registrations remain owned by the enclosing compilation session and unwind
alongside the arena.

This failure model does not permit leaked external state. The compilation
session owns temporary pins and similar runtime-visible resources, and releases
them when an unsuccessful session is destroyed. Compiled code, validity-cell
dependencies, assumptions, cache entries, and other persistent runtime state
are installed only after all fallible compilation work and final verification
have succeeded. Publication is the final commit; failure before it leaves
previously executing interpreter and compiled state unchanged.

Editor transactions therefore exist to hide deliberately incomplete multi-step
rewrites and to validate their completed structure, not to roll back allocation
failure. On the successful path, an edit must leave the published graph valid.
On resource failure, the enclosing compilation is discarded.

### Managed Value Constants and Pins

`Const` instructions and Snapshot side data embed constants directly as
`ValueConstant`s. Core does not distinguish values that will become machine
immediates from values that will require the traced constant pool, and does not
assign a pool index or otherwise model that eventual pool.

A compilation pin is a strong root as well as a relocation prohibition. It
keeps the object alive and prevents its address from changing; a mere
`do-not-move` bit that still permits reclamation would be insufficient.
Constructing a pointer-valued `ValueConstant` requires the compilation session
to retain a deduplicated pin
for that object. Pins are session state rather than fields in arena objects and
are released together when the compilation session ends. Detaching an
instruction need not remove its pin; retaining pins until the short compilation
finishes keeps editor cleanup simple.

The pinning API already expresses this construction contract even while its
current implementation is a no-op because CloverVM has no moving collector.
Factories must still call it unconditionally. That keeps the representation and
all construction paths ready for a moving collector without later discovering
which instruction kinds can hide managed references. The verifier independently
walks every `ValueConstant` attribute and asserts that each pointer-valued entry
is covered by the session's pin set; an implementation whose pins are currently
implicit may report that coverage trivially.

Backend or Machine-IR lowering classifies each surviving `Const`. It may encode
or synthesize a suitable non-pointer value as an immediate, or pass any `Value`
to `MachineCodeEmitter::add_value_to_constant_pool()`. The emitter owns pool
values and assigns and deduplicates final `ValuePoolEntry` offsets. Pointer-valued
constants must take the traced-pool path; Core itself does not express this
split.
Successful publication initializes the `JitCodeObject` pool before session pins
are released. On failure, the emitter owners, compilation pins, and arena are
discarded while the interpreter continues.

The initial no-safepoint policy, prohibition on managed allocation during
compilation, deferred managed-object constant folding, and possible future
phase-boundary yielding are pipeline contracts specified in
[JIT Compiler and IR](jit-compiler-and-ir.md). The pin representation supports
that future extension without adding indirection to instruction operands.

## Pointers, Serials, and Determinism

Structural relationships use pointers, while relationships that consume an
instruction result use zero-overhead typed pointer wrappers:

```cpp
ProgramValueRef lhs;
ProgramValueRef rhs;
SnapshotRef snapshot;
BlockEdge *true_edge;
BlockEdge *false_edge;
```

A client can follow such a relationship without also carrying the graph or an
indexed instruction store. This is more suitable for the list-based IR than a
Carbon-style pervasive ID lookup.

Pointers are references, not deterministic identities. Instructions, blocks,
and block edges retain their typed, monotonically allocated serials. Compiler
output must not depend on pointer values or unordered pointer-container
iteration. Passes use defined traversal order and serials for diagnostics,
stable tie-breaking, and deterministic ordering.

The result-reference wrappers are mandatory for every result-consuming field:

```cpp
using ProgramValueRef = ResultRef<ResultClass::ProgramValue, Instruction *>;
using SnapshotRef = ResultRef<ResultClass::Snapshot, Instruction *>;
```

Constructing a result reference validates that the producer's intrinsic
`ResultClass` matches the class required by the wrapper. A
`ResultClass::None` instruction cannot be referenced as an operand, a Snapshot
cannot be used as a program value, and a program value cannot be used where
recovery state is required. When validating an encoded operand slot that consumes
an instruction result, the verifier uses `operand_accepts_result()` rather than a
mapping switch. Generated construction signatures make mismatched classes
unrepresentable to ordinary callers, structural operand replacement checks the
declared operand class, and the verifier independently checks the encoded
payload. Raw `Instruction *` remains the identity used for instruction-list
placement, diagnostics, and other non-result structural operations. A result
wrapper still contains a pointer; it does not turn the IR back into a
container-relative integer-ID representation.

Generic traversal, dominance, use discovery, and Semantic IR use the erased
`ProgramValueRef`. Core typed APIs refine it without changing its pointer-sized
representation:

```cpp
template<ValueRepresentation Representation>
class RepresentedValueRef;

using TaggedValueRef =
    RepresentedValueRef<ValueRepresentation::TaggedValue>;
using F64Ref =
    RepresentedValueRef<ValueRepresentation::F64>;
```

Erasing a `RepresentedValueRef` to `ProgramValueRef` is implicit and free.
Refining an erased Core reference validates the producer's intrinsic
representation. Fixed-representation generated constructors and accessors use
the refined wrapper, making common mismatches C++ type errors; generic
infrastructure deliberately retains the erased form.

## Concrete Typed Instructions

`src/jit/instruction.def` is the authoritative schema for the closed set of
live instruction kinds. Each definition names the instruction kind and typed
view, the IR level or levels in which it is legal, its intrinsic
`ResultClass`, its `MustEffects` lower bound and `MayEffects` upper bound, its
payload shape, every fixed or variable operand slot with its `OperandClass`,
and every immutable payload attribute with its `AttributeClass`. Every ordinary
operand is a typed instruction-result reference. Every attribute declared for a
kind is present; the schema has no optional-attribute mechanism. Core
program-value results and operands additionally declare fixed representation
constraints. The sole
exception is Snapshot's representation-erased captured-value operand described
below. Repeated inclusion of that schema generates a dense
`InstructionOrdinal`, the encoded `InstructionKind` enum, invariant kind
metadata, representation-safe construction and access, generic operand
traversal, result/operand class legality, attribute decoding, effect bounds, and
the size and alignment constraints for encoded payloads. The `Detached` storage
tag is not listed as a semantic instruction definition.

The upper four bits of the 16-bit `InstructionKind` encode its intrinsic result
class and representation as two numerically aligned two-bit fields. The lower
12 bits contain its dense generated ordinal:

```text
15        14 13        12 11                       0
+-----------+------------+--------------------------+
|ResultClass| ValueRep   | instruction ordinal      |
+-----------+------------+--------------------------+
```

The encoded bits remain part of the kind for equality and exhaustive switches;
they are not independently mutable properties. Masks decode `ResultClass` and
`ValueRepresentation` without a kind switch or metadata lookup. Schema
generation rejects `None` or `Snapshot` results with a non-`None`
representation and rejects `ProgramValue` results without a concrete
representation.

Because full kind values are consequently sparse, they do not directly index
metadata. The same schema pass generates a dense `InstructionOrdinal`, and the
low 12 bits index the compact metadata table. Thus metadata lookup pays one mask
without introducing holes or duplicating result information in the table. The
12-bit ordinal permits up to 4,096 instruction kinds.

The schema owns facts that must remain synchronized for every instruction
kind. It may generate storage decoding and straightforward typed-accessor
boilerplate, while instruction-specific convenience accessors may remain
handwritten. It does not generate pass implementations or visitor-method
dispatch; passes remain ordinary C++ so they can organize related cases
locally.

Conceptually, representative definitions describe the following payloads. The
examples elide the required IR-level and effect-bound fields:

```text
AddF64
    result: ProgramValue(F64)
    lhs: ProgramValue(F64)
    rhs: ProgramValue(F64)

AddSMI
    result: ProgramValue(TaggedValue)
    lhs: ProgramValue(TaggedValue)
    rhs: ProgramValue(TaggedValue)
    snapshot: Snapshot

BoxF64
    result: ProgramValue(TaggedValue)
    source: ProgramValue(F64)

UnboxF64
    result: ProgramValue(F64)
    source: ProgramValue(TaggedValue)
    snapshot: Snapshot

ShapeGuard
    result: ProgramValue(TaggedValue)
    object: ProgramValue(TaggedValue)
    expected_shape: attr Shape
    validity: attr ValidityCell
    snapshot: Snapshot

PythonCall
    result: ProgramValue(TaggedValue)
    callable: ProgramValue(TaggedValue)
    arguments[]: ProgramValue(TaggedValue)
    snapshot: Snapshot
    interpreter_return_pc: attr BytecodePC

CheckNotImplemented
    result: ProgramValue(TaggedValue)
    call_result: ProgramValue(TaggedValue)
    snapshot: Snapshot

Mov
    result: ProgramValue(TaggedValue)
    source: ProgramValue(TaggedValue)

Const
    result: ProgramValue(TaggedValue)
    constant: attr ValueConstant

MovF64
    result: ProgramValue(F64)
    source: ProgramValue(F64)

Parameter
    result: ProgramValue(TaggedValue)

ParameterF64
    result: ProgramValue(F64)

Snapshot
    result: Snapshot
    captured_values[]: snapshot operand ProgramValue(AnyRepresentation)
                     or snapshot attr ValueConstant
    resume_pc: attr BytecodePC

ConditionalBranch
    result: None
    condition: ProgramValue(TaggedValue)
    true_edge: attr BlockEdge
    false_edge: attr BlockEdge
```

`AnyRepresentation` is a narrow operand constraint in the schema, not a member
of `ValueRepresentation` and never an instruction result. Snapshot's
`captured_values` array is the only slot allowed to use it because a Snapshot
records logical recovery values rather than normal Core dataflow for one
machine representation. Its schema describes a discriminated `SnapshotValue`
whose alternatives are a program-value operand or a directly retained
`ValueConstant` attribute. This is a Snapshot-specific payload shape, not
another general operand or attribute class.

Generated generic Snapshot traversal reports a `ProgramValueRef` alternative
as an operand use and a `ValueConstant` alternative to attribute traversal. The
generated Snapshot accessor preserves the same distinction. Side-exit
frame-sync generation stores tagged program values directly, materializes
constants according to their value and target lowering, and boxes captured
`F64` values before writing them to the interpreter frame.
Adding another alternative or representation therefore requires an exhaustive
frame-materialization case. No arithmetic, call, forwarding, parameter, or
other Core instruction may accept an erased representation.

Core represents every ordinary use of a constant through a normal
`ProgramValueRef` produced by `Const`; constants are not embedded in consumer
operands. Backend preparation or Machine IR chooses immediate synthesis or a
constant-pool load. Pointer-valued constants must use the traced pool, while a
non-pointer value may use either form according to target encodability and
profitability. The phase also selects lowerings and `LocationSummary` records
before liveness and register allocation run. Immediate shape rules, including
any future target-specific single-consumer immediate nodes, remain backend
policy rather than Core IR legality.

Generated factory methods and typed accessors expose fixed constraints in their
C++ signatures. Each program-value operand takes the concrete typed reference
for its declared representation:

```cpp
F64Ref make_add_f64(F64Ref lhs, F64Ref rhs);
TaggedValueRef make_box_f64(F64Ref source);
F64Ref make_unbox_f64(TaggedValueRef source, SnapshotRef snapshot);
TaggedValueRef make_mov(TaggedValueRef source);
TaggedValueRef make_const(Value constant);
F64Ref make_mov_f64(F64Ref source);
```

Defining separate kinds keeps their generated construction and access APIs
concrete and prevents a representation-polymorphic instruction from becoming
an unchecked escape hatch. Exact macro spelling remains an implementation
detail.

No `OperandClass`, `AttributeClass`, or `ValueRepresentation` tag is stored
beside each ordinary payload word. Generic code reads the instruction kind once
and selects schema-generated layout metadata or a schema-generated per-kind
enumerator. Conceptually:

```cpp
struct OperandSlotDescriptor
{
    OperandClass operand_class;
    ProgramValueConstraint representation_constraint;
    SlotLayout layout;  // Fixed or variable-length.
    uint16_t offset;
};

struct AttributeSlotDescriptor
{
    AttributeClass attribute_class;
    SlotLayout layout;  // Fixed or variable-length.
    uint16_t offset;
};

void visit_operands(Instruction &instruction, OperandVisitor visitor);
void visit_attributes(Instruction &instruction, AttributeVisitor visitor);
```

The generated dispatch interprets each payload word only according to the
schema for that instruction kind. `ProgramValue` and `Snapshot` operands are
ordinary result-reference uses for SSA, liveness, and rewriting. Attribute
slots such as `BlockEdge`,
`Shape`, `ShapeKey`, `ValidityCell`, bytecode PCs, immediates, and value
constants are immutable semantic payload; they are skipped by generic
use discovery and result replacement. CFG maintenance, verification, printing,
and cloning may inspect attributes through their own visitor or typed accessors.

`Snapshot` is the one explicit exception to ordinary local-use behavior. It is
a zero-code aggregate result whose captured `ProgramValue` operands become point
uses at every guard or side exit that consumes the `SnapshotRef`. Liveness
expands a Snapshot operand transitively at that consuming position, so several
nearby guards may safely share one Snapshot without treating its captured
values as dead after the Snapshot instruction itself. Verification keeps the
Snapshot anchored near its consumers and on the correct side of effect
boundaries. This special case is preferred over a general per-slot role axis;
the design should revisit that choice if another same-class relationship needs
different generic behavior.

Every graph has one immutable IR level, initially `Semantic` or `Core`; a
future Machine IR may use the same mechanism if it adopts this representation.
An instruction definition may name one level or an explicit set when the same
semantic kind is valid in more than one IR. Allowed levels are kind metadata
and consume no space in an instruction. IR-level-specific factories omit
disallowed constructors, the bulk builder checks the completed graph at
finalization, and the CFG editor rejects incremental insertion or replacement
with a kind not allowed at the graph's level. The verifier independently checks
every placed instruction. It also
rejects references that cross graphs or IR levels. Concrete analysis types
accept only the graph level they own, such as `SemanticValueAnalysis` for a
Semantic graph and `CoreEffectAnalysis` for a Core graph.

For Core graphs, verification additionally requires every `ProgramValue`
producer to have one legal representation, every fixed operand constraint to
match its producer, and every representation-changing edge to be an explicit
conversion instruction. Verification rejects `AnyRepresentation` on every
result and on every operand other than `Snapshot.captured_values`. Snapshot
capture may contain heterogeneous program values and `ValueConstant`s. Every
pointer-valued constant must be covered by the compilation session's pin set.
Recovery planning must interpret each captured item using its producer's
concrete representation or retained `Value` and provide an exhaustive
frame-materialization operation for that case.

Each concrete instruction form is a final, fieldless subclass of `Instruction`.
The instruction arena placement-constructs the subclass selected by the schema;
it must never construct a base `Instruction` and reinterpret it as a concrete
kind. Because subclasses add neither fields nor virtual dispatch, every concrete
kind has the same 48-byte representation as `Instruction` while providing
kind-specific read-only accessors:

```cpp
class AddF64Instruction final : public Instruction
{
public:
    static constexpr InstructionKind Kind = InstructionKind::AddF64;
    static constexpr ResultClass Result = ResultClass::ProgramValue;
    static constexpr ValueRepresentation Representation =
        ValueRepresentation::F64;
    static constexpr EffectProfile MustEffects = EffectProfile::None;
    static constexpr EffectProfile MayEffects = EffectProfile::None;
    static constexpr IRLevelMask AllowedIRLevels = IRLevelMask::Core;
    static constexpr bool IsVariadic = false;

    F64Ref lhs() const;
    F64Ref rhs() const;

private:
    friend class InstructionPool;
    AddF64Instruction(uint32_t serial, F64Ref lhs, F64Ref rhs);
};

static_assert(sizeof(AddF64Instruction) == sizeof(Instruction));
```

These public constants expose only intrinsic semantic facts from the schema.
Template code that already knows the concrete subclass can therefore fold kind,
result, representation, effect-bound, and IR-level queries without consulting
dynamic kind metadata. Physical layout constants such as operand counts,
attribute bases, inline-slot counts, and indirection remain private to generated
construction and accessors.

The subclass exposes only immutable fields meaningful for its instruction kind.
Inferred types, proven-absent effects, locations, and other phase knowledge are
read through the concrete metadata object that owns them, not through the
instruction object. Ordinary code holds `Instruction *` for heterogeneous IR
structure and performs a kind-checked downcast when it needs concrete access.

There is one semantic `InstructionKind` enum generated from
`src/jit/instruction.def`; the generated `InstructionOrdinal` exists only for
compact table indexing. Each concrete subclass declares its own
`static constexpr Kind`, and schema-generated validation requires it to match
the view mapping in the definition. Checked conversion uses the type itself as
the source of the expected kind:

```cpp
template<typename TypedInstruction>
const TypedInstruction *Instruction::as() const
{
    assert(kind() == TypedInstruction::Kind);
    return static_cast<const TypedInstruction *>(this);
}
```

`is<T>()` and `try_as<T>()`, if useful, follow the same mapping. They use an
enum comparison followed by a static downcast; the arena has already begun the
lifetime of the matching concrete subclass. The design does not use
`dynamic_cast`, `typeid`, or virtual instruction methods.

Category views may later represent a deliberately defined set of kinds with a
common payload shape. Such a view has no single `Kind`, so it is obtained
through a separate membership-checked API such as `as_category<T>()`; it cannot
be used with `CL_JIT_INSTRUCTION_CASE`. Category views are deferred until an
actual grouped operation family justifies them.

## Mostly Immutable Instructions

An instruction's kind, result class, Core value representation, constants,
bytecode origin, payload shape, and attributes are immutable
after construction. A pass that changes one of these properties constructs an
unplaced replacement through the appropriate instruction factory, then asks the
structural IR editor to attach it and rewrite the published graph explicitly.

Removal from a graph is not instruction mutation. The editor may poison the
abandoned allocation as the final step of removal:

```text
Live(fixed InstructionKind) -> removed from graph -> poisoned storage
```

The CFG editor poisons storage only after it has rewritten or removed every use,
removed the instruction's own operand occurrences from any active
mutation-aware `UseIndex`, invalidated or removed active metadata entries, and
unlinked the instruction from its block. The edit plan must establish the
absence of incoming uses through a current `UseIndex` or a complete generic
operand scan. A detached instruction's pointer-valued `ValueConstant` is no
longer semantically visible, and the compilation session may retain its pin
until the session ends. The editor may debug-poison the remaining payload
storage and publish the detached tag last. An editor transaction is not
observable by ordinary passes in an intermediate state. Poisoned storage is
never republished or returned to a live kind.

The serial is deliberately preserved for diagnostics. Any detached instruction
encountered by verification, generic traversal, typed conversion, a result
reference, or a current `UseIndex` is a hard compiler bug. The diagnostic reports
the preserved serial and does not interpret the poisoned payload. Ordinary pass
code does not branch on detachedness as a supported case; concrete instruction
pointers must not be retained across structural edits without proving that the
instruction remains attached.

Operand slots are controlled mutable structure. The structural editor may
replace a `Snapshot` operand only with a result of the matching declared
`OperandClass`. A `ProgramValue` replacement must be a `ProgramValueRef` whose
producer satisfies the slot's representation constraint.

The editor provides one symmetric primitive for changing such a slot:

```cpp
void replace_program_operand(
    OperandSlotHandle slot,
    ProgramValueRef expected,
    ProgramValueRef replacement);
```

It verifies that the user is live, the expected value still occupies the slot,
the slot has `OperandClass::ProgramValue`, and the replacement satisfies its
representation constraint. The referenced producer must be live and belong to
the same graph.

The editor maintains metadata lifetime, analysis invalidation, and graph
mutation generation. If the pass has retained a mutation-aware `UseIndex`, the
editor moves the affected `UseRecord` to the replacement producer. Otherwise
the generation change makes the old index stale. Attribute slots such as
`BlockEdge`, `Shape`, `ShapeKey`, `ValidityCell`, bytecode PCs, immediates, and
value constants are immutable semantic payload; changing
one requires instruction replacement. Changing any slot's class, representation
constraint, count, or layout likewise requires replacement. Concrete subclasses expose
every operand and attribute read-only and never provide setters or writable
arrays.

The schema-generated generic operand walker is the common primitive for use
discovery and bulk rewriting. It walks fixed and variable operands in declared
order and never visits attributes. For every `ProgramValue` or `Snapshot`
operand it can emit a temporary `UseRecord` containing the producer and an
`OperandSlotHandle` that identifies the user and the schema-declared slot:

```cpp
struct UseRecord
{
    const Instruction *producer;
    OperandSlotHandle operand;
};
```

Operand layout and variable-operand counts are immutable, so such a handle remains
physically resolvable while its user remains live. The editor still validates
that the user is live and that the slot contains the expected producer before
rewriting it.

An on-demand, generation-checked `UseIndex` groups these records by producer.
It is useful for repeated sparse queries such as no-use and single-use tests,
dead-code elimination, dependent worklists, and replacing the uses of a small
number of values. Building it costs one whole-graph operand walk. It may be
discarded after one mutation plan, maintained privately by a pass across an
editing batch, or rebuilt; the graph and ordinary editor do not pay to keep it
permanently current.

Bulk transformations may instead record a typed result-reference replacement
map and call a generic `rewrite_operands()` operation. The operation walks all
operands once and rewrites matching references without first materializing use
records. This costs `O(all operands + replacements)` and is preferable when
translation or lowering has accumulated many substitutions.

Replacements have simultaneous semantics: given `A -> B` and `B -> C`, an
original use of `A` becomes `B` unless the map was explicitly transitively
normalized before the scan. Before mutating, the bulk operation validates every
affected slot. Constant folding creates or reuses a `Const`
producer and rewrites uses to that instruction's `ProgramValueRef`.

A representation-changing rewrite likewise inserts an explicit conversion or
replaces the consumer; it cannot use generic result replacement to connect
incompatible encodings. After successful preflight, the batch applies the
corresponding use-index moves and advances graph and attachment generations
once.

The same walker independently reconstructs uses for verification and also
supports cloning and printing of operand relationships. Attribute visitors or
typed accessors handle cloning, printing, CFG edge maintenance, constant pin
validation, and bytecode-PC diagnostics for non-dataflow payload. The verifier
compares any current `UseIndex` against reconstructed operand records and
hard-fails on references to poisoned storage. Kind-specific named accessors such
as branch edges remain available through concrete accessors.

## Phase-Owned Attached Metadata

Inferred types, proven-absent effects, dependencies, locations, and similar derived
facts are not part of the physical instruction representation. They live in
explicit metadata objects owned by the phase and IR level that defines them,
for example:

```text
SemanticValueAnalysis   Semantic ProgramValueRef -> ValueFacts
CoreEffectAnalysis      Core Instruction*        -> ProvenAbsentEffects
LocationAssignments     Core Instruction*        -> backend locations
UseIndex                Instruction*             -> temporary UseRecords
```

Core `ValueRepresentation` is deliberately not an attachment. It is an
immutable producer and operand contract used to type the SSA graph itself.
Register, spill, and constant locations remain backend-owned attached metadata.

This is not a generic per-instruction property bag. Each attachment is a
concrete type with its own invariants, key domain, mutation rules, and permitted
graph level. Clients query it with an instruction pointer or typed result
reference. A dense implementation may use the instruction serial as an internal
vector index and retain the pointer in each populated entry for defensive
identity validation; serial lookup is not exposed as the pass API.

Mutable analysis builds or updates its private table and publishes a frozen
view tagged with the source graph and mutation generation. Every query validates
that the graph still has that generation, that the instruction is live and
belongs to the graph, and that the entry's pointer matches. Structural mutation
makes old frozen views stale. The baseline response is broad invalidation and
recomputation of any analysis a later pass still needs. A future analysis may
consume editor mutation descriptions to preserve unaffected entries or
recompute only affected dependents, but that is an optimization justified by
measured cost, not a requirement of the instruction representation.

Attachments exist only while a later phase consumes them. Semantic value facts
may be discarded after Semantic-to-Core lowering; Core effect information may
be discarded after effect-dependent optimization; backend location data has
its own later lifetime. Removing an instruction invalidates or removes its
entries from every active attachment before the editor may poison the abandoned
storage. Major representation boundaries may build a fresh graph while using
the same instruction, CFG, serial, and arena machinery.

## Kind Effect Bounds and Proven Absence

Each instruction kind declares two immutable effect bounds in
`src/jit/instruction.def`:

- `MustEffects` is the lower bound: effects necessarily performed by every
  instance of that kind and therefore never removable by analysis.
- `MayEffects` is the upper bound: every effect that any instance of that kind
  may perform without changing instruction kind.

Examples of must-effects include a return terminating its block or an operation
performing an inherently visible write. A generic call's may-effects include
raising and its other conservative call implications even when target analysis
can prove some of them absent for a particular call.

When a pass needs refined effect information, a concrete phase-owned analysis
may provide proof metadata for one particular live instruction.
Newly constructed instructions default to the kind's full `MayEffects`
envelope. `CoreEffectAnalysis`, or the corresponding concrete analysis for
another allowed IR level, stores the effects it has proven absent from operand
facts, resolved targets, and other evidence:

```text
MustEffects(kind) subset-of MayEffects(kind)
ProvenAbsentEffects(instruction) subset-of MayEffects(kind)
ProvenAbsentEffects(instruction) intersection MustEffects(kind) == empty
EffectiveEffects(instruction) =
    MayEffects(kind) - ProvenAbsentEffects(instruction)
```

The schema must assert the bound relationship for every instruction kind. Any
effect analysis that exists must assert the `ProvenAbsentEffects` constraints.
A `MustEffects` bit outside `MayEffects` makes the kind metadata contradictory.
Proving absent an effect outside `MayEffects` means the analysis is recording
irrelevant or misclassified information; proving absent a `MustEffects` bit
means the instruction kind metadata or analysis is wrong. None of these cases is
a recoverable compilation failure.

A pass with a current, generation-checked effect-analysis view receives the
per-instruction `ProvenAbsentEffects` and derives `EffectiveEffects` with the
formula above. A pass without such a view receives `MayEffects`, the
conservative kind envelope. Supplying a stale view asserts; it never silently
falls back. The first Core slice may rely entirely on `MayEffects` until a real
optimization needs a refined attachment. In particular, the physical
instruction and its concrete subclass do not expose `is_pure()` based on
`MustEffects`: absence from the lower bound says nothing about what the
instruction may do.

Selecting a genuinely different semantic operation still requires instruction
replacement. An analysis attachment refines facts about the existing operation;
it does not change its kind or semantic payload.

## Typed Switch Matching

Compiler passes are organized as direct switches rather than visitor methods.
This keeps one pass reviewable as one body of code and lets related instruction
kinds remain adjacent or share grouped cases.

Carbon-style macros retain a real compiler-visible `switch` and bind a typed
read-only view in each case:

```cpp
#define CL_JIT_INSTRUCTION_SWITCH(instruction)                         \
    switch (const auto &cl_jit_instruction_switch_value =             \
                (instruction);                                        \
            cl_jit_instruction_switch_value.kind())

#define CL_JIT_INSTRUCTION_CASE(Type, variable)                        \
    Type::Kind:                                                        \
    if (Type variable =                                               \
            cl_jit_instruction_switch_value.as<Type>();               \
        false)                                                        \
    {                                                                 \
    }                                                                 \
    else
```

A code-generation pass then reads as an ordinary match:

```cpp
CL_JIT_INSTRUCTION_SWITCH(*instruction)
{
    // Arithmetic.
    case CL_JIT_INSTRUCTION_CASE(AddInstruction, add)
    {
        emit_add(add.lhs(), add.rhs());
        break;
    }

    case CL_JIT_INSTRUCTION_CASE(SubtractInstruction, subtract)
    {
        emit_subtract(subtract.lhs(), subtract.rhs());
        break;
    }

    // Calls and exits.
    case CL_JIT_INSTRUCTION_CASE(CallInstruction, call)
    {
        prepare_arguments(call.arguments());
        emit_call(call.target());
        break;
    }

    case CL_JIT_INSTRUCTION_CASE(ReturnInstruction, return_instruction)
    {
        emit_return(return_instruction.return_value());
        break;
    }
}
```

The macros expand to real `case Type::Kind` labels. Exhaustive JIT switches do
not contain a `default`, and the build enables the compiler's missing-enum-case
warnings, including `-Wswitch` and `-Wswitch-enum` where supported. Adding an
`InstructionKind` therefore produces a warning in each exhaustive pass that
has not been updated. The warning is deliberately not promoted to an error:
Clang and GCC have disagreed in practice about whether some switches are
exhaustive. These diagnostics complement the authoritative instruction schema:
the schema keeps invariant kind metadata and traversal synchronized, while the
warnings identify handwritten pass logic that must consider a new kind.

Each typed case must terminate with `break`, `return`, or another explicit
control transfer. Falling through into a case for a different concrete kind would
attempt to interpret the original instruction as the wrong kind. Shared bodies
must use an explicitly checked category representation rather than typed-case
fallthrough.

A deliberately partial classifier should normally use `is<T>()` or ordinary
conditionals. Any local suppression of exhaustive-switch checking must be
explicit and exceptional.

## Implementation Validation

The implementation validates the naturally aligned 48-byte, five-slot record,
schema-generated kind metadata, typed CFG terminators, explicit constant
producers, fixed operand walking, and a
PythonCall homogeneous variadic range. Fixed kinds store operands before
attributes; variable kinds store their entire operand array behind slot zero and
their attributes in the remaining inline slots. Snapshot uses a payload-first
parallel descriptor array, and generic traversal visits its result references
without confusing its inline or managed constants for producers. It also defines representative
constants, binary arithmetic, shape and shape-key guards, conditional branches,
calls, Snapshots, and metadata-heavy Core instructions in `instruction.def`.

The instruction arena placement-constructs the concrete subclass selected by
the template. Generated constructors list operands and then attributes, while
the pool privately prefixes the serial. Variadic construction asks the concrete
class for `n_indirect_slots_for(...)`, allocates that side storage, and passes it
to the in-place constructor. A later implementation slice should measure total
graph and side-data use with realistic translated functions.
Success does not require every payload to fit inline. It requires every
representative layout to use the same declarative schema and uniform arena-owned
side data without a handwritten storage or traversal escape hatch. Evidence
that the five-slot record causes excessive side data or cache cost reopens the
sizing decision.

## Rejected Directions

A virtual, behavior-bearing instruction hierarchy would couple storage ownership to polymorphic
deletion and encourage semantic behavior to spread across virtual methods. It
would also retain the `std::deque<std::unique_ptr<Instruction>>` allocation
shape that this representation makes unnecessary.

C++ RTTI and `dynamic_cast` add no useful checking beyond the explicit closed
`InstructionKind` enum. A checked kind comparison followed by a static downcast
to the arena-constructed fieldless subclass is simpler and makes exhaustive
matching possible.

A visitor-based exhaustive dispatcher would scatter a pass across overloads
and obscure the grouping and ordering of related instructions. Direct typed
switches provide the same missing-kind diagnostics while keeping the pass
local. This rejects generated visitor dispatch, not the declarative
`src/jit/instruction.def` schema used to keep instruction invariants
synchronized.

Per-instruction variable-size records could improve density, as in V8
Turboshaft, but would require a relocation-aware operation buffer and index
references instead of stable pointers. Fixed records with arena-owned side
data preserve the desired lifetime and typed-access properties with much less
storage machinery.

Pervasive integer instruction IDs would make every semantic traversal depend
on an instruction container. Stable pointers are the usable reference form;
typed serials provide deterministic identity without that lookup indirection.

## Related Documents

- [JIT Compiler and IR](jit-compiler-and-ir.md)
- [JIT Control-Flow Graph](jit-control-flow-graph.md)
- [Semantic IR and Specialization](jit-semantic-ir-and-specialization.md)
- [JIT Compiler Bring-up Plan](jit-compiler-bring-up-plan.md)
