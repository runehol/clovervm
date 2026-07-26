# Indexed JIT Instruction Storage Refactoring

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Accepted |
| Implementation | Not started |
| Scope | Replacing pointer-identified 48-byte JIT instructions with compact table-indexed instructions while retaining pointer-based CFG objects |
| Owning layers | `CompilationStorage` owns compiler object lifetime and indexed lookup; the instruction schema owns physical payload layout and typed access; CFG objects own graph topology |
| Validated against | `819673eba7e0ebff3560cc844746ea117e715efc` (2026-07-26) |
| Supersedes | N/A; when implemented, this replaces the storage and identity model in [JIT Instruction Representation](jit-instruction-representation.md) |

The refactoring makes instruction identity relative to one compilation-owned
storage table. It targets the representation that dominates Core IR memory
without forcing the comparatively small, mutation-heavy control-flow graph into
the same access model.

## Ownership and identity

`CompilationArena` becomes `CompilationStorage`. A compilation session owns one
storage object and publicly exposes its address:

```cpp
class CompilationSession
{
public:
    CompilationStorage *storage() { return &storage_; }
    const CompilationStorage *storage() const { return &storage_; }

private:
    std::vector<Owned<Value>> retained_values_;
    CompilationStorage storage_;
};
```

Reading and resolving existing storage entries is public. Allocation remains a
privileged operation:

```cpp
class CompilationStorage
{
public:
    Instruction instruction(InstructionId);
    BlockEdgeId id_for(const BlockEdge *) const;
    BlockEdge *block_edge(BlockEdgeId) const;

private:
    friend class CompilationSession;
    friend class GraphBuilder;
    friend class GraphRewriter;
    friend class RewriteContext;

    CompilationStorage() = default;

    template<typename ConcreteInstruction, typename... Args>
    ConcreteInstruction make_instruction(Args &&...);

    void detach_instruction(InstructionId);

    Block *make_block(...);
    BlockEdge *make_block_edge(...);
    ControlFlowGraph *make_graph(); // Constructs the graph with `this`.

    std::vector<InstructionEntry> instructions_;
    InstructionSideTable instruction_side_data_;

    ObjectPool<ControlFlowGraph> graphs_;
    ObjectPool<Block> blocks_;
    ObjectPool<BlockEdge> block_edges_;
    std::vector<BlockEdge *> block_edges_by_id_;
};
```

Ordinary compiler passes receive the session, its storage pointer, or a graph
that borrows that pointer. They may follow IDs, inspect instructions, and query
edge mappings. They cannot append raw instructions, blocks, edges, or graphs.
`GraphBuilder`, `GraphRewriter`, and their narrowly scoped construction contexts
are the blessed mutation surfaces and use the private creation API. This keeps
schema validation, placement, reconstruction, and CFG bookkeeping attached to
the layers that own them.

Instructions are append-only entries in `instructions_`. `InstructionId` is a
typed 32-bit vector index and is the instruction's only persistent identity.
The stored instruction entry has no serial. IDs are never reused during a
compilation; removing an instruction from a graph may poison its table entry but
does not move or erase it.

Detachment directly poisons the corresponding `InstructionEntry`; it does not
add a detached-ID table or another check beside normal entry resolution.
`CompilationStorage::detach_instruction(InstructionId)` is private and available
to the storage's privileged implementation classes; `GraphRewriter` is its sole
intended caller. It resolves the entry and invokes its private
`detach_and_poison()` operation. Every later logical access therefore observes
the poisoned entry through the ordinary instruction-access path.

`ProgramValueRef`, `SnapshotRef`, represented value references, block parameter
lists, block instruction lists, block-edge arguments, use lists, and
instruction-indexed analysis maps store `InstructionId` rather than
`Instruction *`:

```cpp
class ProgramValueRef
{
public:
    InstructionId instruction_id() const { return instruction_; }

private:
    InstructionId instruction_;
};

static_assert(sizeof(ProgramValueRef) == sizeof(uint32_t));
```

An ID is meaningful only within its owning `CompilationStorage`. Persistent
references remain four-byte IDs; they do not carry a storage pointer.
Compiler APIs that dereference them must have the compilation storage available.

Blocks and control-flow graphs remain stable-address, pointer-based objects.
They are few, contain mutation-heavy vectors and query state, and are commonly
retained while graph algorithms run. Each graph borrows a pointer to the storage
that owns it:

```cpp
class ControlFlowGraph
{
public:
    CompilationStorage *storage() { return storage_; }
    const CompilationStorage *storage() const { return storage_; }

private:
    friend class CompilationStorage;

    explicit ControlFlowGraph(CompilationStorage *storage)
        : storage_(storage)
    {
    }

    CompilationStorage *storage_;
};
```

The graph cannot outlive its owning compilation session. Blocks reach storage
through their existing graph pointer. This allows graph-only APIs such as CFG
verification, query preparation, IR printing, instruction traversal,
register-allocation preparation, and backend emission to retain their natural
signatures rather than all acquiring a separate storage argument.

`BlockEdge` also remains pointer-based in the CFG API, but each edge receives a
compact `BlockEdgeId`. Branch instruction payloads store that ID, and typed
access resolves it through `CompilationStorage`. The edge ID replaces the edge
serial as its deterministic storage identity.

Runtime-owned objects are outside this indexed identity scheme. Attributes such
as `Shape *` and `ValidityCell *` remain pointer-shaped values.

## Instruction entries and logical access

The stored `InstructionEntry` is a 16-byte type-erased table entry:

```cpp
class alignas(8) InstructionEntry
{
private:
    friend class CompilationStorage;

    uint32_t slots_[3];
    uint16_t kind_;
    uint16_t operand_storage_;
};

static_assert(sizeof(InstructionEntry) == 16);
```

The names distinguish three roles:

- `InstructionSlot` is the temporary raw deque element used only while current
  concrete instruction objects still require stable addresses.
- `InstructionEntry` is the final physical entry in the indexed instruction
  table.
- `Instruction` is the generic logical handle used by compiler code.

The slots precede the header so that slot zero is eight-byte aligned. The high
bit of `operand_storage_` records indirect operand storage; the remaining bits
hold the logical operand count.

`Instruction` is the generic logical handle containing
`CompilationStorage *` and `InstructionId`. Concrete instruction classes
inherit from it:

```cpp
class Instruction
{
public:
    InstructionId id() const;
    InstructionKind kind() const;

protected:
    const InstructionEntry &entry() const;
    std::span<const uint32_t> payload() const;

    CompilationStorage *storage_;
    InstructionId id_;
};

class ShapeGuardInstruction : public Instruction
{
public:
    TaggedValueRef object() const;
    SnapshotRef snapshot() const;
    Shape *expected_shape() const;
};

class ValidityCellGuardInstruction : public Instruction
{
public:
    TaggedValueRef value() const;
    SnapshotRef snapshot() const;
    ValidityCell *validity() const;
};
```

Logical instructions retain an ID rather than a pointer or reference to a vector
entry. Appending an instruction may reallocate `instructions_` without
invalidating an existing `Instruction` or concrete subclass. Schema generation
continues to provide concrete constructors, typed accessors, metadata, operand
traversal, and reconstruction.

## Reference typing and storage access

`InstructionKind` continues to encode result class and, for program values,
`ValueRepresentation`. Recovering those facts from an arbitrary
`ProgramValueRef` requires resolving its ID:

```cpp
Instruction definition =
    graph.storage()->instruction(value.instruction_id());

ValueRepresentation representation =
    definition.value_representation();
```

Concrete instructions already carry storage, so their own kind, result,
representation, payload, and referenced-edge accessors need no additional
argument. Schema-generated operand accessors also do not need to inspect the
referenced definition: the schema proves the result class and representation of
the returned reference type. They construct `TaggedValueRef`, `F64Ref`, or
`SnapshotRef` directly from the encoded ID through a private schema-trusted
path.

Converting an arbitrary generic reference to a represented reference remains a
checked operation and requires storage:

```cpp
TaggedValueRef tagged =
    TaggedValueRef::from(*graph.storage(), generic_value);
```

The checked operation resolves the instruction, decodes the representation from
its kind, and rejects an incompatible conversion. Typed-to-typed rewriting and
schema decoding preserve the existing reference type without repeating this
lookup.

At the start of the refactoring, JIT production code contains 53
`reference.instruction()` uses across 15 files. Most use the pointer only for
identity, equality, hashing, rewrite remapping, liveness ownership, or location
lookup; those become direct `InstructionId` operations and need no storage.
Definition type recovery is concentrated in:

- the reference constructor assertions;
- CFG operand and block-argument verification;
- graph-rewriter validation after operand remapping;
- `Uses::result_class()` and `Uses::value_representation()`;
- the bytecode translator's generic-to-tagged conversion;
- allocation materialization's tagged-versus-F64 transfer selection; and
- the live-range-origin program-value assertion.

These sites resolve through the graph, an existing logical instruction, or the
session already owned by the pass. Use lists and other graph-owned query caches
retain IDs and use their graph's borrowed storage pointer when they need type
facts. Type information is not duplicated in `ProgramValueRef`.

## Payload encoding

The schema computes 32-bit physical words for operands and attributes:

- an instruction operand is one `InstructionId` word;
- a block-edge attribute is one `BlockEdgeId` word;
- `BytecodePC` and other 32-bit attributes use one word;
- `Value`, `ShapeKey`, runtime pointers, and other 64-bit attributes use two
  consecutive words;
- padding words are inserted so every 64-bit attribute begins at an even inline
  word index.

Attributes are always inline. For an instruction with inline operands, the
operands begin at slot zero and the attributes follow them, including any
padding needed to align a wide attribute. The schema selects this layout only
when the complete sequence fits in three words.

When operands are indirect, attributes are instead laid out exactly as if the
instruction had zero operands: they begin at slot zero with their ordinary
alignment. Slot two contains the 32-bit operand-table offset. Consequently, the
schema rejects an instruction kind whose attributes alone require more than two
inline words.

A fixed-arity kind uses indirect operands when its aligned inline layout does
not fit. A variadic kind always uses indirect operands, even when a particular
instance has few enough operands to fit inline. Storage mode is therefore
determined by the instruction kind, never by a runtime operand count.

The schema generates the storage mode as part of the concrete instruction type.
A typed accessor therefore never switches between inline and indirect operands
based on runtime operand count. The operand-storage word still carries the
logical count needed to delimit a variadic sequence and lets generic traversal
select the encoded operand source.

Every indirect sequence contains only 32-bit instruction IDs. The side table
therefore needs no wide-value alignment or attribute decoding.

Representative layouts are:

```text
AddSMI, inline:
    inline 0    lhs InstructionId
    inline 1    rhs InstructionId
    inline 2    snapshot InstructionId

Const, inline:
    inline 0-1  Value

ConditionalBranch, inline:
    inline 0    condition InstructionId
    inline 1    true BlockEdgeId
    inline 2    false BlockEdgeId

ShapeGuard:
    inline 0-1  Shape *
    inline 2    side-table offset

    indirect 0 object InstructionId
    indirect 1 snapshot InstructionId

ValidityCellGuard:
    inline 0-1  ValidityCell *
    inline 2    side-table offset

    indirect 0 value InstructionId
    indirect 1 snapshot InstructionId

PythonCall:
    inline 0    interpreter_return_pc
    inline 2    side-table offset

    indirect 0 callable InstructionId
    indirect 1 snapshot InstructionId
    indirect 2... argument InstructionIds
```

The attribute catalogue at the start of this refactoring is:

| Attribute class | Stored width | Users |
|---|---:|---|
| `Shape` | two words | `ShapeGuard.expected_shape` |
| `ValidityCell` | two words | `ValidityCellGuard.validity` |
| `ShapeKey` | two words | `ShapeKeyGuard.expected_shape_key` |
| `ValueConstant` | two words | `Const.constant` |
| `BytecodePC` | one word | `PythonCall.interpreter_return_pc`, `Snapshot.resume_pc` |
| `BlockEdge` | one word | conditional and unconditional branches |

## Refactoring slices

### 1. Rename the owner and install storage pointers

Rename `CompilationArena` to `CompilationStorage` without changing allocation,
instruction identity, or payload representation. Change the public session
accessor to `storage()`, construct each CFG with a borrowed pointer to its owning
storage, and update builder, rewriter, and construction contexts to use the new
name and access boundary.

At the end of this slice, instructions remain slab-allocated 48-byte concrete
objects, references remain pointers, and IR output is unchanged. The slice
establishes only ownership vocabulary and storage availability.

### 2. Establish stable indexed identity behind the pointer API

Replace the instruction slab implementation with an append-only deque of raw
instruction slots:

```cpp
struct InstructionSlot
{
    alignas(Instruction) std::byte storage[sizeof(Instruction)];

    Instruction *instruction();
    const Instruction *instruction() const;
};

std::deque<InstructionSlot> instructions_;
```

The raw slot is transitional storage for the current fieldless concrete
subclasses. `CompilationStorage::make_instruction<T>()` appends one slot,
placement-constructs `T`, and assigns an `InstructionId` equal to the deque
index. Deque growth preserves the existing instruction pointers, while
`CompilationStorage::instruction(InstructionId)` provides constant-time indexed
lookup.

Rename the stored instruction serial to `InstructionId`. During this transition
the ID field supports pointer-to-ID conversion for old callers; it ceases to be
stored once the logical `Instruction` carries the ID. Do not change
`ProgramValueRef`, CFG instruction lists, concrete instruction access, slot
width, side data, or textual identity in this slice.

Focused verification must cover ID-to-pointer and pointer-to-ID round trips,
monotonic IDs, pointer stability across multiple deque-block allocations,
detachment, and unchanged deterministic printing.

### 3. Convert instruction references and operand payloads to IDs

Keep the existing stored concrete instruction objects and pointer-based CFG
instruction collections. Convert `ProgramValueRef`, `SnapshotRef`, and
represented value references to four-byte `InstructionId` wrappers. Change
instruction operand payload words from pointers to zero-extended instruction
IDs while retaining the existing pointer-sized physical slots and operand side
storage.

Schema-generated typed operand accessors construct the declared reference type
directly from the stored ID. Generic-to-represented conversions, CFG
verification, rewriting validation, and other operations that need facts about
the definition resolve the ID through the graph or session's storage. Existing
instruction pointers provide their stored ID when a builder constructs an
operand reference.

Do not convert block parameter lists, block instruction lists, rewriter state,
instruction-indexed analysis maps, allocator anchors, or emitter inputs in this
slice. They remain pointer-based and continue to refer to the stable concrete
objects in the deque.

Focused verification must establish that references are four bytes,
ID-encoded operands round-trip through every fixed and variadic accessor,
incompatible generic-to-represented conversions are still rejected, CFG
verification and rewrite reconstruction preserve type checks, and all existing
IR dumps remain unchanged.

### 4. Convert instruction access and CFG state to logical handles

Make `Instruction` a table-aware logical handle containing
`CompilationStorage *` and `InstructionId`, and make each concrete instruction
class inherit from it. Once no concrete subclass occupies the backing storage,
replace the transitional raw slots with `std::deque<InstructionEntry>`.
`InstructionEntry` remains a 48-byte table entry with five pointer-sized payload
slots; this slice does not compress or reorganize payloads.

Remove the stored instruction ID field because the logical `Instruction`
already carries the deque index. Convert block parameter and instruction lists,
block-edge arguments, use lists, rewriter state, allocator anchors, location
maps, emitter inputs, and instruction-indexed analysis maps from pointers to
IDs. Operand words remain pointer-sized and retain the zero-extended instruction
IDs introduced by the previous slice.

Route arbitrary result-class and representation queries through the owning
graph's storage. Schema-generated typed operand accessors preserve their
declared reference type without re-reading the defining instruction.

`GraphRewriter` detaches a removed instruction by calling the private
`CompilationStorage::detach_instruction(InstructionId)`. That operation directly
poisons the entry in the deque. It does not create auxiliary detached state or
alter ID resolution.

This remains the broadest slice, but it changes only instruction access and
persistent CFG identity. Focused verification must establish that generic and
concrete logical handles resolve the expected entry, typed downcasts preserve
their storage and ID, detached IDs fail on access, rewriting and analyses use
the new ID namespace consistently, and all existing IR dumps remain unchanged.

### 5. Replace the transitional deque with a vector

Make `InstructionEntry` an ordinary trivially movable entry and replace
`std::deque<InstructionEntry>` with `std::vector<InstructionEntry>`. No external
API, identity, or payload encoding changes: logical instructions and persistent
references already resolve by ID, so vector relocation is unobservable.

Focused verification must retain a live logical instruction and references
across forced vector reallocations, then successfully access their kind,
operands, and attributes.

### 6. Compact block-edge attributes

Introduce `BlockEdgeId` lookup while preserving pointer-based CFG APIs. Convert
branch instruction attributes from `BlockEdge *` to IDs and resolve typed edge
accessors through `CompilationStorage`. The ID replaces the edge serial as its
deterministic identity.

Keep instruction slots pointer-sized in this slice so edge identity conversion
is tested independently from physical packing.

### 7. Shrink the physical instruction representation

Change instruction and indirect-operand words to 32 bits, reorder the stored
instruction so its slots precede its kind/count header, and reduce the common
record from five slots to three.

Generate attribute widths and alignment padding from `instruction.def`. Keep all
attributes inline, make fixed operands indirect when the aligned
operands-then-attributes layout does not fit, keep variadic operands
unconditionally indirect, and reserve slot two for every indirect operand-table
offset.

Focused layout verification must round-trip every attribute class, verify
aligned 64-bit attributes, cover inline `AddSMI`, `Const`, and
`ConditionalBranch`, cover indirect operands for all guards, cover variadic
`PythonCall` and `Snapshot`, preserve generic operand traversal and
reconstruction, and enforce `sizeof(InstructionEntry) == 16`.

### 8. Remove transitional machinery and update measurements

Remove the old instruction pool, pointer encoders, serial vocabulary, and any
migration-only helpers. Update [JIT Instruction
Representation](jit-instruction-representation.md) to describe the implemented
model and measure instruction-table and operand-table storage on representative
compilations.

Every slice must pass `ninja -C build-debug all check` and leave the repository
in a coherent state. A slice must not quietly combine identity migration with
slot compression; failures should remain attributable to one representation
boundary.
