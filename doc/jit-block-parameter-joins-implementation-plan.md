# JIT Block-Parameter Joins Implementation Plan

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Accepted |
| Implementation | Partial: slices 1 through 3 are implemented; slices 4 through 9 are not started |
| Scope | Staged implementation of block-entry metadata, block traversal, fixed-point scheduling, block-parameter joins, destination-only join rewriting, constant join folding, and restricted cross-edge F64 conversion |
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
    const BlockEdge &edge;
    ProgramValueRef value;
};

class BlockParameterJoin
{
public:
    const Block &block() const;
    Instruction parameter() const;
    IncomingArgumentRange incoming_arguments() const;
    ProgramValueRef argument_from(const BlockEdge &) const;
};
```

The parameter instruction ID identifies the join. The CFG privately resolves
the parameter's argument-column index; passes do not retain or mutate that
index. The view records the graph generation and rejects use after structural
mutation.

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
self-edge joins, two distinct edges with one source and target, and stale-view
rejection.

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

## Slice 6: Add Destination Materialization and Constant Join Folding

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

Implement constant join folding on top:

- ignore incoming references to the parameter itself;
- require at least one non-self incoming value;
- require every non-self input to be a `Const` whose tagged value is
  equal to the candidate under `Value::operator==`;
- create a destination `Const` retaining and pinning that exact `Value`;
- remove the parameter and all corresponding edge arguments.

Equal SMIs fold according to CloverVM identity. Distinct equal boxed Floats do
not fold. A loop with one identical constant entry and an unchanged parameter
backedge folds to a destination-local constant.

Run this structural simplification to a fixed maximum number of rounds. Each
successful round must remove at least one parameter. Reaching the limit returns
an optimization error through `JitCompilationError`; it never accepts a
partially iterated result as final. Add an `OptimizationError` alternative to
`JitCompilationError`, initially containing `FixedPointLimitReached`, rather
than overloading register-allocation or code-cache failures. DCE runs after the
fold to remove dead predecessor constants.

Tests cover exact tagged identity, distinct equal heap objects, SMI identity,
self-only cycles, a constant plus self-backedge, mixed constants, multiple
simultaneous parameter removals, and destination locality verification.

## Slice 7: Add Atomic Representation Conversion Mechanics

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

## Slice 8: Implement the Restricted Cross-Edge F64 Rewrite

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

## Slice 9: Integrate and Verify the Optimization Pipeline

Place the passes in a canonical direction that cannot recreate their inputs:

1. tagged-value fact propagation and guard simplification;
2. local F64 box/unbox simplification;
3. constant join folding;
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
