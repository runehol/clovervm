# JIT Instruction Representation

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Not started; the current virtual instruction hierarchy is temporary CFG scaffolding |
| Scope | Physical instruction storage, typed instruction access, IR-level legality, phase metadata, effects, matching, and arena lifetime for Core and Semantic IR |
| Owning layers | The JIT instruction representation owns storage, schema-generated construction, and typed access; the bulk graph builder owns deferred-validation construction; concrete analyses own attached inferred facts and analyzed effects; the CFG editor owns incremental structural mutation |
| Validated against | N/A |
| Supersedes | The open instruction-representation alternatives in [JIT Control-Flow Graph](jit-control-flow-graph.md) and the integer-only instruction reference direction in [JIT Compiler and IR](jit-compiler-and-ir.md) |

Core IR and the optional Semantic IR use a fixed-size, type-erased
`Instruction` allocated with compilation lifetime. Instruction-result operands
are pointers, while other semantic inputs use their schema-declared
pointer-sized encodings. Each allocation also carries a typed serial for
deterministic identity, diagnostics, and ordering. Read-only typed instruction
views provide kind-specific access without an instruction class hierarchy,
virtual dispatch, or C++ RTTI.

The representation has deliberately different roles:

```text
Instruction
    fixed-size stored object
    stable address and serial
    instruction kind and intrinsic output class
    encoded kind-specific payload

AddInstruction, CallInstruction, ...
    short-lived read-only typed views over Instruction

SemanticValueAnalysis, CoreEffectAnalysis, ...
    concrete phase-owned metadata indexed by instruction
    attached, frozen, incrementally updated, and discarded as required

IR-level instruction factory
    schema-safe allocation of intrinsically valid, unplaced instructions

bulk graph builder
    cheap append during translation and one-shot publication validation

CFG editor
    checked incremental insertion, removal, and instruction replacement
```

## Storage and Lifetime

The physical `Instruction` has one fixed size. The exact number and widths of
its payload words will be selected while implementing representative
instructions; this design does not yet fix them. Conceptually it contains:

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
    SlotClass output_class() const;

    template<typename TypedInstruction>
    TypedInstruction as() const;

private:
    Serial serial_;
    InstructionStorageTag tag_;
    InstructionPayload payload_;
};
```

The physical storage tag represents either one live `InstructionKind` or the
reserved `Detached` state. The exact encoding may reserve one tag value rather
than adding another per-instruction field. `InstructionKind` remains the closed
semantic enum generated from `src/jit/instruction.def`; `Detached` is a storage
lifetime state, not an instruction kind. `kind()`, `output_class()`, typed
conversion, and payload traversal all require a live tag. Only `serial()` and
`is_detached()` remain meaningful after detachment.

One shared classification describes both what an instruction may produce and
what semantic entity an input slot may contain:

```cpp
enum class SlotClass
{
    None,
    ProgramValue,
    Snapshot,
    BlockEdge,
    Shape,
    ShapeKey,
    ValidityCell,
    ConstantValue,
};
```

`None`, `ProgramValue`, and `Snapshot` are legal instruction output classes.
`ProgramValue`, `Snapshot`, `BlockEdge`, `Shape`, `ShapeKey`, `ValidityCell`,
and `ConstantValue` are legal input-slot classes; `None` can never describe an
input. `BlockEdge` is used instead of a direct block reference because the CFG
represents every control-transfer occurrence with a first-class edge.
`SlotClass` controls compatibility, physical decoding, and the default generic
handling of the slot.

`Instruction::output_class()` is derived from instruction-kind metadata; it is
not another field in the record. `ProgramValue` says that the instruction
defines an SSA program value, not that analysis knows its precise Python or
representation type. Such inferred facts belong to a concrete phase-owned
metadata object such as `SemanticValueAnalysis`. Changing an instruction's
output class requires replacing the instruction with a different kind.

The record may contain raw pointers, serials, enums, flags, scalar immediates,
and other trivially destructible values. It must not directly contain an
owning `std::vector`, `std::string`, `std::unique_ptr`, reference-counted
handle, or any value whose destructor releases resources.

Payload input slots are pointer-sized words. The schema, rather than a runtime
tag stored beside each word, determines their C++ interpretation:

```text
ProgramValue   -> Instruction* wrapped as ProgramValueRef
Snapshot       -> Instruction* wrapped as SnapshotRef
BlockEdge      -> BlockEdge*
Shape          -> Shape*
ShapeKey       -> inline ShapeKey value
ValidityCell   -> ValidityCell*
ConstantValue  -> inline Value
```

The implementation enforces that every encoded class fits the slot size and
alignment. Variable-length homogeneous inputs and unusually large instruction
data use pointer/count references to compilation-arena allocations:

```cpp
using InstructionSlot = uintptr_t;

struct InstructionSlots
{
    const InstructionSlot *data;
    uint32_t size;
};
```

Construction may use a mutable buffer before completing the instruction, but
stored and typed access decodes the declared slot class and exposes immutable
typed values, for example `absl::Span<ProgramValueRef>`. Clients cannot replace
an input by assigning through the span.

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
or by copying trivially copyable payload values with `memcpy`. Typed views must
not reinterpret generic byte storage as an object whose lifetime has not begun,
read inactive union members, or assume alignment that the payload does not
provide. Side-data allocation checks alignment and allocation-size overflow.

The compilation arena releases instruction and trivial side-data storage in
bulk. It does not walk those objects to invoke destructors. The same common
arena may also own normally destroyed tables and pools; common compilation
ownership does not imply that every object occupies the destructor-free
allocation domain. The current `PolymorphicObjectPool<Instruction>` and virtual
destructor belong to the temporary CFG scaffolding and will be replaced when
this representation is implemented.

### Construction, Placement, and Publication

Allocation and graph placement are separate operations. An IR-level-specific
instruction factory allocates from the compilation arena and returns an
intrinsically valid, unplaced instruction. It does not need a CFG editor or
insertion position:

```cpp
CoreInstructionFactory factory(arena);
ProgramValueRef add = factory.make_add(lhs, rhs);
```

The factory surface is generated or validated from `instruction.def`.
`SlotTraits<SlotClass>` selects the C++ parameter type and encoding for each
declared input, while output traits select the typed result returned by the
constructor. Consequently, ordinary callers cannot choose an inconsistent
kind, output class, input class, arity, or payload layout. IR-level-specific
factory surfaces expose only instruction kinds permitted at that level. The
exact macro spelling and whether generated functions delegate to shared
templates are implementation details.

This compile-time construction safety does not attempt to prove contextual
graph properties such as dominance or block-edge ownership. There are two
placement paths with deliberately different validation costs.

A translator or major lowering uses a bulk `GraphBuilder`. Its common
`append()` operation attaches an unplaced instruction in amortized constant
time and performs only work naturally local to that append, such as extending
the block's instruction sequence. It does not rescan dominance, repeatedly
verify the partially built graph, or otherwise turn a linear translation into
a quadratic algorithm. The builder may defer use indexes, CFG indexes, and
other derivable structures when building them once is cheaper than maintaining
them incrementally.

The builder's `finalize()` operation constructs deferred indexes and validates
the completed graph in one `O(instructions + edges + inputs)` pass. It checks
IR-level legality, graph membership, result and input classes, live producers,
block-edge ownership, terminator placement, dominance, and other structural
invariants. A graph under bulk construction is not published to ordinary
passes; failed finalization returns diagnostics rather than exposing a
partially valid graph. Region construction may similarly attach a batch of
mutually referring unplaced instructions before validating the batch in its
completed context.

Once a graph is published, local transformations use the CFG editor. The
editor attaches factory-created instructions, rewrites inputs, updates active
indexes and mutation generations, and detaches replaced instructions. It may
check contextual invariants eagerly when those checks are constant-time or
already maintained incrementally. A transformation that performs many related
edits may use an editor transaction that defers global verification until
commit; ordinary passes cannot observe the intermediate graph. The editor is
therefore the authority for mutating a published graph, not a mandatory route
through which every instruction must be allocated.

Placement is graph-owned state rather than another physical instruction tag.
The lifetime progression is:

```text
allocated and unplaced -> placed in one graph -> Detached
```

An unplaced instruction has a live instruction kind and a schema-valid payload,
but is not yet a member of any graph. It may be attached at most once. The
reserved physical `Detached` tag remains the permanent state for an instruction
removed from a published graph; detachment is not an allocation-reuse
mechanism.

### Managed Constant Roots

Heap-backed `Value` constants used during compilation are published through a
separate compilation root table. The table contains `Value *` entries pointing
to the stable, mutable `Value` slots embedded in instructions or arena-owned
side data:

```cpp
class CompilationArena
{
public:
    void add_root(Value *slot);

private:
    std::vector<Value *> root_slots_;
};
```

This lets reclamation and a future moving collector find and, when necessary,
rewrite the actual instruction or side-data entries. An instruction does not
contain `Owned<Value>`, and the root table is not a parallel table of owned
values. Immediate values that contain no managed reference need no root-table
entry. Schema-generated `ConstantValue` slot enumeration identifies the
candidate locations whose concrete values determine whether registration is
needed.

The root table is a normally destroyed compilation object and remains
registered with the runtime root mechanism for the compilation lifetime. Its
slots remain valid until that registration ends. Root-table teardown occurs
before the destructor-free instruction storage is released.

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
using ProgramValueRef = ResultRef<SlotClass::ProgramValue, Instruction *>;
using SnapshotRef = ResultRef<SlotClass::Snapshot, Instruction *>;
```

Constructing a result reference validates that the producer's intrinsic output
class matches the slot class required by the wrapper. A `SlotClass::None`
instruction cannot be referenced as an input, a Snapshot cannot be used as a
program value, and a program value cannot be used where recovery state is
required. The same `SlotClass` vocabulary describes non-instruction inputs such
as block edges and stable runtime metadata without translating between separate
input and output enums. Generated construction signatures make mismatched
classes unrepresentable to ordinary callers, structural input replacement
checks the declared class, and the verifier independently checks the encoded
payload. Raw `Instruction *` remains the identity
used for instruction-list placement, diagnostics, and other non-result
structural operations. A result wrapper still contains a pointer; it does not
turn the IR back into a container-relative integer-ID representation.

## Typed Read-Only Views

`src/jit/instruction.def` is the authoritative schema for the closed set of
live instruction kinds. Each definition names the instruction kind and typed
view, the IR level or levels in which it is legal, its intrinsic output
`SlotClass`, its unavoidable kind effects, its payload shape, and every fixed
or variable input slot with its `SlotClass`. Repeated inclusion of that schema
generates or validates the `InstructionKind` enum, invariant kind metadata,
typed accessors, generic input traversal, output/input class legality, and the
size and alignment constraints for encoded payloads. The `Detached` storage tag
is not listed as a semantic instruction definition.

The schema owns facts that must remain synchronized for every instruction
kind. It may generate storage decoding and straightforward typed-accessor
boilerplate, while instruction-specific convenience accessors may remain
handwritten. It does not generate pass implementations or visitor-method
dispatch; passes remain ordinary C++ so they can organize related cases
locally.

Conceptually, representative definitions describe:

```text
Add
    output: ProgramValue
    lhs: ProgramValue
    rhs: ProgramValue

ShapeGuard
    output: ProgramValue
    value: ProgramValue
    expected_shape: Shape
    validity: ValidityCell
    snapshot: Snapshot

ShapeKeyGuard
    output: ProgramValue
    value: ProgramValue
    expected_key: ShapeKey
    snapshot: Snapshot

Snapshot
    output: Snapshot
    captured_values[]: ProgramValue

Constant
    output: ProgramValue
    value: ConstantValue

ConditionalBranch
    output: None
    condition: ProgramValue
    true_edge: BlockEdge
    false_edge: BlockEdge
```

No class tag is stored beside each payload word. Generic code reads the
instruction kind once and selects schema-generated layout metadata or a
schema-generated per-kind enumerator. Conceptually:

```cpp
struct InputSlotDescriptor
{
    SlotClass slot_class;
    SlotLayout layout;  // Fixed or variable-length.
    uint16_t offset;
};

void visit_inputs(Instruction &instruction, InputVisitor visitor);
```

The generated dispatch interprets each payload word only according to the
schema for that instruction kind. `ProgramValue` slots are ordinary uses at the
containing instruction, `BlockEdge` slots participate in CFG maintenance, and
metadata and constant classes use their class-specific lifetime and validation
rules.

`Snapshot` is the one explicit exception to ordinary local-use behavior. It is
a zero-code aggregate result whose captured `ProgramValue` inputs become point
uses at every guard or side exit that consumes the `SnapshotRef`. Liveness
expands a Snapshot input transitively at that consuming position, so several
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

Each concrete instruction form is a small read-only view holding a
`const Instruction *`. It is not derived from `Instruction`, is not separately
allocated, and does not own storage:

```cpp
class AddInstruction
{
public:
    static constexpr InstructionKind Kind = InstructionKind::Add;

    ProgramValueRef lhs() const;
    ProgramValueRef rhs() const;

private:
    friend class Instruction;

    explicit AddInstruction(const Instruction *instruction)
        : instruction_(instruction)
    {
    }

    const Instruction *instruction_;
};
```

The view exposes only immutable fields meaningful for its instruction kind.
Inferred types, analyzed effects, locations, and other phase knowledge are read
through the concrete metadata object that owns them, not through a typed
instruction view. Copying a view copies a non-owning read capability; it never
grants mutation authority. Views are intended to be short-lived pass locals
rather than objects stored in the IR.

There is one ordinary `InstructionKind` enum generated from
`src/jit/instruction.def`. Each typed view declares its own
`static constexpr Kind`, and schema-generated validation requires it to match
the view mapping in the definition. Checked conversion uses the type itself as
the source of the expected kind:

```cpp
template<typename TypedInstruction>
TypedInstruction Instruction::as() const
{
    assert(kind() == TypedInstruction::Kind);
    return TypedInstruction(this);
}
```

`is<T>()` and `try_as<T>()`, if useful, follow the same mapping. They use an
enum comparison followed by ordinary construction of the view. The design does
not use `dynamic_cast`, `typeid`, or virtual instruction methods.

Category views may later represent a deliberately defined set of kinds with a
common payload shape. Such a view has no single `Kind`, so it is obtained
through a separate membership-checked API such as `as_category<T>()`; it cannot
be used with `CL_JIT_INSTRUCTION_CASE`. Category views are deferred until an
actual grouped operation family justifies them.

## Mostly Immutable Instructions

An instruction's kind, output class, constants, bytecode origin, payload shape,
and non-input semantic parameters are immutable after construction. A pass that
changes one of these properties constructs an unplaced replacement through the
appropriate instruction factory, then asks the structural IR editor to attach
it and rewrite the published graph explicitly.

Detachment is the sole exception to the stored kind remaining live for the
allocation lifetime. It is a one-way lifetime transition, not semantic
mutation:

```text
Live(fixed InstructionKind) -> Detached
```

The CFG editor performs this transition only after it has rewritten or removed
every use, removed the instruction's own operand occurrences from use indexes,
and unlinked the instruction from its block. It then neutralizes managed
constant slots through the compilation-root mechanism and leaves any still
registered root slot holding a safe immediate value. It clears or debug-poisons
the remaining payload storage, notifies active metadata owners, and publishes
the `Detached` tag last. An editor transaction is not observable by ordinary
passes in an intermediate state. A detached allocation is never republished or
returned to a live kind.

The serial is deliberately preserved for diagnostics. Verification rejects a
detached instruction in a block list, any operand or Snapshot reference whose
producer is detached, and any remaining use-index entry involving a detached
instruction. It reports the preserved serial rather than interpreting the
poisoned payload. Generic traversal likewise checks `is_detached()` before
dispatching on a kind and never visits detached side data. Short-lived typed
views must not be retained across structural edits.

Input slots are controlled mutable structure. The structural editor may replace
a slot only with an entity of the same declared `SlotClass`, while maintaining
the corresponding SSA use index, Snapshot dependency, CFG index, metadata
lifetime, analysis invalidation, and graph mutation generation. Changing a
slot's class, count, or layout requires instruction replacement. Typed views
expose inputs read-only and never provide setters or writable arrays.

Frequent `replace_all_uses_with` operations use indexed `InputSlotHandle`s that
identify the affected mutable slots directly rather than scanning the graph.
The schema-generated generic input walker constructs and verifies those
indexes, rebuilds them when required, and supports cloning, printing, and other
whole-instruction operations. It walks fixed and variable inputs in declared
order and never compares instruction pointers against words declared as
`BlockEdge`, metadata, or inline constants. Kind-specific named accessors such
as branch edges remain available through typed views.

## Phase-Owned Attached Metadata

Inferred types, analyzed effects, dependencies, representations, locations, and
similar derived facts are not part of the physical instruction representation.
They live in explicit metadata objects owned by the phase and IR level that
defines them, for example:

```text
SemanticValueAnalysis   Semantic ProgramValueRef -> ValueFacts
CoreEffectAnalysis      Core Instruction*        -> analyzed EffectFlags
LocationAssignments     Core Instruction*        -> backend locations
```

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
makes an old frozen view stale, but it does not require discarding all stored
facts. The editor supplies a mutation description so the owning analysis can
preserve unaffected entries, cheaply derive facts for locally transparent
instructions, and incrementally recompute only affected dependents before
publishing the next generation.

Attachments exist only while a later phase consumes them. Semantic value facts
may be discarded after Semantic-to-Core lowering; Core effect information may
be discarded after effect-dependent optimization; backend location data has
its own later lifetime. Detaching an instruction invalidates or removes its
entries from every active attachment before the editor publishes the detached
storage tag. Major representation boundaries may build a fresh graph while
using the same instruction, CFG, serial, and arena machinery.

## Kind Effects and Analyzed Effects

Instruction-kind effects and analyzed effects have different meanings.

Kind effects are unavoidable semantic properties of the operation. They are
declared for each `InstructionKind` in `src/jit/instruction.def`, exposed
through schema-generated invariant metadata, and cannot be removed by analysis.
Examples include a return terminating its block or an operation performing an
inherently visible write.

Analyzed effects are conservative phase-owned metadata for a particular live
instruction. `CoreEffectAnalysis`, or the corresponding concrete analysis for
another allowed IR level, initializes them conservatively and may refine them
as operand facts, resolved targets, and other evidence become known.

The effective summary is:

```cpp
EffectFlags CoreEffectAnalysis::effects(const Instruction *instruction) const
{
    validate_current_entry(instruction);
    return instruction_kind_effects(instruction->kind()) |
           analyzed_effects(instruction);
}
```

Consequently, an effect that analysis may prove absent must not be classified
as an unavoidable kind effect. A generic call, for example, may begin with
`MayRaise` in its analyzed effects; target analysis can remove it when
justified. An effect inherent in every execution of a kind remains in the kind
table.

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
        emit_return(return_instruction.value());
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
control transfer. Falling through into a case for a different typed view would
attempt to interpret the original instruction as the wrong kind. Shared bodies
must use an explicitly checked category representation rather than typed-case
fallthrough.

A deliberately partial classifier should normally use `is<T>()` or ordinary
conditionals. Any local suppression of exhaustive-switch checking must be
explicit and exceptional.

## Rejected Directions

A virtual instruction hierarchy would couple storage ownership to polymorphic
deletion and encourage semantic behavior to spread across virtual methods. It
would also retain the `std::deque<std::unique_ptr<Instruction>>` allocation
shape that this representation makes unnecessary.

C++ RTTI and `dynamic_cast` add no useful checking beyond the explicit closed
`InstructionKind` enum. A checked kind comparison followed by typed view
construction is simpler and makes exhaustive matching possible.

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
