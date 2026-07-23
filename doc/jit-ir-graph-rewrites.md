# JIT IR Graph Rewrites

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | Not started |
| Scope | Read-only instruction traversal, on-demand use records, and forward instruction rewriting within published JIT IR basic blocks |
| Owning layers | The traversal contract owns observable walk order; the read-only walker owns inspection; the use-index builder owns use records; the graph rewriter owns operand substitution, instruction placement, and commit; the instruction schema owns reconstruction; individual passes own matching and semantic legality; CFG editing owns successor and predecessor changes |
| Validated against | N/A |
| Supersedes | If accepted, the incremental mutable-operand rewrite direction in [JIT Instruction Representation](jit-instruction-representation.md) and [JIT Compiler and IR](jit-compiler-and-ir.md) |

JIT IR instructions are immutable. A graph rewrite constructs a replacement
instruction stream rather than editing instruction payloads or rewriting use
slots in place. The callback sees each instruction from the original published
graph. After the callback returns, the rewriter resolves the proposed output
through replacements for definitions already visited and appends the canonical
form to a staged instruction stream.

This design covers local instruction rewrites, lowering one instruction to an
instruction sequence, erasure, and passes such as dead-code elimination. It
does not cover changes to the CFG topology.

Read-only traversal, use indexing, and structural rewriting are separate
components. They share a traversal contract where appropriate, but the graph
rewriter does not own, build, retain, or implicitly update a use index.

## Shared Traversal Contract

Read-only walks and graph rewrites accept the same traversal value:

```cpp
enum class BlockWalkOrder : uint8_t
{
    ProgramOrder,
};

class InstructionTraversal
{
public:
    constexpr InstructionTraversal() = default;

    [[nodiscard]] constexpr InstructionTraversal
    with_block_order(BlockWalkOrder order) const;

    constexpr BlockWalkOrder block_order() const;

private:
    BlockWalkOrder block_order_ = BlockWalkOrder::ProgramOrder;
};
```

`with_block_order()` returns an altered copy and leaves the original traversal
unchanged. This allows a pass to derive a local traversal policy from a shared
default without mutable configuration:

```cpp
InstructionTraversal traversal =
    InstructionTraversal()
        .with_block_order(BlockWalkOrder::ProgramOrder);
```

Only program block order exists initially. Dominator order is added with the
dominator-tree analysis it requires; the enum does not advertise an order whose
required input is undefined. Body instructions within each block are visited
forward, including the terminator. Block parameters are not part of this
traversal. Code that specifically needs them reads `Block::parameters()`
directly; a common parameter-traversal option is added only when a real consumer
justifies it.

The read-only API is:

```cpp
walk_instructions(
    graph, traversal,
    [&](const Block &block, const Instruction &instruction) {
        // Read-only inspection.
    });
```

The callback receives no durable placement record. The block supplies all
necessary traversal context. Early-exit control and reverse instruction order
are deferred until a real analysis requires them.

## Independent Use Index

Use records are an explicitly requested, on-demand analysis:

```cpp
struct UseRecord
{
    const Instruction *user;
    uint16_t operand_index;
    OperandClass operand_class;
    ValueRepresentation representation;
};

class UseIndex
{
public:
    std::span<const UseRecord> uses_of(const Instruction &) const;
    size_t use_count(const Instruction &) const;
};

UseIndex build_use_index(const ControlFlowGraph &graph);
```

The builder may be implemented using the read-only walker, but it remains a
separate public analysis and is independent of the graph rewriter. A use record
identifies the immutable user instruction and its semantic operand ordinal, not
the address of a mutable payload slot. Rewriting reconstructs instructions, so
a stored slot address would immediately become stale.

The index records the graph and mutation generation from which it was built.
Every query asserts that the generation is still current. A structural rewrite
invalidates an existing index; callers explicitly build another if they need
post-rewrite uses. The graph does not maintain permanent use lists.

## Core Model

For each original instruction, the rewriter performs:

```text
original instruction
    -> invoke the pass callback on the original instruction
    -> receive zero or more proposed instructions
    -> normalize proposed operands using earlier replacements
    -> append normalized instructions to staged output
    -> remember the normalized result replacing the original definition
```

Given:

```text
a = Add(x, y)
b = Multiply(a, z)
c = Negate(b)
```

if the callback replaces `a` with `a2`, the callback is still subsequently shown
the original `Multiply(a, z)`. If it returns `keep()`, the rewriter then
reconstructs that instruction as `b2 = Multiply(a2, z)` in staged output and
records `b -> b2`. The callback later sees the original `Negate(b)`, whose kept
output is normalized to `Negate(b2)` after the callback returns.

Every callback therefore reasons about one stable published graph generation.
The replacement map is a construction detail of the staged result, not a
partially rewritten input exposed to the pass. A transformation that wants to
optimize the rewritten result runs another pass.

This model relies on the current Core IR rule that an instruction result is
used only later in the same block. Block parameters remain unchanged definitions
available before the instruction stream. Cross-block SSA values and parameter
replacement would require a graph-level renaming design.

## Rewrite API

The graph rewriter accepts the same `InstructionTraversal` and presents the
same block and instruction arguments as the read-only walker:

```cpp
GraphRewriter rewriter(arena, graph);

RewriteSummary summary = rewriter.rewrite_instructions(
    traversal,
    [&](const Block &block,
        const Instruction &instruction) -> RewriteResult {
        // instruction belongs to the original published graph
    });
```

`GraphRewriter::make_instruction<T>(...)` follows the existing
construction vocabulary: it allocates an instruction without placing it.
The callback may construct it using references from the original graph. After
the callback returns, the rewriter resolves those operands through replacements
already established by the walk. The callback captures the rewriter when it
needs this factory, so its traversal arguments remain identical to the
read-only callback.

The callback does not mutate the block, attach instructions, or modify operand
slots. It returns the complete output for the current position.

The read-only walker and graph rewriter conform to the same observable
traversal contract but do not share an engine. The rewriter must walk original
vectors while constructing staged vectors and a replacement map; implementing
it by invoking the read-only walker would obscure those ownership rules.

## Rewrite Results

A rewrite result contains:

```text
instructions    zero or more instructions emitted at this position
replacement     the canonical result replacing the original definition
```

Convenience constructors express the common cases:

```cpp
RewriteResult::keep();
RewriteResult::erase();
RewriteResult::replace(instruction);
RewriteResult::replace(sequence, replacement);
RewriteResult::replace_with_value(existing_definition);
```

Their meanings are:

| Result | Emitted instructions | Remembered replacement |
|---|---|---|
| `keep()` | The normalized instruction | The normalized instruction, when it has a result |
| `erase()` | None | Erased |
| `replace(new)` | `new` | `new` |
| `replace(sequence, result)` | The sequence in order | `result` |
| `replace_with_value(value)` | None | `value` |

A replacement sequence separates execution from value identity. For example,
lowering one operation to an AArch64 immediate materialization followed by an
add emits both instructions but maps later uses of the old result to the add:

```cpp
return RewriteResult::replace({arm_imm12, add}, add);
```

A one-to-one rewrite such as a pointer-valued `Const` becoming `LoadConst` is:

```cpp
return RewriteResult::replace(load_const);
```

The instructions emitted for one callback are not revisited during the same
walk. Fixed-point rewriting, if required, is a separate driver or another pass.

## Result and Sequence Invariants

The rewriter validates each result before commit:

- proposed instructions are unplaced or are the original instruction requested
  by `keep()` at the current position;
- sequence order satisfies definition-before-use;
- every operand refers to a block parameter, an earlier staged result, or an
  earlier instruction in the same sequence;
- a replacement for a `ProgramValue` has the same `ValueRepresentation`;
- a replacement for a `Snapshot` is another `Snapshot` result;
- an instruction with `ResultClass::None` has no replacement result;
- a result-producing instruction has a compatible replacement unless it is
  explicitly erased;
- no non-final sequence instruction is a block terminator.

The pass owns semantic correctness. Structural acceptance of erasing an unused
call, for example, does not prove that discarding its effects is valid.

## Erasure

Erasure is sequence replacement with an empty sequence. The replacement map
retains an explicit erased state rather than silently dropping the original
definition.

Erasing a value or Snapshot producer is valid when it has no later uses. If a
later instruction refers to an erased definition, operand normalization reports
a compiler error identifying the erased producer and its user. Because the
rewrite is staged, this does not expose a partially rewritten block.

The graph-rewrite mechanism does not itself decide whether an instruction is
dead. A dead-code-elimination pass requests a use index, makes its own effect
and liveness decisions, and returns `erase()` for removable instructions. A
simple post-rewrite DCE pass can remove constants made dead by an earlier
folding or lowering pass.

## Instruction Reconstruction

The instruction schema generates a generic reconstruction operation:

```cpp
Instruction *rebuild_with_operands(
    const Instruction &instruction,
    OperandResolver resolver,
    GraphRewriter &rewriter);
```

It reconstructs the same concrete instruction kind with resolved typed operands
and unchanged attributes. It returns the original instruction when no operand
changed. Reconstruction is generated from `src/jit/instruction.def`; it does
not mutate raw slots or introduce handwritten cloning switches.

When reconstruction creates a new instruction, keeping it is still a structural
replacement. The rewriter records the original definition as mapping to the
reconstructed definition so the substitution propagates transitively.

## Traversal Direction

Mutating graph rewrites initially walk forward only. Forward order lets the
rewriter normalize each callback result using decisions already made for every
definition that may legally appear in its operands.

A backward mutating walk cannot provide that guarantee: it visits a use before
the callback has decided how to rewrite its producer. Supporting it would
require stale callback inputs or a separate decision and reconstruction phase.

Read-only reverse instruction traversal and a backward rewrite driver are
deferred until a concrete pass establishes their required semantics.

## Staging and Commit

The rewriter builds one new body-instruction pointer vector per block while all
original block vectors remain unchanged:

```cpp
struct StagedBlockRewrite
{
    Block *block;
    std::vector<Instruction *> instructions;
};
```

Block-parameter vectors are neither copied nor replaced because parameters are
outside the initial traversal and rewrite surface. Peak transient storage is
therefore approximately a second copy of the body instruction pointers, not a
second copy of the arena-owned instructions or payloads.

After every block has been traversed:

1. it validates every completed output vector and replacement map;
2. it swaps every staged body vector into its block;
3. it uses the old vectors left by those swaps to identify and detach original
   instructions no longer present in the graph;
4. it advances the graph mutation generation once and invalidates affected
   attached analyses;
5. debug and test configurations verify the completed graph.

This graph-wide commit keeps the original graph and any generation-checked
`UseIndex` valid throughout all callbacks. No callback observes a graph in
which only earlier blocks have committed.

Arena allocations created during the rewrite need no rollback. Allocation
failure abandons the compilation session under the existing JIT failure model.
Ordinary passes cannot observe the staged block.

The callback-based API is the initial public surface. A cursor may later wrap
the same staging engine if a pass needs manual traversal control, but it must
preserve the same original-input, post-result-normalization, and commit rules.

## Terminators and CFG Changes

Instruction rewriting and CFG editing remain separate responsibilities.

Initially:

- a terminator cannot be erased;
- replacing a non-terminator cannot emit a terminator;
- a sequence replacing a terminator ends in exactly one terminator;
- a replacement terminator preserves the original successor edges.

Changing successors, redirecting edges, splitting blocks, and replacing one
operation with a multi-block region require a CFG editor that updates target
predecessor indexes and invalidates CFG analyses. They are not implicit side
effects of `rewrite_instructions()`.

## Analysis Interaction

The graph rewriter never builds a `UseIndex` implicitly. A pass requests one
before rewriting only when its semantic decisions require use counts or user
enumeration. Because callbacks receive original instructions and commit occurs
only after every callback, the pass may query that index throughout the rewrite
walk. The completed structural rewrite then invalidates the index and other
attachments by default; an analysis may later consume the rewrite summary to
update itself when that is worth the complexity.

The rewrite summary records at least whether instructions changed and whether
the terminator changed. Since this API preserves successor edges, ordinary
instruction rewrites preserve CFG topology even when they invalidate
instruction-indexed analyses.

## Related Documents

- [JIT Instruction Representation](jit-instruction-representation.md)
- [JIT Control-Flow Graph](jit-control-flow-graph.md)
- [JIT Compiler and IR](jit-compiler-and-ir.md)
- [JIT Machine-Code Emission](jit-machine-code-emission.md)
