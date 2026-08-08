# JIT Block-Parameter Joins and Dataflow Traversal

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Partial: entry metadata, block ordering, fixed-point scheduling, and read-only block-parameter joins are implemented; atomic join conversion is not started |
| Scope | A shared view of block parameters and their incoming edge arguments, reusable block traversal and fixed-point scheduling, and atomic block-parameter rewrites |
| Owning layers | The CFG owns entry metadata and join structure; traversal owns ordering and scheduling; analyses own lattices and transfer functions; transformation passes own legality; the graph rewriter owns atomic structural mutation |
| Validated against | N/A |
| Supersedes | N/A |

Block parameters and the argument at the same position on every incoming edge
form one semantic join. Analyses already inspect this relationship to propagate
tagged-value facts and liveness. Transformations inspect it to eliminate
equivalent parameters. Cross-edge representation changes need to modify the
whole relationship atomically.

The common abstraction is therefore the join, rather than a special case in
each analysis or a general-purpose edge-rewriting language.

## Block-Parameter Joins

`BlockParameterJoin` is a read-only CFG view of one block parameter and its
incoming values:

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

The parameter instruction ID is the relationship's structural identity. The
argument-column index is a private implementation detail resolved from that ID
when constructing or committing the view. Both range queries are lazy standard
transform views and allocate no intermediate storage. A join is valid only for
the immediate traversal that produced it. The joins and ranges are ephemeral
borrowed views and must not be retained across structural mutation.

The read view belongs to the CFG, not to `GraphRewriter`. Read-only analyses
and transformations both consume it.

Existing code that consumes this relationship includes:

- tagged-value fact analysis, which merges incoming facts into a parameter;
- dead-code elimination, which propagates liveness from a parameter to its
  incoming arguments;
- equivalent-parameter elimination, which compares incoming argument columns.

Immediate planned clients include constant join folding and cross-edge
boxed-value elimination. Future clients may include other changes between
tagged and unboxed representations.

## Join Folding by Destination Materialization

CloverVM block arguments are not unrestricted SSA phi operands. An ordinary
definition is local to its block, so a definition from one predecessor cannot
replace a parameter in the destination merely because every incoming argument
appears to name the same value. The replacement must itself be defined in the
destination block.

Constants provide the initial sound case when every incoming instruction
contains the same tagged value under `Value::operator==`, not merely a
Python-equal value:

```text
predecessor A -- Const(0.5) --+
                                +--> destination(%parameter)
predecessor B -- Const(0.5) --+
```

The pass materializes a `Const` containing that same tagged value in the
destination, replaces every use of `%parameter` with that destination-local
definition, and removes the parameter and its argument position from every
incoming edge. For a boxed Float, the new `Const` refers to the exact same
object; it does not allocate an equal replacement Float. The predecessor
constants then become ordinary dead-code-elimination candidates.

A loop self-reference can be ignored only after the remaining incoming values
establish a destination-materializable replacement:

```text
entry ---- Const(0.5) --+
                          +--> loop(%parameter)
backedge -- %parameter --+
```

This folds by inserting `Const(0.5)` in the loop block, not by making the
entry-block constant directly visible there.

The same mechanism could later support another explicitly rematerializable
operation, but equality of incoming definitions alone is insufficient. A
separate already-implemented case may coalesce two destination parameters when
their complete incoming argument columns are equivalent; its replacement is a
retained parameter defined in the same destination block.

## Atomic Join Conversion

A representation conversion is one logical rewrite even though it changes
several pieces of the CFG. The pass describes the conversion and proves its
semantic legality; `GraphRewriter` validates and commits all structural edits
together.

Cross-edge F64 boxing is the first client. Given boxed values entering a tagged
parameter, the rewrite:

1. replaces the tagged destination parameter with an F64 parameter;
2. supplies one F64 value on every incoming edge;
3. inserts one `BoxF64` at the start of the destination; and
4. redirects the old tagged parameter's remaining uses to that box.

```text
source A: BoxF64(x) --+
                       +--> destination(tagged p)
source B: BoxF64(y) --+
```

becomes:

```text
source A: x --+
              +--> destination(f64 p):
source B: y --+        boxed_p = BoxF64(p)
                       ... tagged uses use boxed_p
```

The join rewrite may only reuse definitions already available in each source
block. It never inserts instructions into a source block or an edge-transfer
block. Consequently, an incoming `BoxF64(x)` can supply `x`, but an incoming
boxed Float `Const` cannot be converted unless an appropriate F64 definition
already exists in that source. The initial optimization deliberately leaves
such joins unchanged.

Replacing incoming boxes with a destination box is legal only when the
incoming boxes represent identity-discardable allocations. An existing boxed
Float from a literal, argument, or other external source must retain its tagged
identity. Snapshot uses normally keep that original tagged value alive; an F64
parameter alone cannot recover whether a loop iteration carried an existing
object or a safely rematerializable arithmetic result. The initial conversion
therefore does not attempt mixed existing-object and `BoxF64` joins.

Every incoming edge must be accounted for. The conversion is rejected rather
than partially committed if a new argument has the wrong representation, a
definition is unavailable at its placement, the destination materialization
cannot replace all old uses, or any other structural invariant fails. It does
not insert instructions in source blocks, redirect edges, change CFG topology,
modify unrelated arguments, or rewrite unrelated instructions.

Conversion requests identify the original parameter by instruction ID. The
rewriter privately resolves and reconstructs its complete incoming argument
column, so converting or removing several parameters cannot expose shifting
indexes to a pass. A self-edge remains a special staging case: the transaction
must explicitly define how an old parameter reference maps to its replacement
parameter while rebuilding that parameter's own backedge.

The rewrite outcomes should remain distinct even if they share commit
machinery:

```cpp
BlockParameterRewrite::keep();
BlockParameterRewrite::replace_with_destination_parameter(parameter);
BlockParameterRewrite::materialize_in_destination(...);
BlockParameterRewrite::convert_representation(...);
```

The exact C++ builder surface remains to be fixed by the implementation
readiness review. These names describe separate legality contracts, not one
unrestricted mutation object.

## Block Traversal

Traversal direction and convergence are separate policies. The initial block
traversals are:

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

Their initial order and change dependencies are:

| Traversal | Initial visit order | Dependants after a change |
|---|---|---|
| Program order | Every block in stored graph order | None |
| Forward | Multi-root reverse postorder, then any remaining blocks | CFG successors |
| Backward | Reverse of the complete forward order | CFG predecessors |

A one-pass walk visits every block exactly once:

```cpp
for(const Block *block: ordered_blocks(graph, order))
{
    visit(*block);
}
```

A monotone analysis first performs that complete visit, then revisits dependent
blocks until its facts stop changing:

```cpp
enum class DataflowUpdate : uint8_t
{
    Unchanged,
    Changed,
};

iterate_blocks_to_fixed_point(
    graph, order, maximum_total_revisits,
    [&](const Block &block) -> DataflowUpdate {
        // Changed means information visible to dependants changed.
    });
```

During the initial visit, a changed block queues only dependants that have
already been visited. An unvisited dependant needs no queue entry because its
guaranteed initial visit will observe all changes accumulated so far. After
every block has been visited once, the driver drains a FIFO revisit queue.
Fixed-point iteration accepts forward or backward order; program order has no
dependency direction and is rejected.
Scheduling a block already present in the queue is a no-op; scheduling a block
after it has been removed may enqueue it again. Revisits occur in deterministic
queuing order and have no separate priority scheme.

The initial sweep is not included in the revisit count. The revisit count is
one global total rather than one counter per block. Each analysis supplies a
generous graph-scaled maximum, normally representing some number of effective
additional sweeps. Reaching it with work still queued fails the analysis and
causes ordinary whole-compilation fallback; the driver never returns partial
facts. The scheduler otherwise does not own the analysis domain. The analysis
owns its initial facts, lattice, merge operation, transfer function, dependency
interpretation, and proof of termination. In particular, fixed-point
termination requires monotone movement through a finite-height lattice or an
explicit widening policy.

Tagged-value propagation is a forward fixed-point analysis. Instructions
derive facts from their operands, and each `BlockParameterJoin` merges the
facts supplied by incoming edges. When a block's externally visible facts
change, its successors are reconsidered.

A conventional live-in/live-out analysis is a backward fixed-point analysis.
Current dead-code elimination is different: it roots liveness at every
effectful instruction and follows definition dependencies backwards. An
infinite loop containing effects must remain live even though it has no exit.
DCE may reuse the generic deduplicating worklist and the join view, but it must
not be forced into an exit-rooted traversal.

## Entry Blocks

Forward traversal must include exception-handler entries. They are external
CFG roots even though normal block edges do not reach them. Their parameters
must start with conservative externally supplied facts just like parameters of
the normal entry.

The CFG currently exposes only `entry_block()`. During bytecode lowering, the
lowerer must register the normal entry and every exception-handler entry with
the CFG. A common query then exposes that information:

```cpp
Block *normal_entry_block() const;
std::span<Block *const> exception_entry_blocks() const;
std::span<Block *const> entry_blocks() const;
```

`exception_entry_blocks()` contains only registered exception-handler entries.
`entry_blocks()` contains the normal entry followed by those exception entries.
Entry status should not be inferred merely from an empty predecessor list: it
is semantic information, and a disconnected block is not necessarily an
external entry. Forward traversal uses all registered entries as roots when
constructing its order, then appends any blocks not reached from those roots so
that the initial visit still covers the complete graph.

## Structural Rewrite Fixed Points

Repeated graph simplification is distinct from a monotone dataflow analysis:

```cpp
do
{
    RewriteSummary summary = fold_block_parameter_joins(graph);
}
while(summary.changed());
```

A committed rewrite invalidates ephemeral join views and advances the graph
generation used by cached analyses. Each round therefore obtains fresh views
and queries.
The structural pass owns a termination argument and a fixed maximum number of
rounds. Reaching that maximum causes compilation fallback rather than accepting
a partially simplified graph. The dataflow fixed-point driver must not be
reused merely because both operations repeat until there is no change.

## Deliberate Boundaries

This design does not introduce a general dataflow framework or a declarative
edge-pattern language. It centralizes only:

- the CFG concept of a block-parameter join;
- common one-pass block traversal;
- deduplicated forward and backward fixed-point scheduling; and
- atomic maintenance of parameter and edge-argument structure.

Passes continue to own semantic matching and legality. Guards, allocations,
snapshots, and potentially failing operations cannot be moved through joins
merely because they have the same syntactic form on every incoming edge.
Snapshot-only box sinking into side exits is also separate from block-parameter
conversion: it identifies rematerializable Core IR cones, lowers them into
Machine IR side exits, and relies on Machine IR dead-code elimination to remove
the hot-path originals.
