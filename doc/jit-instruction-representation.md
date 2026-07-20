# JIT Instruction Representation

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Not started; the current virtual instruction hierarchy is temporary CFG scaffolding |
| Scope | Physical instruction storage, typed instruction access, analysis mutation, effects, matching, and arena lifetime for Core and Semantic IR |
| Owning layers | The JIT instruction representation owns storage and typed access; instruction kinds own semantic payloads and unavoidable effects; analysis passes own inferred types and analyzed effects; the CFG editor owns structural replacement |
| Validated against | N/A |
| Supersedes | The open instruction-representation alternatives in [JIT Control-Flow Graph](jit-control-flow-graph.md) and the integer-only instruction reference direction in [JIT Compiler and IR](jit-compiler-and-ir.md) |

Core IR and the optional Semantic IR use a fixed-size, type-erased
`Instruction` allocated with compilation lifetime. Instruction operands and
other semantic references are pointers, while each allocation also carries a
typed serial for deterministic identity, diagnostics, and ordering. Read-only
typed instruction views provide kind-specific access without an instruction
class hierarchy, virtual dispatch, or C++ RTTI.

The representation has four deliberately different roles:

```text
Instruction
    fixed-size stored object
    stable address and serial
    instruction kind
    current type and analyzed effects
    encoded kind-specific payload

AddInstruction, CallInstruction, ...
    short-lived read-only typed views over Instruction

InstructionAnalysisEditor
    narrow capability for updating type and analyzed effects

CFG editor
    structural insertion, removal, and instruction replacement
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
    InstructionKind kind() const;
    Type *type() const;
    EffectFlags analyzed_effects() const;
    EffectFlags effects() const;

    template<typename TypedInstruction>
    TypedInstruction as() const;

private:
    friend class InstructionAnalysisEditor;

    Serial serial_;
    InstructionKind kind_;
    Type *type_;
    EffectFlags analyzed_effects_;
    InstructionPayload payload_;
};
```

The record may contain raw pointers, serials, enums, flags, scalar immediates,
and other trivially destructible values. It must not directly contain an
owning `std::vector`, `std::string`, `std::unique_ptr`, reference-counted
handle, or any value whose destructor releases resources.

Variable-length operands and unusually large instruction data use pointer/count
references to compilation-arena allocations:

```cpp
struct InstructionOperands
{
    Instruction *const *data;
    uint32_t size;
};
```

Construction may use a mutable buffer before publishing the instruction, but
stored and typed access exposes immutable operand slots, for example as
`absl::Span<Instruction *const>`. The pointed-to instructions remain ordinary
IR references; clients cannot replace an operand by assigning through the
span.

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
entry.

The root table is a normally destroyed compilation object and remains
registered with the runtime root mechanism for the compilation lifetime. Its
slots remain valid until that registration ends. Root-table teardown occurs
before the destructor-free instruction storage is released.

## Pointers, Serials, and Determinism

Semantic relationships use pointers:

```cpp
Instruction *lhs;
Instruction *rhs;
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

Typed result-reference wrappers may be added where they enforce a real
semantic distinction, such as program values versus Snapshots. Such a wrapper
holds a pointer and validates the producer's result class; it does not turn the
IR back into a container-relative integer-ID representation.

## Typed Read-Only Views

Each concrete instruction form is a small read-only view holding a
`const Instruction *`. It is not derived from `Instruction`, is not separately
allocated, and does not own storage:

```cpp
class AddInstruction
{
public:
    static constexpr InstructionKind Kind = InstructionKind::Add;

    Instruction *lhs() const;
    Instruction *rhs() const;

    Type *type() const { return instruction_->type(); }
    EffectFlags effects() const { return instruction_->effects(); }

private:
    friend class Instruction;

    explicit AddInstruction(const Instruction *instruction)
        : instruction_(instruction)
    {
    }

    const Instruction *instruction_;
};
```

The view exposes only fields meaningful for its instruction kind. It may read
the instruction's current type and effects, but it exposes no setters. Copying
a view copies a non-owning read capability; it never grants mutation authority.
Views are intended to be short-lived pass locals rather than objects stored in
the IR.

There is one ordinary `InstructionKind` enum. Each typed view declares its own
`static constexpr Kind`; there is no `.def` file, instruction registry, or
generated kind-to-type table. Checked conversion uses the type itself as the
source of the expected kind:

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

An instruction's kind, constants, bytecode origin, Snapshot references, and
other opcode-specific semantic parameters are immutable after construction. A
pass that changes one of these properties constructs a replacement instruction
through the structural IR editor and rewrites the graph explicitly.

Operand slots are controlled mutable structure. The structural editor may
replace operands while maintaining use indexes, invalidating affected analysis,
and advancing the graph mutation generation. Typed instruction views expose
operands read-only and never provide setters or writable operand arrays. This
controls mutation without forcing replacement of every transitive user during
ordinary SSA `replace_all_uses_with` operations.

Every instruction kind provides a generic ordered operand/reference interface
in addition to its semantic typed accessors. The structural editor and verifier
use this interface to find ordinary operands, variable operand arrays,
Snapshots, and other references that participate in rewriting or use indexes.
Kind-specific named references such as branch edges remain available through
typed views.

Two analysis-owned fields may change in place:

- the instruction's current inferred type;
- its analyzed effect flags.

Typed instruction views can read both fields but cannot update them. Mutation
requires an `InstructionAnalysisEditor`, which acts as a capability supplied
only to the small set of passes authorized to refine analysis state:

```cpp
class InstructionAnalysisEditor
{
public:
    void set_type(Instruction *instruction, Type *type);
    void set_analyzed_effects(
        Instruction *instruction, EffectFlags effects);

private:
    friend class PassManager;

    explicit InstructionAnalysisEditor(AnalysisMutationPhase &phase);

    AnalysisMutationPhase *phase_;
};
```

The exact component that constructs the editor will follow the pass-pipeline
API when that API exists. Its constructor must not be public. The editor is the
single enforcement point for valid type/effect combinations, analysis
invalidation, and any debug verification required around mutation. This design
does not add an owning-graph pointer or live-placement state to every
instruction. Graph membership and structural validity remain responsibilities
of the construction pipeline, structural editor, and verifier.

Ordinary passes receive `const Instruction *` or mutable graph access without
an analysis editor. Possessing a typed view never implies permission to mutate
the referenced instruction.

### Analysis Mutation Phases

Type and effect mutation is phase-scoped:

1. Instruction construction initializes types and analyzed effects
   conservatively.
2. A structurally fixed graph enters an analysis mutation phase. Only passes
   holding its `InstructionAnalysisEditor` may update those fields.
3. Fixed-point analysis may narrow or widen its provisional state until it
   converges. Transformations must not rely on provisional facts.
4. Closing the phase validates and freezes the resulting analysis state.
   Effect- or type-dependent transformations consume only frozen state.
5. A later structural edit invalidates the affected frozen state and restores
   the affected fields to conservative values before another dependent
   transformation. A new analysis mutation phase recomputes and freezes them.

The phase may batch updates and advance the applicable analysis generation once
at commit rather than once per fixed-point cell update. Intermediate
type/effect combinations need be valid only according to the mutation-phase
contract; pass-boundary verification observes committed state.

## Kind Effects and Analyzed Effects

Instruction-kind effects and analyzed effects have different meanings.

Kind effects are unavoidable semantic properties of the operation. They come
from `InstructionKind` and cannot be removed by analysis. Examples include a
return terminating its block or an operation performing an inherently visible
write.

Analyzed effects are the current conservative effect state for this particular
instruction. Construction initializes them conservatively. Authorized analysis
may refine them as operand types, resolved targets, and other facts become
known.

The effective summary is:

```cpp
EffectFlags Instruction::effects() const
{
    return instruction_kind_effects(kind_) | analyzed_effects_;
}
```

Consequently, an effect that analysis may prove absent must not be classified
as an unavoidable kind effect. A generic call, for example, may begin with
`MayRaise` in its analyzed effects; target analysis can remove it when justified.
An effect inherent in every execution of a kind remains in the kind table.

Selecting a genuinely different semantic operation still requires instruction
replacement. The editor refines facts about the existing operation; it does
not change its kind or semantic payload.

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
exhaustive.

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
local.

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
