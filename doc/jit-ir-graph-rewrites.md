# JIT IR Graph Rewrites

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | Read-only traversal, instruction use lists, and body-instruction graph rewriting implemented; block arguments and CFG-topology rewriting not started |
| Scope | Read-only instruction traversal, on-demand use lists, and forward instruction rewriting within published JIT IR basic blocks |
| Owning layers | The CFG owns mutation generation and cached analysis storage; the traversal contract declares observable walk order and required queries; `GraphQueries` owns generation-checked callback access; the use-list builder owns use occurrences; the graph rewriter owns operand substitution, instruction placement, and commit; the instruction schema owns reconstruction; individual passes own matching and semantic legality; CFG editing owns successor and predecessor changes |
| Validated against | `tests/test_jit_graph_rewrites.cpp` |
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

Read-only traversal, use-list construction, and structural rewriting remain separate
algorithms. The CFG owns on-demand cached analysis storage because it also owns
the mutation generation and the instructions indexed by those analyses.
Walkers and rewriters prepare the queries declared by their common traversal
configuration and pass a generation-checked query façade to callbacks.

## Shared Traversal Contract

Read-only walks and graph rewrites accept the same traversal value:

```cpp
enum class BlockWalkOrder : uint8_t
{
    ProgramOrder,
};

enum class GraphQuery : uint8_t
{
    None = 0,
    Uses = 1 << 0,
};

class InstructionTraversal
{
public:
    constexpr InstructionTraversal() = default;

    [[nodiscard]] constexpr InstructionTraversal
    with_block_order(BlockWalkOrder order) const;

    [[nodiscard]] constexpr InstructionTraversal
    with_queries(GraphQuery queries) const;

    constexpr BlockWalkOrder block_order() const;
    constexpr GraphQuery queries() const;

private:
    BlockWalkOrder block_order_ = BlockWalkOrder::ProgramOrder;
    GraphQuery queries_ = GraphQuery::None;
};
```

The `with_*()` methods return altered copies and leave the original traversal
unchanged. This allows a pass to derive a local traversal policy from a shared
default without mutable configuration:

```cpp
InstructionTraversal traversal =
    InstructionTraversal()
        .with_block_order(BlockWalkOrder::ProgramOrder)
        .with_queries(GraphQuery::Uses);
```

Only program block order exists initially. Dominator order is added with the
dominator-tree analysis it requires; the enum does not advertise an order whose
required input is undefined. Body instructions within each block are visited
forward, including the terminator. Block parameters are not part of this
traversal. Code that specifically needs them reads `Block::parameters()`
directly; a common parameter-traversal option is added only when a concrete
requirement justifies it.

The read-only API is:

```cpp
walk_instructions(
    graph, traversal,
    [&](const GraphQueries &queries,
        const Block &block,
        const Instruction &instruction) {
        // Read-only inspection.
    });
```

Before visiting the first instruction, the walker prepares every query declared
by `traversal.queries()` for the graph's current generation. The same
`GraphQueries` value is passed to every callback. The callback receives no
durable placement record; the block supplies local traversal context.
Early-exit control and reverse instruction order are deferred until a real
analysis requires them.

## Generation-Checked Graph Queries

The CFG directly owns optional cached analyses. There is no separate public
cache-manager object:

```cpp
class ControlFlowGraph
{
    uint64_t mutation_generation_;
    mutable std::unique_ptr<UseLists> use_lists_;
};
```

`GraphQueries` is a lightweight per-operation capability, not the cache owner:

```cpp
class GraphQueries
{
public:
    const ControlFlowGraph &graph() const;
    const Uses &uses_of(const Instruction &) const;

private:
    const ControlFlowGraph *graph_;
    GraphQuery requested_;
    const UseLists *use_lists_;
};
```

Preparing queries reuses a current cached analysis, builds a missing analysis,
or replaces one tagged with an older mutation generation. Preparation is the
generation-validation boundary. Accessors assert that their query was requested
but do not repeatedly compare graph generations.

A `GraphQueries` value and every reference obtained through it are valid only
until the graph is structurally mutated. Walkers and rewriters uphold that
contract by preparing queries before traversal and retaining the original graph
generation until all callbacks finish.

The standard walker and graph rewriter prepare this object from
`InstructionTraversal::queries()`. A non-standard graph scan is not forced
through those drivers; it may prepare the same query façade directly:

```cpp
GraphQueries queries = graph.prepare_queries(GraphQuery::Uses);
```

This keeps query dependencies explicit without putting `uses_of()`,
`type_of()`, and every future analysis method directly on the structural CFG
interface.

## Use Lists

Uses are the first cached graph query:

```cpp
struct InstructionUse
{
    const Instruction *instruction;
    uint16_t operand_index;
};

struct BlockArgumentUse
{
    const BlockEdge *edge;
    uint16_t argument_index;
};

class Uses
{
public:
    const Instruction *def() const;
    const Block *block() const;

    ResultClass result_class() const;
    ValueRepresentation value_representation() const;

    size_t n_uses() const;
    size_t n_instruction_uses() const;
    size_t n_block_argument_uses() const;

    const std::vector<InstructionUse> &instruction_uses() const;
    const std::vector<BlockArgumentUse> &block_argument_uses() const;
};

class UseLists
{
    // Constructed and cached by ControlFlowGraph.
};
```

Construction is a direct graph scan and remains independent of both the
read-only instruction walker and the graph rewriter. It processes each block in
three phases:

1. establish `Uses` entries for the block parameters;
2. walk body instructions in definition order, establishing result entries and
   recording instruction operand uses;
3. walk outgoing edges and record their block-argument uses.

The third phase produces no entries until `BlockEdge` gains its argument
payload. The index nevertheless exposes block-argument uses separately now so
that adding edge arguments does not change the analysis interface.

The index contains one stable `Uses` object for every result-producing
instruction, including definitions with no uses. Its def and block identify the
definition and the block containing all of its uses. The result class and value
representation are derived from the def kind rather than
copied into each use occurrence.

An `InstructionUse` identifies an immutable consuming instruction and its
semantic operand ordinal, not the address of a mutable payload slot. A
`BlockArgumentUse` similarly identifies the outgoing edge and argument
position. Rewriting reconstructs instructions and CFG editing may reconstruct
edges, so stored payload-slot addresses would immediately become stale.

Uses count occurrences, not distinct using instructions. For example, if
`Multiply(value, value)` consumes the same definition in both operands, its
`Uses` contains two `InstructionUse` entries with the same instruction and
different operand indexes. `n_instruction_uses()` and
`n_block_argument_uses()` report the sizes of the corresponding vectors, and
`n_uses()` is their sum. Block-argument uses must participate in the total so a
definition whose only purpose is to feed a successor parameter is not
incorrectly considered dead.

The cache records the graph mutation generation from which it was built.
`Uses` is plain immutable data and its accessors perform no generation checks.
Returned references and their vectors remain stable until graph mutation. A
structural rewrite makes the cached use lists and every outstanding reference
to them stale; the next preparation requesting uses replaces the cache. The
graph does not incrementally maintain permanent use lists.

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

The graph rewriter accepts the same `InstructionTraversal`. Its callback
receives the same query, block, and instruction traversal context as the
read-only walker, preceded by a narrow rewrite-construction interface:

```cpp
GraphRewriter rewriter(arena, graph);

RewriteSummary summary = rewriter.rewrite_instructions(
    traversal,
    [&](RewriteContext &context,
        const GraphQueries &queries,
        const Block &block,
        const Instruction &instruction) -> RewriteResult {
        // instruction belongs to the original published graph
    });
```

`RewriteContext` exposes only operations permitted while constructing a rewrite:

```cpp
class RewriteContext
{
public:
    template <typename T, typename... Args>
    T *make_instruction(Args &&...args);
};
```

`make_instruction<T>(...)` follows the existing construction vocabulary: it
allocates an instruction without placing it. The context does not expose staged
vectors, replacement maps, attachment, commit, generation changes, or other
graph mutation. It may later gain similarly bounded rewrite-construction
helpers without exposing the complete `GraphRewriter`. The rewriter records
which instructions were allocated through its context so it can reject
arbitrary pointers allocated elsewhere.

The callback may construct instructions using references from the original
graph. After it returns, the rewriter resolves those operands through
replacements already established by the walk.

The callback does not mutate the block, attach instructions, or modify operand
slots. It returns the complete output for the current position.

The read-only walker and graph rewriter conform to the same observable
traversal contract but do not share an engine. The rewriter must walk original
vectors while constructing staged vectors and a replacement map; implementing
it by invoking the read-only walker would obscure those ownership rules.

The initial summary is:

```cpp
struct RewriteSummary
{
    bool instructions_changed = false;
    bool terminators_changed = false;
};
```

These distinctions allow attached queries to adopt more selective invalidation
later without making the initial rewriter maintain them incrementally.

## Rewrite Results

A rewrite result contains:

```text
instructions    zero or more instructions emitted at this position
replacement     the canonical def replacing the original def
```

Convenience constructors express the common cases:

```cpp
RewriteResult::keep();
RewriteResult::erase();
RewriteResult::replace(instruction);
RewriteResult::replace(sequence, ProgramValueRef result);
RewriteResult::replace(sequence, SnapshotRef result);
RewriteResult::replace_without_result(sequence);
RewriteResult::replace_with_def(ProgramValueRef existing_def);
RewriteResult::replace_with_def(SnapshotRef existing_def);
```

Their meanings are:

| Result | Emitted instructions | Remembered replacement |
|---|---|---|
| `keep()` | The normalized instruction | The normalized instruction, when it has a result |
| `erase()` | None | Erased |
| `replace(new)` | `new` | `new` when the original has a result; otherwise none |
| `replace(sequence, result)` | The sequence in order | `result` |
| `replace_without_result(sequence)` | The sequence in order | None |
| `replace_with_def(def)` | None | `def` |

`replace_without_result()` is valid only when the original instruction has
`ResultClass::None`. It never infers that the final emitted instruction should
become a replacement def.

`replace(new)` is the single-instruction convenience form. When the original
has a result, `new` must produce a compatible replacement result. When the
original has no result, the emitted instruction establishes no replacement def.
`replace_with_def()` is equivalent to an empty emitted sequence plus an
explicit existing replacement def.

For example, eliminating an identity operation emits nothing and redirects its
uses to the identity's source:

```cpp
return RewriteResult::replace_with_def(identity.source());
```

An existing graph instruction is never returned as an emitted replacement
instruction merely to express this substitution.

A replacement sequence separates execution from value identity. For example,
lowering one operation to an AArch64 immediate materialization followed by an
add emits both instructions but maps later uses of the old result to the add:

```cpp
return RewriteResult::replace({arm_imm12, add}, TaggedValueRef(add));
```

A one-to-one rewrite such as a pointer-valued `Const` becoming `LoadConst` is:

```cpp
return RewriteResult::replace(load_const);
```

The instructions emitted for one callback are not revisited during the same
walk. Fixed-point rewriting, if required, is a separate driver or another pass.

## Result and Sequence Invariants

The rewriter validates each result before commit:

- every proposed instruction was freshly allocated through this rewrite's
  `RewriteContext`;
- the only original instruction emitted at the current position is the current
  instruction selected by `keep()`;
- an existing graph def used as a replacement is named through
  `replace_with_def()` rather than emitted;
- sequence order satisfies definition-before-use;
- every operand refers to a block parameter, an earlier staged result, or an
  earlier instruction in the same sequence;
- a replacement for a `ProgramValue` has the same `ValueRepresentation`;
- a replacement for a `Snapshot` is another `Snapshot` result;
- an instruction with `ResultClass::None` has no replacement result;
- a result-producing instruction has a compatible replacement unless it is
  explicitly erased;
- no non-final sequence instruction is a block terminator.

While normalizing one emitted sequence, the rewriter records each proposed def
and its normalized def before processing later instructions in that sequence.
If normalization reconstructs an earlier proposed instruction, later sequence
instructions are therefore rebuilt to use the reconstructed def.

The pass owns semantic correctness. Structural acceptance of erasing an unused
call, for example, does not prove that discarding its effects is valid.

## Erasure

Erasure is sequence replacement with an empty sequence. The replacement map
retains an explicit erased state rather than silently dropping the original
definition.

Erasing a value or Snapshot def is valid when it has no later uses. If a
later instruction refers to an erased definition, operand normalization reports
a compiler error identifying the erased def and its use. Because the
rewrite is staged, this does not expose a partially rewritten block.

The graph-rewrite mechanism does not itself decide whether an instruction is
dead. A dead-code-elimination pass requests use lists, makes its own effect
and liveness decisions, and returns `erase()` for removable instructions. A
simple post-rewrite DCE pass can remove constants made dead by an earlier
folding or lowering pass.

## Instruction Reconstruction

The instruction schema generates a generic reconstruction operation:

```cpp
Instruction *rebuild_instruction_with_operands(
    Instruction &instruction,
    DefResolver resolver,
    InstructionFactory &factory);
```

It reconstructs the same concrete instruction kind with resolved typed operands
and unchanged attributes. It returns the original instruction when no operand
changed. The graph rewriter supplies only its old-def-to-new-def resolution;
typed operand adaptation, variadic reconstruction, attribute copying, and the
generated kind switch belong to the instruction layer. Reconstruction is
generated from `src/jit/instruction.def`; it does not mutate raw slots or
introduce handwritten cloning switches.

When reconstruction creates a new instruction, keeping it is still a structural
replacement. The rewriter records the original definition as mapping to the
reconstructed definition so the substitution propagates transitively.

## Traversal Direction

Mutating graph rewrites initially walk forward only. Forward order lets the
rewriter normalize each callback result using decisions already made for every
definition that may legally appear in its operands.

A backward mutating walk cannot provide that guarantee: it visits a use before
the callback has decided how to rewrite its def. Supporting it would
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

This graph-wide commit keeps the original graph and prepared `GraphQueries`
valid throughout all callbacks. No callback observes a graph in which only
earlier blocks have committed.

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

The graph rewriter prepares only the queries declared by its
`InstructionTraversal`. A pass requests uses only when its semantic decisions
require use counts or use enumeration. Because callbacks receive original
instructions and commit occurs only after every callback, the prepared
`GraphQueries` remains valid throughout the rewrite walk. The completed
structural rewrite then advances the graph generation, making that façade and
the cached index stale. A later traversal requesting uses rebuilds it.

The rewrite summary records at least whether instructions changed and whether
the terminator changed. Since this API preserves successor edges, ordinary
instruction rewrites preserve CFG topology even when they invalidate
instruction-indexed analyses.

## Related Documents

- [JIT Instruction Representation](jit-instruction-representation.md)
- [JIT Control-Flow Graph](jit-control-flow-graph.md)
- [JIT Compiler and IR](jit-compiler-and-ir.md)
- [JIT Machine-Code Emission](jit-machine-code-emission.md)
