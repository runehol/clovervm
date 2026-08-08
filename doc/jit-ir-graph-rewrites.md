# JIT IR Graph Rewrites

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Read-only traversal, fixed-point block scheduling, block-parameter joins, instruction use lists, body-instruction rewriting, all block-parameter rewrite outcomes, staged edge splitting, and global dead-code elimination are implemented; arbitrary edge redirection and general CFG-topology rewriting remain open |
| Scope | Traversal and graph queries, immutable-instruction rewriting, block-parameter joins, atomic parameter/edge-column rewrites, and the narrow staged edge-splitting operation in published JIT IR graphs |
| Owning layers | The CFG owns entry metadata, mutation generation, join structure, and cached analysis storage; traversal owns ordering and scheduling; `GraphQueries` owns generation-checked callback access; the graph rewriter owns operand substitution, instruction placement, atomic parameter-column mutation, narrow edge splitting, and commit; the instruction schema owns reconstruction; individual passes own matching, lattices, and semantic legality |
| Validated against | `tests/test_jit_graph_rewrites.cpp`, `tests/test_jit_constant_folding.cpp`, `tests/test_jit_f64_box_simplification.cpp`, `tests/test_jit_core_ir_optimization.cpp`, and `tests/test_jit_dead_code_elimination.cpp` |
| Supersedes | The incremental mutable-operand rewrite direction in [JIT Instruction Representation](jit-instruction-representation.md) and [JIT Compiler and IR](jit-compiler-and-ir.md) |

JIT IR instructions are immutable. A graph rewrite constructs a replacement
instruction stream rather than editing instruction payloads or rewriting use
slots in place. By default, the callback sees each instruction from the
original published graph. A rewrite may instead request an input normalized
through replacements for definitions already visited. After the callback
returns, the rewriter resolves the proposed output in either mode and appends
the canonical form to a staged instruction stream.

This design covers local instruction rewrites, lowering one instruction to an
instruction sequence, erasure, destination materialization, atomic
block-parameter representation conversion, and passes such as dead-code
elimination. It also covers the graph rewriter's deliberately narrow staged
edge-splitting operation. General addition, removal, and redirection of CFG
edges remain outside this interface.

Read-only traversal, use-list construction, and structural rewriting remain separate
algorithms. The CFG owns on-demand cached analysis storage because it also owns
the mutation generation and the instructions indexed by those analyses.
Walkers and rewriters prepare the queries declared by their common traversal
configuration and pass a generation-checked query façade to callbacks.

## Shared Traversal Contract

Read-only block walks request a shared block order. Instruction walks carry one
of those order values together with their requested graph queries:

```cpp
enum class BlockOrder : uint8_t
{
    Program,
    Forward,
    Backward,
};

std::vector<const Block *>
ordered_blocks(const ControlFlowGraph &, BlockOrder);

enum class GraphQuery
{
    None,
    Uses,
    TaggedValueFacts,
};

class InstructionTraversal
{
public:
    constexpr InstructionTraversal() = default;

    [[nodiscard]] constexpr InstructionTraversal
    with_block_order(BlockOrder order) const;

    [[nodiscard]] constexpr InstructionTraversal
    with_queries(GraphQuery queries) const;

    constexpr BlockOrder block_order() const;
    constexpr GraphQuery queries() const;

private:
    BlockOrder block_order_ = BlockOrder::Program;
    GraphQuery queries_ = GraphQuery::None;
};
```

`GraphQuery` members are combinable flags; their numeric encoding is an
implementation detail. The `with_*()` methods return altered copies and leave
the original traversal unchanged. This allows a pass to derive a local
traversal policy from a shared default without mutable configuration:

```cpp
InstructionTraversal traversal =
    InstructionTraversal()
        .with_block_order(BlockOrder::Forward)
        .with_queries(GraphQuery::Uses);
```

Program order follows the stored block vector. Forward order is deterministic
component-wise reverse postorder, rooted first at the normal entry, then the
registered exception entries, and finally any remaining components in program
order. Backward order is the reverse of that complete forward order. The CFG
entry metadata and exact ordering rules are specified in
[JIT Control-Flow Graph](jit-control-flow-graph.md). Every order visits every
block exactly once. Body instructions within each block are visited forward,
including the terminator. Block parameters are not part of instruction
traversal; join-aware code uses `ControlFlowGraph::block_parameter_joins()`.

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

### Fixed-point block scheduling

Traversal direction and convergence are separate policies. A monotone analysis
can use the shared scheduler after defining its own lattice and transfer
function:

```cpp
enum class DataflowUpdate : uint8_t
{
    Unchanged,
    Changed,
};

enum class FixedPointStatus : uint8_t
{
    Converged,
    RevisitLimitReached,
};

iterate_blocks_to_fixed_point(
    graph, order, maximum_total_revisits,
    [&](const Block &block) -> DataflowUpdate {
        // Changed means information visible to dependants changed.
    });
```

The driver first visits every block in the selected order. During that sweep, a
changed block queues only dependants already visited; an unvisited dependant
will observe the accumulated state during its guaranteed initial visit. It then
drains a deduplicating FIFO queue. Forward traversal schedules successors and
backward traversal schedules predecessors; program order is rejected because
it defines no dependency direction. The initial sweep does not count against
the global revisit limit.

`RevisitLimitReached` is a result, not a mandated compilation failure. Each
caller owns the conservative policy for its domain. Tagged-value fact analysis
widens all tagged facts to unknown, constant-join analysis abandons its
constant conclusions, and another caller may choose compilation fallback. No
caller may consume a partial result as though it had converged.

This scheduler is for monotone movement through a finite-height lattice, or for
a domain with an explicit widening policy. It is not the driver for repeated
structural optimization. A committed rewrite invalidates join views and graph
queries and advances the mutation generation, so an optimization pipeline runs
fresh passes in bounded rounds and owns its own termination policy. The Core IR
optimizer accepts the valid graph produced after its defensive round limit;
the limit is not itself a compilation error.

## Block-Parameter Joins

A block parameter and the argument at the same position on every incoming edge
form one semantic join. The CFG exposes that relationship directly:

```cpp
struct IncomingArgument
{
    BlockEdgeId edge;
    ProgramValueRef value;
};

class BlockParameterJoin
{
public:
    const Block &block() const;
    Instruction parameter() const;
    auto incoming_arguments() const;
    ProgramValueRef argument_from(BlockEdgeId edge) const;
};

class ControlFlowGraph
{
public:
    auto block_parameter_joins(const Block &) const;
};
```

The parameter instruction ID is the join's structural identity. The
argument-column index is private and is resolved from that ID, so callbacks do
not observe shifting indexes when several columns change in one transaction.
The join and its lazy incoming range are ephemeral borrowed views. They must not
be retained across graph mutation.

This shared read view is used by tagged-value fact propagation, constant join
folding, dead-code elimination, equivalent-parameter elimination, and F64 join
conversion. It centralizes CFG structure without centralizing pass semantics:
analyses still own their lattices and transformations still prove legality.

### Destination-local replacement

Ordinary definitions are block-local. Even if every predecessor supplies an
apparently equal value, that predecessor definition cannot directly replace a
destination parameter. A rewrite must either retain another parameter in the
same destination or materialize a replacement at destination entry.

Constant join folding is the canonical materialization case. Once analysis has
proved an exact tagged value or exact F64 bit pattern on every incoming path,
the pass inserts the corresponding constant in the destination, redirects the
old parameter's uses to it, and removes the parameter's argument column. For a
boxed object, the new tagged constant refers to the same object; numeric or
Python equality is insufficient.

A self-edge may be ignored while proving such a constant only after the other
incoming values establish a destination-materializable result. The result is
still inserted in the loop block. It does not make a predecessor-local
definition visible across the edge.

### Atomic representation conversion

Representation conversion changes one complete join relationship atomically.
For example, F64 box simplification can replace a tagged parameter fed only by
eligible `BoxF64` results with an F64 parameter, replace every incoming edge
argument with its available F64 source, insert one `BoxF64` at destination
entry, and redirect the old tagged uses to that destination box.

The pass proves that discarding the incoming box identities is semantically
legal. The graph rewriter proves structural completeness: every incoming edge
is named exactly once, each replacement has the new representation and is
available in its source block, the destination materialization is well placed,
and its result can replace all old uses. It rejects the entire request if any
part fails; it never partially converts a column or inserts source-block work.

A self-edge must name its replacement explicitly. It may use the new parameter
for an unchanged recurrence or an available compatible body definition. The
rewriter does not infer a mapping from the removed parameter. This explicit
rule also permits several converted columns, including cross-column
recurrences, to be staged without exposing transient indexes.

## Generation-Checked Graph Queries

The CFG directly owns optional cached analyses. There is no separate public
cache-manager object:

```cpp
class ControlFlowGraph
{
    uint64_t mutation_generation_;
    mutable std::unique_ptr<UseLists> use_lists_;
    mutable std::unique_ptr<TaggedValueFactAnalysis> tagged_value_facts_;
};
```

`GraphQueries` is a lightweight per-operation capability, not the cache owner:

```cpp
class GraphQueries
{
public:
    const ControlFlowGraph &graph() const;
    const Uses &uses_of(const Instruction &) const;
    const TaggedValueSet &tagged_value_facts_of(ProgramValueRef) const;

private:
    const ControlFlowGraph *graph_;
    GraphQuery requested_;
    const UseLists *use_lists_;
    const TaggedValueFactAnalysis *tagged_value_facts_;
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
`tagged_value_facts_of()`, and every future analysis method directly on the
structural CFG interface.

## Use Lists

Uses are the first cached graph query:

```cpp
struct InstructionUse
{
    InstructionId instruction;
    uint32_t operand_index;
};

struct BlockArgumentUse
{
    const BlockEdge *edge;
    size_t argument_index;
};

class Uses
{
public:
    Instruction def() const;
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

The index contains one stable `Uses` object for every result-producing
instruction, including definitions with no uses. Its def and block identify the
definition and its containing block. The result class and value representation
are derived from the def kind rather than copied into each use occurrence.

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

In the default original-input mode, the rewriter performs:

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

This model relies on the current Core IR rule that an ordinary instruction
result is used only later in the same block. Block parameters are definitions
available before the instruction stream. Parameter callbacks operate on whole
joins and may keep a column, remove it, replace the parameter with another
destination parameter, materialize a destination-local replacement, or convert
the complete column to another representation. They do not introduce general
cross-block SSA values. Successor replacement and arbitrary edge redirection
would require a broader CFG-editing design.

## Rewrite API

The graph rewriter accepts the same `InstructionTraversal`. Its callback
receives the same query, block, and instruction traversal context as the
read-only walker, preceded by a narrow rewrite-construction interface:

```cpp
enum class RewriteInput : uint8_t
{
    Original,
    Normalized,
};

GraphRewriter rewriter(session, graph);

RewriteSummary summary = rewriter.rewrite_instructions(
    traversal, RewriteInput::Original,
    [&](RewriteContext &context,
        const GraphQueries &queries,
        const Block &block,
        const Instruction &instruction) -> RewriteResult {
        // instruction belongs to the original published graph
    });
```

The overload without a `RewriteInput` selects `Original`.

A callback class may also provide the optional parameter hook:

```cpp
BlockParameterRewrite block_parameter(
    RewriteContext &context,
    const GraphQueries &queries,
    const BlockParameterJoin &join);
```

The ephemeral join identifies the destination block and parameter and exposes
its incoming edge arguments. The callback returns
one of these deliberately distinct outcomes:

```cpp
BlockParameterRewrite::keep();
BlockParameterRewrite::erase();
BlockParameterRewrite::replace_with_destination_parameter(parameter);
BlockParameterRewrite::materialize_in_destination(insertion, result);
BlockParameterRewrite::convert_representation(
    replacement_parameter, incoming_argument_replacements,
    destination_materialization, materialized_result);
```

`materialize_in_destination()` removes the old column and emits a replacement
at the destination entry. `convert_representation()` names the new parameter,
the complete owned replacement edge column, and the destination result that
replaces old uses. These are separate legality contracts, not a general
edge-mutation object. Decisions are collected for the whole graph before
instruction rewriting so all parameter and edge vectors can be rebuilt
consistently.

`RewriteContext` also exposes
`retain_and_pin_value()`. A transformation calls it immediately when creating a
managed value, or before introducing an existing pointer constant that has not
already been registered with this compilation session. The operation appends an
`Owned<Value>` to the session's monotonic retained-value vector and returns the
same typed handle. It is separate from instruction construction: the rewriter
does not scan new instructions for constants, and unused retained values remain
owned until session teardown.

`Original` passes the instruction from the published graph to the callback.
This is the appropriate view for DCE and other analysis-driven passes.
`Normalized` first reconstructs the instruction using replacements established
for earlier definitions, then passes that normalized instruction to the
callback. This is the appropriate view for lowering and canonicalization.
`keep()` retains the instruction actually passed to the callback, so normalized
input does not revert to the original instruction.

Result normalization still runs after the callback in both modes. This resolves
operands on newly emitted instructions and keeps one output contract regardless
of the selected input view.

`Normalized` cannot be combined with `GraphQuery::Uses`. Use lists
describe the original published graph, while a normalized callback may receive
a newly allocated instruction that has no entry in them. If a concrete pass
eventually needs normalized matching and original use information, it should
introduce an explicit dual-view callback rather than making this relationship
implicit.

`RewriteContext` exposes only operations permitted while constructing a rewrite:

```cpp
class RewriteContext
{
public:
    template <typename T, typename... Args>
    T make_instruction(Args &&...args);

    template <typename T>
    T retain_and_pin_value(T value);

    Instruction instruction(InstructionId id) const;

    SideExitRegion *
    make_side_exit_region(std::span<const InstructionId> parameter_ids,
                          std::span<const InstructionId> instruction_ids);
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
slots. Its supported hooks return complete insertions or replacements for
specific staged positions:

```cpp
RewriteInsertion at_block_entry(
    RewriteContext &, const GraphQueries &, const Block &);

RewriteInsertion before_instruction(
    RewriteContext &, const GraphQueries &, const Block &,
    const Instruction &);

RewriteResult rewrite_instruction(
    RewriteContext &, const GraphQueries &, const Block &,
    const Instruction &);
```

A callback object may implement any useful combination of these hooks and
`block_parameter()`. A plain callable remains the concise instruction-only
form. `RewriteInsertion` represents an instruction sequence plus any explicit
transfer outputs needed by edge-transfer or side-exit lowering.

The read-only walker and graph rewriter conform to the same observable
traversal contract but do not share an engine. The rewriter must walk original
vectors while constructing staged vectors and a replacement map; implementing
it by invoking the read-only walker would obscure those ownership rules.

The summary is:

```cpp
struct RewriteSummary
{
    bool block_parameters_changed = false;
    bool blocks_changed = false;
    bool instructions_changed = false;
    bool terminators_changed = false;
    bool ir_level_changed = false;
    NormalizationRemapping normalization_remapping;
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
RewriteResult::keep_with_prefix(sequence);
RewriteResult::keep_with_suffix(sequence);
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
| `keep_with_prefix(sequence)` | The sequence, then the normalized instruction | The normalized instruction, when it has a result |
| `keep_with_suffix(sequence)` | The normalized instruction, then the sequence | The normalized instruction, when it has a result |
| `erase()` | None | Erased |
| `replace(new)` | `new` | `new` when the original has a result; otherwise none |
| `replace(sequence, result)` | The sequence in order | `result` |
| `replace_without_result(sequence)` | The sequence in order | None |
| `replace_with_def(def)` | None | `def` |

`erase()` removes the instruction from block order and poisons the removed
instruction after commit. It does not provide a replacement definition, so a
surviving executable use is invalid.

The insertion helpers retain the current instruction without requiring the pass
to clone it. Their supplied instructions are normalized in sequence order like
ordinary replacement instructions. A suffix may consume the current result:
the rewriter records the normalized current instruction before normalizing the
suffix. In either form, uses of the old definition map to the normalized current
instruction rather than to an inserted instruction.

`keep_with_suffix()` is not valid for a terminator because it would place
instructions after the end of the block. A prefix may precede a retained
terminator normally.

`replace_without_result()` is valid only when the original instruction has
`ResultClass::None`. It never infers that the final emitted instruction should
become a replacement def.

`replace(new)` is the single-instruction convenience form. When the original
has a result, `new` must produce a compatible replacement result. When the
original has no result, the emitted instruction establishes no replacement def.
`replace_with_def()` is equivalent to an empty emitted sequence plus an
explicit replacement def already available in the staged block. In normalized
mode that may be a reconstructed earlier def rather than an instruction from
the published graph.

For example, eliminating an identity operation emits nothing and redirects its
uses to the identity's source:

```cpp
return RewriteResult::replace_with_def(identity.source());
```

An available definition is never returned as an emitted replacement instruction
merely to express this substitution.

A replacement sequence separates execution from value identity. For example,
lowering one operation to an AArch64 immediate materialization followed by an
add emits both instructions but maps later uses of the old result to the add:

```cpp
return RewriteResult::replace({arm_imm12, add}, TaggedValueRef(add));
```

A one-to-one rewrite such as constant-folding an operation to a new `Const` is:

```cpp
return RewriteResult::replace(folded_const);
```

The instructions emitted for one callback are not revisited during the same
walk. Fixed-point rewriting, if required, is a separate driver or another pass.

## Result and Sequence Invariants

The rewriter validates each result before commit:

- every proposed instruction was freshly allocated through this rewrite's
  `RewriteContext`;
- the only original instruction emitted at the current position is the current
  instruction selected by `keep()`;
- a def already available in the staged block is named through
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

### Release-mode invariant checks

Checks whose failure would otherwise propagate null definitions, dereference a
failed lookup, publish malformed instruction placement, or corrupt result and
terminator typing panic in every build. These checks are local to an operation
the rewriter already performs: pointer validation, hash-table membership,
result compatibility, and terminator inspection.

Assertions that merely restate construction proofs or verified graph structure
remain debug-only. The complete `verify_cfg()` pass also runs only in debug and
test builds after commit; release builds do not pay for a second whole-graph
scan after the rewriter's local checks have succeeded.

## Erasure

Erasure is sequence replacement with an empty sequence. The replacement map
retains an explicit erased state rather than silently dropping the original
definition.

Erasing a value or Snapshot def is valid when it has no later uses. If a
later instruction refers to an erased definition, operand normalization reports
a compiler error identifying the erased def and its use. Because the
rewrite is staged, this does not expose a partially rewritten block.

The graph-rewrite mechanism does not itself decide whether an instruction is
dead. The implemented Core dead-code-elimination pass performs one global
mark-and-sweep instead of iterating local use counts:

1. mark every instruction that cannot be discarded under its conservative
   effect profile;
2. follow ordinary operand definitions from every marked instruction;
3. when a marked definition is a block parameter, follow the corresponding
   argument on every predecessor edge;
4. keep entry parameters as external roots;
5. erase unmarked result definitions and non-entry parameters, letting the
   rewriter compact incoming edge arguments atomically.

This removes dead cross-block chains and dead cycles without dominators or
repeated use-list rebuilding. Effect legality belongs to the DCE pass; the
rewriter only provides the structural erasure and compaction mechanism.

## Instruction Reconstruction

The instruction schema generates a generic reconstruction operation:

```cpp
Instruction rebuild_instruction_with_references(
    Instruction &instruction,
    const CompilationStorage &storage,
    const DefResolver &resolver,
    InstructionFactory &factory,
    InstructionRebuildMode mode =
        InstructionRebuildMode::ReuseIfUnchanged);
```

It reconstructs the same concrete instruction kind with resolved typed operands
and resolved first-class `BlockEdge` attributes; other attributes remain
unchanged. It returns the original instruction when no reference changed. The
graph rewriter supplies only its old-reference-to-new-reference resolution;
typed operand adaptation, variadic reconstruction, attribute copying, and the
generated kind switch belong to the instruction layer. Reconstruction is
generated from `src/jit/instruction.def`; it does not mutate raw slots or
introduce handwritten cloning switches. `AlwaysClone` is available when a
transaction needs a distinct instruction even though its references did not
change; the default reuses an unchanged instruction.

When reconstruction creates a new instruction, keeping it is still a structural
replacement. The rewriter records the original definition as mapping to the
reconstructed definition so the substitution propagates transitively.

## Traversal Direction

Mutating graph rewrites require program block order and walk instructions
forward within each block. Forward instruction order lets the rewriter
normalize each callback result using decisions already made for every
definition that may legally appear in its operands.

A backward mutating walk cannot provide that guarantee: it visits a use before
the callback has decided how to rewrite its def. Supporting it would
require stale callback inputs or a separate decision and reconstruction phase.

Read-only block traversal supports backward block order while retaining forward
instruction order within each block. Reverse instruction traversal and a
backward rewrite driver are deferred until a concrete pass establishes their
required semantics.

## Staging and Commit

The rewriter builds new parameter and body `InstructionId` vectors per block
while all original block vectors remain unchanged:

```cpp
struct StagedBlockRewrite
{
    Block *block;
    std::vector<InstructionId> parameters;
    std::vector<InstructionId> instructions;
    std::vector<InstructionId> removed_originals;
};

struct StagedBlockParameterRewrite
{
    InstructionId original_parameter;
    BlockParameterRewrite rewrite;
    std::optional<InstructionId> output_parameter;
};
```

When no parameter callback exists, the original parameter vector is copied
unchanged. When one exists, each original parameter produces an explicit staged
column with zero or one output parameter and zero or one incoming edge column.
Keep preserves both; erase, destination-parameter replacement, and destination
materialization remove both; representation conversion supplies a new
parameter and a complete new edge column. Destination-parameter vectors and
incoming-edge vectors are reconstructed from that same plan, so multiple
simultaneous changes cannot disagree or observe shifting indexes.

Destination materializations are emitted in original parameter order after the
new parameter vector and before ordinary block-entry insertion. A
materialization may use definitions available at destination entry but cannot
smuggle in predecessor-local definitions. For a conversion, each source edge
argument is checked at the source terminator, including the explicit mapping
used for a self-edge.

After every block has been traversed:

1. it validates every completed output vector, edge argument vector, and
   replacement map;
2. it swaps every staged parameter and body vector into its block and rebuilds
   predecessor indexes when edges changed;
3. it uses staged removal records to identify and poison erased or replaced
   originals;
4. it commits the rewriter's target IR level, which defaults to the graph's
   existing level;
5. it advances the graph mutation generation once and invalidates affected
   attached analyses;
6. debug and test configurations verify the completed graph.

This graph-wide commit keeps the original graph and any permitted prepared
`GraphQueries` valid throughout all callbacks. No callback observes a graph in
which only earlier blocks have committed.

Instruction entries created during the rewrite are append-only and need no
rollback. The rewriter borrows `CompilationStorage` from the compilation
session passed to its constructor. Allocation failure abandons the session
under the existing JIT failure model. Ordinary passes cannot observe the staged
block.

The callback-class API detects the presence of each optional hook at compile
time, so passes pay only for the events they implement. A cursor or additional
rewrite event may later extend the same staging engine, but it must preserve
the selected input view, post-result normalization, and commit rules.

## Terminators and CFG Changes

Instruction rewriting and general CFG editing remain separate responsibilities,
with one narrow integrated topology operation.

- a terminator cannot be erased;
- replacing a non-terminator cannot emit a terminator;
- a sequence replacing a terminator ends in exactly one terminator;
- a replacement terminator preserves the original successor edges.

Parameter rewriting reconstructs each incoming edge and its owning terminator
with a matching argument column while normally preserving the source, target,
order, and number of edges.

`GraphRewriter::stage_edge_splits()` is the implemented exception. Before a
query-free rewrite, a caller may request pass-through blocks placed explicitly
after the source or before the target. Each staged block has one
representation-matched parameter per original edge argument and an outgoing
edge to the old target. The blocks become visible only with the surrounding
rewrite commit, allowing block-entry insertions to populate them without an
intermediate published graph. The operation does not combine edge splitting
with block-parameter compaction in the same transaction.

General successor replacement, arbitrary edge redirection, adding or removing
outgoing edges, and replacing an operation with a multi-block region remain
unimplemented. The CFG guide owns the detailed edge and predecessor-index
contract.

## Analysis Interaction

The graph rewriter prepares only the queries declared by its
`InstructionTraversal`. A pass requests uses only when its semantic decisions
require use counts or use enumeration. Such analysis-driven passes select
`RewriteInput::Original`; normalized input rejects use lists. Because commit
occurs only after every callback, permitted prepared `GraphQueries` remain valid
throughout the rewrite walk. The completed structural rewrite then advances the
graph generation, making that façade and the cached index stale. A later
traversal requesting uses rebuilds it.

The rewrite summary separately records block-parameter, block-topology,
instruction, terminator, and IR-level changes. Ordinary instruction rewrites
preserve CFG topology; a staged edge split reports its block change explicitly.

## Deliberate Boundaries

The shared machinery stops at structure and scheduling. It does not introduce a
general dataflow framework or declarative edge-pattern language. Passes own
their semantic domains, transfer functions, matching, and legality. In
particular, guards, allocations, snapshots, and potentially failing operations
cannot move through a join merely because incoming instructions have the same
shape.

F64 join conversion is an instructive client, not policy embedded in the
rewriter. Its identity and snapshot restrictions belong to F64 box
simplification. Snapshot-only box sinking into side exits is a different
Core-to-Machine transformation: it identifies rematerializable cones, lowers
them into side-exit regions, and relies on Machine IR dead-code elimination for
the hot-path originals.

## Related Documents

- [JIT Instruction Representation](jit-instruction-representation.md)
- [JIT Control-Flow Graph](jit-control-flow-graph.md)
- [JIT Compiler and IR](jit-compiler-and-ir.md)
- [JIT Machine-Code Emission](jit-machine-code-emission.md)
