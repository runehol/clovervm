# JIT Instruction Representation

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | The indexed 16-byte instruction entry, schema-generated typed views, compact operands and attributes, typed CFG terminators, generic operand traversal and reconstruction, deterministic textual IR printing, detachment poisoning, and graph-rewriter integration are implemented; representative storage measurement and complete Snapshot recovery encodings remain |
| Scope | Physical instruction storage, typed instruction access, Core value representations, IR-level legality, phase metadata, effects, matching, and compilation lifetime for Core and Semantic IR |
| Owning layers | The JIT instruction representation owns storage, schema-generated construction, typed access, operand traversal, and reconstruction; the graph builder owns deferred-validation construction; concrete analyses own attached inferred facts and proven-absent effects; the graph rewriter owns staged body-instruction replacement and future CFG editing owns topology mutation |
| Validated against | `tests/test_jit_storage.cpp`, `tests/test_jit_cfg.cpp`, `tests/test_jit_graph_rewrites.cpp`, and `tests/test_jit_ir_print.cpp` |
| Supersedes | The open instruction-representation alternatives in [JIT Control-Flow Graph](jit-control-flow-graph.md) and the integer-only instruction reference direction in [JIT Compiler and IR](jit-compiler-and-ir.md) |

Core IR and the optional Semantic IR store instructions in one
compilation-owned indexed table. `InstructionId` is the persistent identity.
`Instruction` and its concrete subclasses are lightweight typed views
containing the owning `CompilationStorage` and an ID; they do not own or embed
the physical record. Instruction operands store compact IDs, while non-dataflow
attributes use their schema-declared 32-bit or 64-bit encodings.

The representation has deliberately different roles:

```text
InstructionEntry
    fixed-size type-erased table entry
    instruction kind and intrinsic result class
    operand count and storage mode
    three compact inline payload slots

Instruction, AddInstruction, CallInstruction, ...
    storage-relative value views
    typed read-only accessors

SemanticValueAnalysis, CoreEffectAnalysis, ...
    concrete phase-owned metadata indexed by InstructionId
    attached, frozen, invalidated, recomputed, and discarded as required

typed graph-builder construction
    schema-safe append of intrinsically valid, unplaced entries

bulk graph builder
    cheap append/emplace during translation and one-shot publication validation

GraphRewriter
    staged body-instruction replacement through immutable reconstruction
```

## Storage and Lifetime

`CompilationStorage` owns the append-only instruction table, the indirect
operand table, and the stable-address CFG objects:

```cpp
class CompilationStorage
{
public:
    Instruction instruction(InstructionId) const;

private:
    std::vector<InstructionEntry> instructions_;
    InstructionOperandTable instruction_operands_;
    ObjectPool<ControlFlowGraph> graphs_;
    ObjectPool<Block> blocks_;
    std::deque<BlockEdge> block_edges_;
};
```

Instructions are append-only. `InstructionId` is a typed 32-bit vector index;
IDs are never reused during one compilation. Construction checks once that the
table still fits the ID domain. Existing views retain only the ID, so vector
reallocation does not invalidate them.

The physical `InstructionEntry` is an explicitly 8-byte-aligned 16-byte value:

```cpp
class alignas(8) InstructionEntry
{
private:
    uint32_t slots_[3];
    uint16_t kind_;
    uint16_t operand_storage_;
};

static_assert(sizeof(InstructionEntry) == 16);
static_assert(alignof(InstructionEntry) == 8);
```

The high bit of `operand_storage_` records indirect operands. The remaining 15
bits hold the total logical operand count. `kind_` stores the encoded
`InstructionKind`. The slots precede the header so slots zero and one form the
one 8-byte-aligned position available to a 64-bit inline attribute.

`Instruction` is the general typed view:

```cpp
class Instruction
{
public:
    InstructionId id() const;
    const CompilationStorage *storage() const;
    bool is_detached() const;
    InstructionKind kind() const;
    ResultClass result_class() const;
    ValueRepresentation value_representation() const;
    uint16_t operand_count() const;
    bool operands_are_indirect() const;

    template<typename ConcreteInstruction>
    ConcreteInstruction as() const;

private:
    const CompilationStorage *storage_;
    InstructionId id_;
};

static_assert(sizeof(Instruction) == 16);
```

Concrete instruction classes inherit `Instruction` and add no fields. They are
views into `CompilationStorage`, not C++ objects stored in the instruction
vector. `as<T>()` checks the kind and returns the corresponding view by value.

The physical storage tag normally represents one live `InstructionKind`. The
exact encoding may also reserve a poison tag used only after the graph rewriter has
removed an instruction from a published graph. `InstructionKind` remains the
closed semantic enum generated from `src/jit/instruction.def`; the poison tag is
not an instruction kind and is not a state ordinary passes handle. `kind()`,
`result_class()`, typed conversion, and payload traversal all assert that the
tag is live. Only `id()` and `is_detached()` remain meaningful on poisoned
storage, for diagnostics.

Instruction results and operands use distinct enum types. Ordinary operands are
always instruction-result references that participate in SSA use lists,
dominance, liveness, and bulk replacement. Constants become ordinary program
values through explicit `Const` instructions. Snapshot captures those
program-value results like any other value.

Attributes are schema-named semantic or structural payload such as block
edges, shapes, bytecode origins, and return PCs. Each attribute class maps to
one C++ parameter type and one generated encoder/decoder pair, but attributes
do not need a runtime class enum because they are never generically interpreted
as SSA uses. The operand cases intentionally have the same numeric values as
the matching result classes, so validation can compare result references
cheaply without a mapping switch:

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
simple. `BlockEdge` is an attribute instead of an operand because CFG edge
maintenance is not SSA use tracking; the CFG still represents every
control-transfer occurrence with a first-class edge. Operand classes control
structural compatibility, physical decoding, and generic use handling. Core
program-value operands apply the additional `ValueRepresentation` constraint
described below.

`Instruction::result_class()` is decoded directly from the upper bits of
`InstructionKind`; it is not another field in the record or an entry duplicated
in the metadata table. `ProgramValue` says that the instruction defines an SSA
program value, not that analysis knows its precise Python type.
Such inferred facts belong to a concrete phase-owned metadata object such as
`SemanticValueAnalysis`. Changing an instruction's result class requires
replacing the instruction with a different kind. There is no constant
`ResultClass`: `Const` produces an ordinary `ProgramValue` from its immutable
`ValueConstant` attribute. Core does not decide how that value will be
materialized.

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

The schema, rather than a runtime tag beside each word, determines the physical
width and C++ interpretation of every payload:

```text
ProgramValue   -> one InstructionId word wrapped as ProgramValueRef
Snapshot       -> one InstructionId word wrapped as SnapshotRef
BlockEdge      -> one BlockEdgeId word resolved through CompilationStorage
Shape          -> two words containing Shape *
ShapeKey       -> two words containing ShapeKey
ValidityCell   -> two words containing ValidityCell *
BytecodePC     -> one word
ValueConstant  -> two words containing Value
```

`ValueConstant` is the schema classification of that attribute, not a pool
handle or a second runtime wrapper; its typed accessor and constructor use
`Value` directly.

`ValueConstant` may contain either a non-pointer tagged value or a managed
pointer. Every managed constant placed in a graph is retained by the compilation
session because instruction payloads are not GC-scannable relocation slots. An
existing constant is registered with `retain_and_pin_value()` when graph
construction encounters it. A compiler-created value is registered with the
same operation immediately after creation.

All attributes are inline and have kind-constant offsets. Attribute layout is
partly schema-authored: a 64-bit attribute must begin at an aligned slot.
Generated accessors assert the required offset and width at compile time. Wide
values are encoded and decoded with `memcpy`, avoiding object-lifetime and
aliasing violations.

For direct operands, fixed operands occupy the leading slots and attributes
follow them. The complete layout must fit in three slots and satisfy every
attribute-alignment assertion.

For indirect operands, attributes are laid out from slot zero as though the
instruction had no inline operands. Slot two stores the 32-bit offset into
`InstructionOperandTable`. The schema requires the attributes plus this offset
to fit in the three inline slots. Indirection is a generated storage property:
an instruction is indirect when it is variadic, when operands and attributes
do not fit inline, or when direct placement would misalign an attribute.

A variable operand range is always the final operand declaration. All fixed
operands therefore occupy kind-constant leading indices in the indirect array,
followed by the variable tail. For example, `PythonCall` stores its callable at
operand zero, its Snapshot at operand one, and arguments beginning at operand
two. Typed access to the fixed operands needs no runtime argument count.

`InstructionOperandTable` is one compilation-owned `std::vector<uint32_t>`.
Each indirect instruction stores its operands in one contiguous range:

```cpp
struct InstructionOperandTable::Allocation
{
    uint32_t offset;
    std::span<uint32_t> words;
};
```

Snapshot is a representation-erased positional variadic range. A value-bearing
position stores one `ProgramValueRef` word, and the referenced def kind
supplies its concrete representation. This includes frame-header positions:
raw PC and FP contents require a non-tagged program-value representation, while
the return code object is tagged. The exact non-tagged representation remains
deferred. No position uses a nullable or structural escape encoding.

Each generated variadic class exposes hidden construction machinery
`n_indirect_slots_for(constructor arguments...)`. Compilation storage uses it
to allocate the physical operand-table span before appending the entry. It is
deliberately separate from `operand_count()`: the former sizes storage, while
the latter counts logical operands.

The 15-bit count is an explicit representation limit. Generated construction
asserts that an instruction fits it; exceeding the limit is a compiler logic
error, not a partially valid instruction state.

Construction may use a mutable buffer before completing the instruction, but
stored and typed access decodes the declared operand or attribute class and
exposes immutable typed values and range views such as
`ProgramValueRefRange<F64>`. Clients cannot replace an operand through those
views.

The operand table allocates the final range first and returns a mutable span to
the factory. The factory writes operand words directly into that range. A typed
operand-range view retains the instruction view, logical offset, and count; it
resolves the current table span when accessed, so later operand-table vector
growth cannot leave a dangling span.

`InstructionEntry` is trivially destructible and movable. Runtime pointers in
attributes are borrowed from objects whose lifetimes cover compilation.
Normally destroyed storage tables, CFG pools, retained values, and scoped
resources are owned by `CompilationStorage` or the enclosing
`CompilationSession`.

### Construction, Placement, and Publication

Allocation and graph placement are separate operations. The typed builder API
takes the compilation session, appends an intrinsically valid entry to its
storage, and returns an unplaced typed instruction view. It does not need an
insertion position:

```cpp
AddSMIInstruction sum =
    builder.make_instruction<AddSMIInstruction>(lhs, rhs, snapshot);
```

The constructor surface is generated from `instruction.def`.
Operand and attribute traits select the
C++ parameter type and encoding for each declared slot, representation traits
refine Core program-value operands, and the template returns the requested typed
view. Consequently, ordinary callers cannot choose an
inconsistent kind, result class, operand class, attribute class, Core
representation, arity, or payload layout.
Future IR-level-specific wrappers may expose only instruction kinds permitted at
that level. The generated metadata still records each kind's allowed
levels for placement and verification.

Instruction construction does not generically scan instructions for managed
constants. Graph-building code calls
`GraphBuilder::retain_and_pin_value()` as it encounters each existing pointer
constant before publishing that value through a `Const`. A pass that creates a
new managed value uses `CompilationSession::retain_and_pin_value()` directly,
or the same capability on its `RewriteContext`. Failure to retain a value is a
resource failure that aborts the compilation.

This compile-time construction safety does not attempt to prove contextual
graph properties such as dominance or block-edge ownership. There are two
placement paths with deliberately different validation costs.

A translator or major lowering uses a bulk `GraphBuilder`. Construction and
rewriting APIs use `make` for storage allocation without attachment, `append` for
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
graph membership, result and operand classes, live defs, block-edge
ownership, terminator placement, local definition-before-use, and other
structural invariants. It does not build optional `UseLists` merely to
perform this validation. A graph under bulk construction is not published to
ordinary passes. If final verification finds an invalid graph, that is a
compiler logic error: it reports the structural diagnostic and hard-asserts
rather than turning the bug into an interpreter fallback.

Once a graph is published, body-instruction transformations use
`GraphRewriter`. The callback allocates replacement instructions through a
narrow `RewriteContext`; the rewriter reconstructs changed operands, builds one
staged vector per block, verifies the proposed stream, swaps all changed block
vectors at one graph-wide commit, poisons removed instructions, and increments
the mutation generation once. Optional `UseLists` describe the original
published generation and are invalidated rather than incrementally maintained.
CFG-topology mutation remains a separate future editor responsibility. Neither
path is a mandatory route through which an unplaced instruction must be
allocated.

Placement and liveness are graph-owned state rather than another physical
instruction tag. The normal lifetime progression is:

```text
allocated and unplaced -> placed in one graph -> removed from graph
```

An unplaced instruction has a live instruction kind and a schema-valid payload,
but is not yet a member of any graph. It may be attached at most once. After
removal, the rewriter may poison the abandoned storage with the reserved detached
tag to catch stale references. Poisoning is not an allocation-reuse mechanism,
not graph membership, and not a semantic IR state.

### Compilation Failure and Runtime Publication

Graph publication above means making a finalized IR visible to compiler passes.
Runtime publication here means installing completed machine code and persistent
dependencies. The latter occurs only after all compiler phases succeed.

The JIT distinguishes compiler logic errors from resource failure. Violating an
instruction-schema, graph, rewriter, or pass invariant is a compiler bug. Such a
violation hard-asserts, with the verifier and stable-ID diagnostics used to
identify the responsible pass. It is not reported as an ordinary inability to
compile and must not silently fall back to the interpreter.

Allocation exhaustion and comparable resource failures are expected compilation
failures. Fallible storage, operand-table, index, and code-buffer allocation propagates
an explicit compilation failure such as `CompileFailure::AllocationFailure` to
the JIT entry point. The entire compilation session, including any partially
built or partially rewritten graph, is then abandoned, and execution continues
in the interpreter. The rewriter does not need to roll a graph back into a usable
state after such a failure because no later pass may observe that compilation.

Instructions, blocks, edges, operand data, and other compiler storage are owned
by the compilation session; when compilation fails, allowing that session to
leave scope releases the entire compilation domain. The
compiler does not need to discover and individually undo partially constructed
IR objects. Normally destroyed compilation tables and scoped external
registrations remain owned by the enclosing compilation session and unwind
alongside its storage.

This failure model does not permit leaked external state. The compilation
session retains managed constants and similar runtime-visible resources, and
releases them when an unsuccessful session is destroyed. Compiled code,
validity-cell
dependencies, assumptions, cache entries, and other persistent runtime state
are installed only after all fallible compilation work and final verification
have succeeded. Publication is the final commit; failure before it leaves
previously executing interpreter and compiled state unchanged.

Editor transactions therefore exist to hide deliberately incomplete multi-step
rewrites and to validate their completed structure, not to roll back allocation
failure. On the successful path, an edit must leave the published graph valid.
On resource failure, the enclosing compilation is discarded.

### Managed Value Constants and Compilation Retention

`Const` instructions embed constants directly as `ValueConstant`s. Core does
not distinguish values that will become machine
immediates from values that will require the traced constant pool, and does not
assign a pool index or otherwise model that eventual pool.

Every pointer-valued constant embedded in Core is retained in one monotonic
session vector of `Owned<Value>`. For an existing compiler input,
`retain_and_pin_value()` adds the retain required to keep its address usable
from unscannable instruction storage. A newly created managed value, such as a
tuple produced by constant folding, has no durable source owner; the same
operation adds its ownership retain immediately and returns the same typed
handle. `GraphBuilder` and `RewriteContext` both expose this capability.

Retained values are session state rather than fields in instruction entries.
Instruction construction does not infer ownership from a
`ValueConstant`; the builder or transformation registers the value at the
point where it encounters or creates it. In the current collector, the
`Owned<Value>` is both the lifetime root and the compilation pin. A future
moving collector must preserve the address of these retained values while the
session is active because it cannot rewrite the copies embedded in instruction
payloads. The initial vector permits duplicates and is released as a unit when
the session ends.

Backend or Machine-IR lowering classifies each surviving `Const`. It may encode
or synthesize a suitable non-pointer value as an immediate, or pass any `Value`
to `MachineCodeEmitter::add_value_to_constant_pool()`. The emitter owns pool
values and assigns and deduplicates final `ValuePoolEntry` offsets. Pointer-valued
constants must take the traced-pool path; Core itself does not express this
split.
Successful publication initializes the `JitCodeObject` pool and establishes its
ownership before session-retained constants are released. On failure, the
emitter owners, retained values, and compilation storage are discarded while
the interpreter continues.

Allocation may request a future safepoint but does not synchronously enter one.
Initial compilation does not acknowledge safepoints mid-compilation, so a pass
may create a managed value and immediately retain it in the session before
making it available to later compiler work. Possible future phase-boundary
yielding is specified in [JIT Compiler and IR](jit-compiler-and-ir.md).

## Indexed Identity and Determinism

Persistent instruction relationships use typed 32-bit IDs. Relationships that
consume results use typed wrappers around those IDs:

```cpp
ProgramValueRef lhs;
ProgramValueRef rhs;
SnapshotRef snapshot;
BlockEdge *true_edge;
BlockEdge *false_edge;
```

An instruction ID is meaningful only within its owning `CompilationStorage`.
APIs that dereference a reference therefore receive storage explicitly or
operate through an `Instruction` view that already carries it. A
`ControlFlowGraph` borrows its owning storage, and block instruction and
parameter accessors return resolving views over their stored ID sequences.
Explicit `instruction_ids()` and `parameter_ids()` accessors remain available
for algorithms that need compact identities.

Blocks and block edges remain stable-address CFG objects because they are few
and mutation-heavy. Branch attributes store compact `BlockEdgeId`s and resolve
them through compilation storage. Compiler output must not depend on CFG object
addresses or unordered pointer-container iteration. Defined graph traversal
order and typed IDs provide diagnostics, stable tie-breaking, and deterministic
ordering.

The result-reference wrappers are mandatory for every result-consuming field:

```cpp
class ProgramValueRef
{
    InstructionId instruction_;
};

class SnapshotRef
{
    InstructionId instruction_;
};
```

Constructing a result reference validates that the def's intrinsic
`ResultClass` matches the class required by the wrapper. A
`ResultClass::None` instruction cannot be referenced as an operand, a Snapshot
cannot be used as a program value, and a program value cannot be used where
recovery state is required. When validating an encoded operand slot that consumes
an instruction result, the verifier uses `operand_accepts_result()` rather than a
mapping switch. Generated construction signatures make mismatched classes
unrepresentable to ordinary callers, structural operand replacement checks the
declared operand class, and the verifier independently checks the encoded
payload. Instruction-list placement and instruction-indexed analyses use
`InstructionId`; a typed reference does not carry a storage pointer.

Generic traversal, dominance, use discovery, and Semantic IR use the erased
`ProgramValueRef`. Core typed APIs refine it without changing its 32-bit
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
Refining an erased Core reference validates the def's intrinsic
representation. Fixed-representation generated constructors and accessors use
the refined wrapper, making common mismatches C++ type errors; generic
infrastructure deliberately retains the erased form.

## Concrete Typed Instructions

`src/jit/instruction.def` is the authoritative schema for the closed set of
live instruction kinds. Each definition names the instruction kind and typed
view, the IR level or levels in which it is legal, its intrinsic
`ResultClass`, its `MustEffects` lower bound and `MayEffects` upper bound, its
payload shape, every fixed or variable operand slot with its `OperandClass`,
and every immutable payload attribute with its schema attribute class. Every ordinary
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

AndSMI, OrrSMI, EorSMI
    result: ProgramValue(TaggedValue)
    lhs: ProgramValue(TaggedValue)
    rhs: ProgramValue(TaggedValue)
    effects: none; binary SMI bitwise operations cannot overflow

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
    snapshot: Snapshot

ValidityCellGuard
    result: ProgramValue(TaggedValue)
    value: ProgramValue(TaggedValue)
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
    captured_values[]: snapshot operand ProgramValue(TaggedValue)
    resume_pc: attr BytecodePC

ConditionalBranch
    result: None
    condition: ProgramValue(TaggedValue)
    true_edge: attr BlockEdge
    false_edge: attr BlockEdge
```

Snapshot positions capture ordinary `ProgramValueRef`s. Position zero denotes
the accumulator. Position one denotes the highest canonical stack slot:
parameter zero for a function with parameters, or the highest frame-header slot
for a zero-parameter function. Later positions proceed through consecutive
descending stack addresses. The sequence is therefore accumulator, parameters,
parameter padding, fixed frame-header slots, locals, temporaries, and then the
next inlined frame's parameters, which are the caller's outgoing arguments.
Its header follows those parameters, and the pattern repeats for further
inlined frames. Outgoing arguments and callee parameters are one physical and
logical slot sequence, never duplicated.

A Snapshot may capture any prefix of the CFG's complete ordering, but cannot
omit an interior position. An unavailable accumulator, dead temporary, or
padding slot is represented by an `Uninitialized` definition. Operand count
therefore identifies the captured extent.

Header positions contain program values for the interpreted PC, compiled PC,
frame pointer, and return code object. The two PCs and frame pointer use a raw
non-tagged representation rather than tagged-value encoding; the code object
uses `TaggedValue`. The exact non-tagged representation is deferred. Pure
definitions of these contents may be sunk into
recovery, after which frame synchronization writes them to their canonical
homes.

Generated generic Snapshot traversal reports every captured program value as
an operand use, including header values. A tagged frame value in another
representation first passes through an explicit conversion such as `BoxF64`;
the conversion may later be marked sunk so it executes only during recovery.
Side-exit frame-sync generation stores tagged program values directly, may
rematerialize captured `Const` defs, and boxes captured `F64` values before
writing them to the interpreter frame.
Adding another alternative or representation therefore requires an exhaustive
frame-materialization case. No arithmetic, call, forwarding, parameter, or
other Core instruction may accept an erased representation.

Core represents every ordinary use of a constant through a normal
`ProgramValueRef` produced by `Const`; constants are not embedded in use
operands. Backend preparation or Machine IR chooses immediate synthesis or a
constant-pool load. Pointer-valued constants must use the traced pool, while a
non-pointer value may use either form according to target encodability and
profitability. The phase also selects lowerings and `AllocationConstraints`
before register allocation runs. Immediate shape rules, including any future
target-specific single-use immediate nodes, remain backend policy rather than
Core IR legality.

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

Each concrete instruction class also exposes the schema position of every
operand as static `constexpr` data, such as
`ReturnInstruction::return_value_operand_index`. Target preparation can
therefore anchor a sparse operand constraint without repeating a positional
integer or switching on the instruction kind.

Defining separate kinds keeps their generated construction and access APIs
concrete and prevents a representation-polymorphic instruction from becoming
an unchecked escape hatch. Exact macro spelling remains an implementation
detail.

No `OperandClass`, attribute-class tag, or `ValueRepresentation` tag is stored
beside each ordinary payload word. Generic code reads the instruction kind once
and selects a schema-generated per-kind enumerator. Conceptually:

```cpp
struct OperandSlotDescriptor
{
    OperandClass operand_class;
    ProgramValueConstraint representation_constraint;
    SlotLayout layout;  // Fixed or variable-length.
    uint16_t offset;
};

void visit_operands(const Instruction &instruction, OperandVisitor visitor);
```

The generated dispatch interprets each payload word only according to the
schema for that instruction kind. `ProgramValue` and `Snapshot` operands are
ordinary result-reference uses for SSA, liveness, and rewriting. Attribute
slots such as `BlockEdge`,
`Shape`, `ShapeKey`, `ValidityCell`, bytecode PCs, and value
constants are immutable semantic payload; they are skipped by generic
use discovery and result replacement. CFG maintenance, verification, printing,
and cloning inspect attributes through generated typed accessors and
schema-expanded code.

`Snapshot` is the one explicit exception to ordinary local-use behavior. It is
a zero-code aggregate result whose captured `ProgramValue` operands become point
uses at every guard or side exit that consumes the `SnapshotRef`. Liveness
expands a Snapshot operand transitively at that consuming position, so several
nearby guards may safely share one Snapshot without treating its captured
values as dead after the Snapshot instruction itself. Verification keeps the
Snapshot anchored near its uses and on the correct side of effect
boundaries. This special case is preferred over a general per-slot role axis;
the design should revisit that choice if another same-class relationship needs
different generic behavior.

An instruction definition may name one IR level or an explicit set when the
same semantic kind is valid in more than one IR. Allowed levels are kind
metadata and consume no space in an instruction. The implemented CFG is Core
and its builder, rewriter, and verifier reject kinds not declared Core-legal.
If Semantic or Machine IR later reuse the same graph representation, the graph
will gain one immutable level discriminator and level-specific construction and
analysis façades.

For Core graphs, verification additionally requires every `ProgramValue` def to
have one legal representation, every operand constraint to match its def, and
every representation-changing edge to be an explicit conversion instruction.
Every Snapshot position must reference a live `ProgramValue` def whose
representation is valid for the corresponding position in the CFG's canonical
ordering description. Recovery reaches through any sunk conversion rather than
interpreting a type-erased Snapshot operand.

Each concrete instruction form is a fieldless value-view subclass of
`Instruction`. Compilation storage always stores an `InstructionEntry`;
requesting a concrete kind constructs the entry through the generated schema
and returns the corresponding view. Because subclasses add neither fields nor
virtual dispatch, every concrete view has the same two-word representation as
`Instruction` while providing kind-specific read-only accessors:

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
    friend class CompilationStorage;
    AddF64Instruction(const CompilationStorage *storage, InstructionId id);
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
instruction object. Ordinary code holds `Instruction` by value for
heterogeneous IR structure and requests a kind-checked concrete view when it
needs typed access.

There is one semantic `InstructionKind` enum generated from
`src/jit/instruction.def`; the generated `InstructionOrdinal` exists only for
compact table indexing. Each concrete subclass declares its own
`static constexpr Kind`, and schema-generated validation requires it to match
the view mapping in the definition. Checked conversion uses the type itself as
the source of the expected kind:

```cpp
template<typename TypedInstruction>
TypedInstruction Instruction::as() const
{
    assert(kind() == TypedInstruction::Kind);
    return TypedInstruction(storage_, id_);
}
```

`is<T>()` and `try_as<T>()`, if useful, follow the same mapping. They use an
enum comparison followed by typed view construction. The design does not use
`dynamic_cast`, `typeid`, virtual instruction methods, or C++ object-lifetime
reinterpretation.

Category views may later represent a deliberately defined set of kinds with a
common payload shape. Such a view has no single `Kind`, so it is obtained
through a separate membership-checked API such as `as_category<T>()`; it cannot
be used with `CL_JIT_INSTRUCTION_CASE`. Category views are deferred until an
actual grouped operation family justifies them.

## Immutable Instructions and Detachment

An instruction's kind, result class, Core value representation, constants,
bytecode origin, payload shape, and attributes are immutable
after construction. A pass that changes one of these properties constructs an
unplaced replacement through the appropriate instruction factory, then asks the
graph rewriter to place it in the staged replacement stream.

Removal from a graph is not instruction mutation. The graph rewriter may poison the
abandoned allocation as the final step of removal:

```text
Live(fixed InstructionKind) -> removed from graph -> poisoned storage
```

The graph rewriter poisons removed originals only after every block's staged
instruction vector has committed. Result replacement and schema-generated
reconstruction ensure no committed user retains a reference to an erased def;
encountering such a use is a hard compiler fault. A detached instruction's
pointer-valued `ValueConstant` is no longer semantically visible. Every value
registered with the session remains retained until session teardown, so
detachment does not prune that monotonic vector. Poisoned storage is never
republished or returned to a live kind.

The ID is deliberately preserved for diagnostics. Any detached instruction
encountered by verification, generic traversal, typed conversion, a result
reference, or current `UseLists` is a hard compiler bug. The diagnostic reports
the preserved ID and does not interpret the poisoned payload. Ordinary pass
code does not branch on detachedness as a supported case; instruction IDs and
views must not be reused across structural edits without proving that the
instruction remains attached.

Operand and attribute slots have no public mutable access. When an earlier def
has been replaced, the schema-generated reconstruction API rebuilds each later
instruction with the resolved operands and copies its immutable attributes.
It enforces `OperandClass` and `ValueRepresentation` through the generated
constructor types. A representation-changing rewrite must emit an explicit
conversion rather than connecting incompatible refs.

The generic operand walker remains the common primitive for verification,
reconstruction, and on-demand `UseLists`. A use occurrence is identified by
its user instruction and `uint32_t` operand index; the instruction schema
recovers the operand's class and representation. `UseLists` are immutable,
generation-bound analysis of the original graph. Rewriting invalidates the
cache instead of editing it occurrence by occurrence.

The complete traversal, query, rewrite-result, normalization, staging, and
commit contracts live in
[JIT IR Graph Rewrites](jit-ir-graph-rewrites.md). They are not duplicated as
an independent mutable-slot editor API here.

The same walker independently reconstructs uses for verification and also
supports cloning and printing of operand relationships. Typed accessors and
schema-expanded code handle cloning, printing, CFG edge maintenance, constant
diagnostics, and bytecode-PC diagnostics for non-dataflow payload. The verifier
compares any current `UseLists` against reconstructed operand records and
hard-fails on references to poisoned storage.

## Phase-Owned Attached Metadata

Inferred types, proven-absent effects, dependencies, locations, and similar derived
facts are not part of the physical instruction representation. They live in
explicit metadata objects owned by the phase and IR level that defines them,
for example:

```text
SemanticValueAnalysis   Semantic ProgramValueRef -> ValueFacts
CoreEffectAnalysis      Core InstructionId       -> ProvenAbsentEffects
LocationAssignments     Core ProgramValueRef     -> backend locations
UseLists                InstructionId            -> temporary UseRecords
```

Core `ValueRepresentation` is deliberately not an attachment. It is an
immutable def and operand contract used to type the SSA graph itself.
Register, spill, and constant locations remain backend-owned attached metadata.
A late sinking analysis may likewise attach its decisions to the graph without
mutating instruction payloads. A sunk instruction keeps its ordinary kind,
result class, representation, and operands; the attachment changes whether its
result must physically exist on the normal path.

This is not a generic per-instruction property bag. Each attachment is a
concrete type with its own invariants, key domain, mutation rules, and permitted
graph level. Clients query it with an instruction ID, instruction view, or typed
result reference as appropriate. Dense implementations use the ID directly as
a vector index.

Mutable analysis builds or updates its private table and publishes a frozen
view tagged with the source graph and mutation generation. A phase-specific
query façade validates the generation before exposing the table; individual
hot accessors need not repeat the same check. Structural mutation makes old
frozen views stale. The baseline response is broad invalidation and
recomputation of any analysis a later pass still needs. A future analysis may
consume rewriter mutation descriptions to preserve unaffected entries or
recompute only affected dependents, but that is an optimization justified by
measured cost, not a requirement of the instruction representation.

Attachments exist only while a later phase consumes them. Semantic value facts
may be discarded after Semantic-to-Core lowering; Core effect information may
be discarded after effect-dependent optimization; backend location data has
its own later lifetime. Committing a rewrite invalidates graph-generation-bound
attachments before any later client may query their entries for removed
instructions. Major representation boundaries may build a fresh graph while
using the same instruction, CFG, ID, and compilation-storage machinery.

## Kind Effect Bounds and Proven Absence

Each instruction kind declares two immutable effect bounds in
`src/jit/instruction.def`:

- `MustEffects` is the lower bound: effects necessarily performed by every
  instance of that kind and therefore never removable by analysis.
- `MayEffects` is the upper bound: every effect that any instance of that kind
  may perform without changing instruction kind.

The currently implemented coarse vocabulary is:

```cpp
enum class EffectProfile : uint8_t
{
    None = 0,
    SideExit = 1 << 0,
    Allocate = 1 << 1,

    PythonVisibleEffects = 1 << 2,
    CallPython = PythonVisibleEffects,
    ControlFlow = 1 << 3,
    TerminateBlock = 1 << 4,
};
```

These are composable flags, but their powers-of-two ordering also establishes
the initial DCE boundary: any complete profile numerically below
`PythonVisibleEffects` is discardable when its result is dead. Thus allocation
and a side exit are not by themselves Python-visible effects. An instruction
that can change control flow is not discarded merely because the exit itself
is invisible to Python. `TerminateBlock` is used together with `ControlFlow`.
This coarse boundary is implemented; the refined dependency and
proven-absence analyses below remain later work.

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
    if (const Type variable =                                         \
            cl_jit_instruction_switch_value.as<Type>();               \
        false)                                                        \
    {                                                                 \
    }                                                                 \
    else
```

A code-generation pass then reads as an ordinary match:

```cpp
CL_JIT_INSTRUCTION_SWITCH(instruction)
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

## Textual IR

The canonical textual form is a concise SSA printer intended for diagnostics
and golden tests:

```text
graph {
bb0(%0):
  %1 = const {constant = 7}
  %2 = snapshot [%0, %1] {resume_pc = 17}
  cond_br %0 {true_edge = bb1(%1), false_edge = bb1(%0)}

bb1(%3):
  return %3
}
```

The printer assigns dense block and result numbers in graph order. These names
do not expose storage IDs, allocation addresses, or rewrite history. Tagged
block parameters omit their representation; a non-default representation is
written after the parameter, such as `%2: f64`. Instruction result
representations are inferred from the operation kind. Snapshot results use the
same `%N` namespace and do not receive machine locations.

Fixed operands are positional. A trailing variadic operand range is enclosed in
brackets so fixed and variadic fields remain visually distinct:

```text
%4 = python_call %0, %2, [%1, %3] {interpreter_return_pc = 24}
```

Attributes follow the operands in a curly-brace dictionary. Attribute names and
ordering come directly from `instruction.def`; operand names are intentionally
omitted. Block-edge attributes print their target and argument vector.
Pointer-valued metadata receives deterministic print-local symbolic identities
rather than exposing native addresses.

Nonzero block loop depth is printed as block metadata:

```text
bb1(%3) {loop_depth = 2}:
```

Depth zero is the default and is omitted.

`format_ir()` prints a complete graph. `format_instruction()` uses the same
graph-local numbering to print one instruction, and the `fmt` formatter for
`ControlFlowGraph` delegates to the canonical graph printer. Adding a new
instruction kind automatically covers its declared operands and attributes;
adding a new attribute class requires one explicit formatting rule.

## Implementation Validation

The implementation validates the explicitly 8-byte-aligned 16-byte entry, three
32-bit inline slots, schema-generated kind metadata, typed CFG terminators,
explicit constant defs, direct and indirect operand walking, wide-attribute
alignment, and the PythonCall and Snapshot variadic ranges. Direct layouts
store operands before attributes. Indirect layouts store attributes from slot
zero, their operand-table offset in slot two, and every operand in the table.
The current generic traversal visits every stored Snapshot capture as a result
reference.

Compilation storage appends the `InstructionEntry` produced by the generated
typed factory and returns the corresponding instruction view. Indirect
construction asks the concrete class for `n_indirect_slots_for(...)`, allocates
that operand-table range, writes it, and records the returned offset. Tests
force both instruction-vector and operand-table reallocation while IDs, views,
and operand-range views remain live. A later measurement slice should report
entry and operand-table use for realistic translated functions.
The current Snapshot constructor accepts `ProgramValueRef`s for every captured
position. Adding the eventual non-tagged header representation and verifying
each prefix against the CFG's canonical ordering description remain part of
the edge-argument and recovery integration slices.
Success does not require every payload to fit inline. It requires every
representative layout to use the same declarative schema and uniform operand
table without a handwritten storage or traversal escape hatch.

## Rejected Directions

A virtual, behavior-bearing instruction hierarchy would couple storage
ownership to polymorphic deletion and encourage semantic behavior to spread
across virtual methods. It would also require separately allocated instruction
objects instead of the compact table.

C++ RTTI and `dynamic_cast` add no useful checking beyond the explicit closed
`InstructionKind` enum. A checked kind comparison followed by construction of a
fieldless typed view is simpler and makes exhaustive matching possible.

A visitor-based exhaustive dispatcher would scatter a pass across overloads
and obscure the grouping and ordering of related instructions. Direct typed
switches provide the same missing-kind diagnostics while keeping the pass
local. This rejects generated visitor dispatch, not the declarative
`src/jit/instruction.def` schema used to keep instruction invariants
synchronized.

Per-instruction variable-size records could improve density, as in V8
Turboshaft, but would make random indexed access and fixed entry layout more
complex. Fixed 16-byte entries plus a separate operand table retain constant-time
lookup and compact ordinary instructions.

Stable instruction pointers would make the common view cheaper to dereference,
but would either prevent dense vector storage or require a segmented container.
The selected design pays storage-assisted lookup in exchange for four-byte
persistent references, movable entries, and direct dense indexing.

## Related Documents

- [Decision Log](decision-log.md)
- [Indexed JIT Instruction Storage Refactoring](jit-indexed-instruction-storage-plan.md)
- [JIT Compiler and IR](jit-compiler-and-ir.md)
- [JIT Control-Flow Graph](jit-control-flow-graph.md)
- [JIT Register Allocation](jit-register-allocation.md)
- [Semantic IR and Specialization](jit-semantic-ir-and-specialization.md)
