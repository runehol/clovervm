# JIT Block-Parameter Joins Implementation Plan

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Accepted |
| Implementation | Partial: slices 1 through 8 are implemented; slices 9 through 11 are not started |
| Scope | Staged implementation of block-entry metadata, block traversal, fixed-point scheduling, block-parameter joins, destination-only join rewriting, generic constant folding, and restricted cross-edge F64 conversion |
| Owning layers | `Value::operator==` defines CloverVM tagged-identity comparison; bytecode lowering registers CFG entries; the CFG owns entry metadata and join structure; traversal owns ordering and scheduling; analyses own transfer and conservative fallback; `GraphRewriter` owns atomic join mutation; optimization passes own semantic legality |
| Validated against | N/A |
| Supersedes | N/A |

This plan implements the design in
[JIT Block-Parameter Joins and Dataflow Traversal](jit-block-parameter-joins-and-dataflow.md).
It deliberately stops short of changing CloverVM's language-level identity
policy. The interpreter and JIT continue to use the same existing tagged-value
identity relation.

`Value::operator==` already compares the complete tagged representation and is
the compile-time identity test. No new identity helper or interpreter change is
part of this work.

## Slice 1: Register All CFG Entry Blocks

Preserve the entry information already owned by bytecode decoding:

```cpp
class GraphBuilder
{
public:
    void register_exception_entry_block(Block *block);
};

class ControlFlowGraph
{
public:
    Block *normal_entry_block() const;
    std::span<Block *const> exception_entry_blocks() const;
    std::span<Block *const> entry_blocks() const;
};
```

One authoritative entry vector stores the normal entry first followed by the
exception entries. The two range queries return spans over the complete vector
and its exception-only suffix, so the partition cannot drift out of sync. The
first graph block remains the normal entry. Exception entries retain the
decoder's deterministic order. `CoreBytecodeTranslator` registers every block
named by
`BytecodeDecoder::exception_handler_block_ids()` after allocating the graph
blocks and before publication.

The verifier rejects duplicate entries, entries owned by another graph, a
missing normal entry, and an entry vector whose first member is not the normal
entry. Entry status is never inferred from an empty predecessor list.

Tests cover one normal entry, multiple exception entries, and an ordinary
predecessorless block that is not registered as an entry.

## Slice 2: Add Complete Block Traversal

Introduce a read-only traversal value independent of graph rewriting:

```cpp
enum class BlockOrder : uint8_t
{
    Program,
    Forward,
    Backward,
};

std::vector<const Block *>
ordered_blocks(const ControlFlowGraph &, BlockOrder);
```

Forward order is deterministic multi-root reverse postorder. The traversal
starts with the normal entry and then each registered exception entry, visiting
successor edges in their semantic terminator order. It next walks any remaining
components in stored program order so every graph block occurs exactly once.
Backward order is the reverse of that complete forward order. Program order is
the graph's stored block vector.

`InstructionTraversal` delegates block enumeration to this vocabulary while
retaining forward instruction order within each block. The initial graph
rewriter continues to request program order explicitly; changing rewrite order
is not part of this slice.

Tests cover diamonds, loops, duplicate source/target edge occurrences,
exception entries, disconnected components, exact-once visitation, and stable
ordering.

## Slice 3: Add Deduplicated Fixed-Point Scheduling

Add a reusable FIFO queue and a block fixed-point driver:

```cpp
template <typename T> class DeduplicatingQueue;

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

template <typename Callback>
FixedPointStatus iterate_blocks_to_fixed_point(
    const ControlFlowGraph &, BlockOrder, size_t maximum_total_revisits,
    Callback &&);
```

The driver performs the complete one-pass traversal before draining revisits.
During that pass, a changed block queues only dependants already visited; an
unvisited dependant will observe the change during its guaranteed initial
visit. Forward dependants are successors and backward dependants are
predecessors.
`BlockOrder::Program` is rejected because it defines no dependency direction.

The revisit queue is FIFO and contains each block at most once. Removing a
block clears its queued mark, so a later change may enqueue it again. No
priority queue or recency priority is used.

The guaranteed initial sweep does not count against the limit. One global
counter records every subsequent callback invocation; there is no per-block
counter. Each analysis chooses a generous graph-scaled total appropriate to
its lattice height. The driver returns `RevisitLimitReached` only when the
limit is exhausted with work still queued, rather than deciding failure
policy. A fact analysis may conservatively replace its result with top facts.
A transformation may return a compilation error. No caller may consume a
partial fixed-point result as if it had converged.

Tests pin the complete initial visit, forward and backward revisits, loop
convergence, FIFO order, duplicate suppression, self-requeue, and the revisit
limit.

## Slice 4: Add the Read-Only BlockParameterJoin View

Add the common CFG relationship:

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
    ProgramValueRef argument_from(BlockEdgeId) const;
};

class ControlFlowGraph
{
public:
    auto block_parameter_joins(const Block &) const;
};
```

The parameter instruction ID identifies the join. The CFG privately resolves
the parameter's argument-column index; passes do not retain or mutate that
index. Both queries use lazy standard transform views rather than custom
iterators or allocated vectors. Joins and their borrowed ranges are ephemeral
and are not retained across structural mutation.

Refactor three existing clients without changing their semantics:

1. equivalent-parameter analysis compares join columns;
2. DCE follows a live join to its incoming arguments;
3. tagged-value fact analysis merges incoming facts through joins.

Tagged-value fact analysis also moves to forward fixed-point traversal. It
seeds the parameters of every registered normal or exception entry with
`unknown`. If the revisit limit is reached, it sets every tagged definition's
fact to `unknown`; this is conservative and preserves the current infallible
`GraphQueries` preparation API.

Tests retain all existing analysis cases and add exception-entry facts,
self-edge joins, and two distinct edges with one source and target.

## Slice 5: Make Block-Parameter Callbacks Join-Based

Change the graph-rewriter callback from separate block/index/parameter inputs
to the structural view:

```cpp
BlockParameterRewrite block_parameter(
    RewriteContext &, const GraphQueries &, const BlockParameterJoin &);
```

Existing outcomes remain available:

```cpp
BlockParameterRewrite::keep();
BlockParameterRewrite::erase();
BlockParameterRewrite::replace_with_destination_parameter(parameter);
```

Replacement parameters must be retained parameters of the same destination
block with compatible results. The rewriter snapshots every decision against
the original parameter IDs, then reconstructs all parameter vectors and edge
argument columns together. Multiple removals cannot expose shifting indexes to
the callback.

This slice changes no representation and inserts no instructions. Existing
rewriter, DCE, and equivalent-parameter tests prove compatibility.

## Slice 6: Add Destination Materialization Mechanics

Extend a parameter rewrite with a destination-local replacement:

```cpp
BlockParameterRewrite::materialize_in_destination(
    RewriteInsertion insertion, ProgramValueRef result);
```

The insertion is placed after destination parameters and before its original
body. Its result replaces the old parameter throughout that block. The old
parameter and its argument column are removed atomically. The insertion may
reference retained destination parameters but never predecessor-local
definitions.

Materializations are emitted in original parameter order before the ordinary
block-entry insertion. The result must be emitted exactly once by its own
insertion and have the same result class and value representation as the
removed parameter. A materialization cannot transfer unrelated definitions or
refer to another parameter materialization from the same rewrite transaction.

Mechanical tests cover simultaneous non-adjacent materializations, argument
column compaction, use redirection, deterministic placement, and rejection of
predecessor-local operands. This slice adds no constant-specific behavior.

## Slice 7: Add ConstF64

Add an F64 constant instruction that is valid in Core and Machine IR. Its
attribute stores the exact `uint64_t` payload bits, and IR printing shows both
the decoded floating value and those bits. Constant comparison uses the bit
representation so signed zero and NaN payloads remain distinct.

Teach allocation constraints and the AArch64 emitter to materialize arbitrary
F64 constants without boxing. Constants matching the exact architectural
expansion of an AArch64 floating-point imm8 use `fmov dN, #imm` and require no
temporary GPR. Positive zero uses the 64-bit mask form of `movi dN, #imm`, with
an all-clear byte mask. All other constants place the exact bits in the
untagged constant-pool area, obtain the pool address through a reserved backend
scratch GPR, and load the value into the assigned SIMD register. ConstF64
therefore has no allocator constraints; immediate selection remains entirely
in emission. Representability is decided from the stored `uint64_t` bits, so
negative zero and NaN payloads are never changed by a floating-point conversion
or comparison. Negative zero remains pool-backed.

Storage, allocation, emission, and execution tests cover ordinary values,
signed zero, infinities, and a non-canonical NaN payload.

## Slice 8: Add Generic Constant Folding

Introduce one general Core IR constant-folding pass:

```cpp
Result<bool, JitCompilationError>
fold_constants(CompilationSession &, ControlFlowGraph &);
```

Its graph-rewriter callback owns both instruction folding and block-parameter
folding. The folding domain distinguishes tagged identity from native F64
arithmetic:

```cpp
struct TaggedConstant
{
    Value value;
};

struct F64Constant
{
    double value;

    uint64_t bits() const;
};

using ConstantValue = std::variant<TaggedConstant, F64Constant>;
```

Tagged constants compare with `Value::operator==`. F64 constants compare their
`bits()` values, never with floating-point `operator==`, so signed zero and NaN
representations remain distinct. F64 folding itself uses native host `double`
arithmetic. The JIT compiler runs on its target platform; it does not emulate a
separate soft-float model. `ConstF64` bits are converted to `double` on entry to
the folding domain and converted back to exact bits when new IR is emitted.

Initial instruction rules are:

- `UnboxF64(Const(exact Float)) -> ConstF64`;
- `NegF64(ConstF64) -> ConstF64`.

The exact Float rule compares the constant object's shape with the builtin
Float root shape; it does not fold a Float subclass. Negation applies native
unary minus to the stored `double`. Later F64 arithmetic rules extend this same
pass and domain rather than adding operation-specific passes.

Constant block-parameter folding is a read-only forward fixed-point analysis,
not repeated structural mutation. Its parameter lattice is:

```text
Unresolved -> ExactConstant -> NotConstant
```

Registered entry parameters start at `NotConstant`. A self-reference
contributes `Unresolved`, so one constant entry plus an unchanged self-backedge
can establish a constant, while a self-only cycle does not. Parameter facts
flow through other parameters, allowing chains across blocks to converge
without rewriting the graph between analysis steps. Conflicting constants and
any non-constant incoming definition produce `NotConstant`. Tagged `Const` and
`ConstF64` are both recognized. If the graph-scaled fixed-point revisit budget
is exhausted, join folding is conservatively disabled.

The pass has three phases:

1. fold existing instruction expressions with normalized rewrite inputs;
2. analyze constant block-parameter joins to a fixed point;
3. atomically materialize every proven parameter in its destination and fold
   instructions newly exposed by those replacements with normalized inputs.

Destination tagged constants retain and pin the exact candidate `Value`.
Destination F64 constants store the candidate's exact bits. The parameter and
its complete incoming argument column are removed in the same graph-rewriter
transaction. Equal SMIs fold according to CloverVM identity. Two tagged
constants holding the same heap object fold; distinct numerically equal boxed
Floats do not. F64 constants fold only when their bit representations match.

DCE remains separate and later removes dead predecessor constants. This slice
does not add `OptimizationError`: the only iteration is monotone read-only
analysis with an existing conservative fallback, while each graph rewrite is a
single atomic commit.

Tests cover exact tagged identity, distinct equal heap objects, SMI identity,
self-only cycles, a constant plus self-backedge, parameter chains, registered
entry parameters, mixed constants, multiple simultaneous parameter removals,
destination locality verification, tagged Float unboxing, rejection of Float
subclasses, identical and differing F64 joins, normalized instruction chains,
F64 negation, signed zero, and NaN payload preservation.

## Slice 9: Add Atomic Representation Conversion Mechanics

Add a separate rewrite result for changing one join's representation:

```cpp
struct IncomingArgumentReplacement
{
    BlockEdgeId edge;
    ProgramValueRef value;
};

BlockParameterRewrite::convert_representation(
    Instruction replacement_parameter,
    std::span<const IncomingArgumentReplacement> incoming,
    RewriteInsertion destination_materialization);
```

The replacement parameter must be newly allocated through `RewriteContext` and
must have the same result class as the original. Every original incoming edge
must appear exactly once with a representation-compatible value already
available in that edge's source block. The rewrite cannot insert into source
blocks or edge-transfer blocks.

The destination materialization may use the new parameter and must provide the
replacement for remaining uses of the old parameter. All conversions are
planned by original parameter instruction ID and committed in one transaction.

Self-edges receive explicit normalization: a proposed incoming argument may
refer to the new parameter when the edge source is its own destination. The
transformation pass, not the rewriter, remains responsible for proving that
this mapping preserves meaning. The structural rewriter only validates
availability and representation.

Mechanical tests cover one conversion, several conversions in one block,
shifting argument columns, missing and duplicate edge replacements,
representation mismatch, unavailable predecessor values, self-edges,
destination insertion order, and complete CFG verification after commit.

## Slice 10: Implement the Restricted Cross-Edge F64 Rewrite

The first semantic client intentionally handles only joins whose complete
incoming column is already expressed as identity-discardable `BoxF64` results:

```text
BoxF64(x), BoxF64(y), ... -> ParameterF64(x, y, ...)
                              boxed = BoxF64(parameter)
```

It rejects:

- boxed constants, arguments, or other pre-existing Float objects;
- an input requiring a new source-block instruction;
- an unchanged tagged self-reference;
- a box whose identity escaped through a normal use;
- a box also passed through another retained tagged argument position;
- any join whose aliases cannot all be mapped to one destination box without
  changing `is` behavior.

The exact eligibility predicate receives a separate readiness review before
this slice. In particular, snapshot-only uses must preserve one logical box per
taken exit and all aliases within that exit. The pass may use existing use
lists, but it must not infer that immutability makes identity irrelevant.

After conversion, local guard and `UnboxF64(BoxF64(...))` simplification runs,
followed by DCE. Snapshot-only boxing is still handled later by the separate
Core-to-Machine side-exit sinking design.

Tests cover all accepted and rejected identity cases, including two destination
parameters receiving the same incoming box, mixed existing/virtual boxes,
snapshot aliases, conditional edges, and loop backedges. Interpreter-level
tests compare interpreted and compiled `is` outcomes.

This restricted slice is not expected to optimize a loop whose initial edge is
a boxed Float literal. That limitation is accepted for the first
implementation.

## Slice 11: Integrate and Verify the Optimization Pipeline

Place the passes in a canonical direction that cannot recreate their inputs:

1. tagged-value fact propagation and guard simplification;
2. local F64 box/unbox simplification;
3. generic constant folding, including constant joins;
4. restricted F64 join conversion;
5. local simplification again for newly adjacent operations;
6. equivalent-parameter elimination;
7. dead-code elimination.

Any repeated structural group has a fixed maximum round count and reports an
optimization error on exhaustion. Successful conversion must strictly reduce
the selected measure, initially the number of eligible boxed incoming join
arguments, so the pipeline cannot alternate tagged and F64 representations.

Run focused tests after each slice and `ninja -C build-debug all check` after
every C++ change. Before pushing the completed sequence, run
`ninja -C build-release all check` at the exact pushed HEAD. Benchmarking is
informational for the restricted first conversion because boxed-literal loop
entries remain unsupported.

## Deferred Identity Work

The plan does not decide whether CloverVM should later change interpreter-level
identity semantics to make unboxed execution easier. Such a change must update
the interpreter and JIT together and receive its own Python-semantics design.
Possible future mechanisms include lazy boxing, virtual identity tokens, or a
different immutable-value identity policy. None is inferred by this plan.
