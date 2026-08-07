# JIT Control-Flow Graph

| Field | Value |
|---|---|
| Document type | Architecture contract |
| Status | Accepted |
| Implementation | Partial: graph construction and publication, fixed-representation terminators, block parameters and edge arguments, predecessor indexes, structural verification, queries, body-instruction rewriting, topology-preserving parameter/argument compaction, and staged edge splitting are implemented; storage-level enforcement of the single-graph session invariant, cross-block dominance, and other general CFG-topology editing are deferred |
| Scope | Structural CFG shared by Core IR and an optional Semantic IR, including block parameters and first-class block-edge arguments |
| Owning layers | The JIT CFG owns block order, block edges, block parameters, instruction placement, and structural verification; individual IR levels own instruction semantics and side exits |
| Validated against | `tests/test_jit_cfg.cpp` and `tests/test_jit_graph_rewrites.cpp` |
| Supersedes | N/A |

This document describes the implemented structural control-flow representation
shared in shape by Core IR and the optional Semantic IR. It refines the
ordered list-based SSA direction in
[JIT Compiler and IR](jit-compiler-and-ir.md). The permanent instruction
storage and typed-access direction is specified separately in
[JIT Instruction Representation](jit-instruction-representation.md). The
lower-level code fragments created by machine branches and branch targets are
defined separately in
[JIT Machine-Code Emission](jit-machine-code-emission.md); they are not CFG
basic blocks.

The central representation is:

```text
ControlFlowGraph
    entry block
    explicit block order

Block
    ordered instructions, ending in a block terminator
    incoming block edges
    ordered parameter instructions

Block terminator instruction
    semantically named outgoing block edges

Block edge
    source block
    target block
    ordered arguments for the target block parameters
```

Block edges and non-returning side exits are different concepts even when
both eventually lower to machine branches.

`ControlFlowGraph` represents one compiled control-flow region, not one Python
function. Inlining may put instructions originating from several `CodeObject`s
in the same graph while the graph retains one entry block. Logical inline
frames and interpreter recovery state are separate metadata and are not modeled
as nested CFG `Function` objects. The implemented graph is Core IR and
verification rejects kinds not declared Core-legal by `instruction.def`. If the
optional Semantic IR later shares these physical CFG classes, the graph will
need an immutable IR-level discriminator so construction, editing, verification,
and analysis attachments can enforce the appropriate instruction set.

## Block Identity, Order, and Numbering

An allocation serial identifies a block for its lifetime. The graph also owns
an explicit mutable block order. These are deliberately separate:

```text
block serial       stable allocation identity
graph block order  current deterministic block schedule
RPO number         derived analysis result
dominator number   derived analysis result
emission order     backend result, potentially based on graph block order
```

Control flow never depends on adjacency in graph block order. A transfer to the
physically next block is still represented by an explicit unconditional
branch. A backend may omit that machine branch after final layout chooses to
place the target next.

The initial implementation uses a vector for graph block order. A linked or
chunked representation must be justified by measured editing costs before
replacing it.

Instructions use typed, storage-relative `InstructionId`s as persistent
identity. Operand references and block parameter/body vectors store those IDs;
ordinary iteration resolves them into lightweight `Instruction` views that
carry the owning `CompilationStorage` pointer. Compilation output must not
depend on pointer values or unordered-container iteration.

`ControlFlowGraph` and blocks are allocated from stable-address object pools in
`CompilationStorage`. Block edges remain stable-address CFG objects but also
receive compact `BlockEdgeId`s because branch instruction attributes store IDs.
Instructions occupy an append-only `std::vector<InstructionEntry>` and have a
dense `InstructionId` sequence. The `CompilationSession` owns the storage and
necessarily outlives every graph object and instruction view. A graph borrows
its owning storage pointer so compiler passes can resolve compilation-relative
identities without separately threading storage through every graph API.

A `GraphBuilder` takes the session, borrows its storage, allocates one
unpublished graph from it, and owns all initial mutation of that graph.
`finalize()` verifies and publishes the graph, returns its stable storage-owned
pointer, and makes that builder unusable. Destroying a builder without
finalizing is valid; session teardown destroys the storage and reclaims the
abandoned graph. A compilation session owns at most one graph for its
instruction-allocation domain. The storage allocator must reject a second graph
allocation in that session; until that check is implemented, callers uphold the
single-graph invariant. This makes cross-graph instruction placement
unrepresentable without adding a storage-wide placement index.

Each block carries a nonnegative loop-depth annotation. It defaults to zero and
may be set through `GraphBuilder::set_loop_depth()` while the graph is
unpublished. Frontends copy the source CFG's loop nesting depth into this
annotation; downstream heuristics such as register-allocation spill weighting
consume it without rediscovering loops. Rewriting preserves the annotation
because it preserves the block object.

## Blocks and Terminators

The block terminator is the final ordinary instruction in the block's ordered
instruction list. It is not stored beside the list and is not a separately
synchronized object.

The implemented accessor is equivalent to:

```cpp
TerminatorInstruction Block::terminator() const
{
    assert(!instruction_ids_.empty());
    Instruction instruction =
        storage()->instruction(instruction_ids_.back());
    assert(instruction.is_block_terminator());
    return TerminatorInstruction(instruction);
}
```

This gives the terminator the same instruction identity, bytecode origin,
placement, operand behavior, and traversal rules as every other instruction.
A linear walk of a block sees all executable instructions, including its
terminator. A CFG walk obtains the same instruction through the block accessor.

A valid completed block satisfies:

- its instruction list is nonempty;
- its final instruction is a block terminator;
- no earlier instruction is a block terminator.

Construction does not require a separate open or sealed state. A builder may
temporarily hold a block with no terminator, but code must not request its
terminator or block successors. The graph reaches its valid form before it is
passed to analyses or optimization passes. The decoded bytecode CFG remains the
earlier scaffold for analyses needed before IR block construction completes.

`is_block_terminator()` is narrower than "may change control flow." A guard can
have a non-returning deoptimization side exit while successful execution
continues to the next instruction in the same block. Such a guard is not a
block terminator.

The implemented fixed-size instruction representation provides both:

- a generic ordered block-successor interface for CFG algorithms;
- checked semantic accessors such as `edge()`, `true_edge()`, and
  `false_edge()` for clients that understand the terminator kind.

Typed terminator views interpret the schema-defined fixed instruction payload
without virtual dispatch or C++ RTTI. See
[JIT Instruction Representation](jit-instruction-representation.md).

## First-Class Block Edges

A block edge represents one occurrence of control transfer between blocks in
the same CFG, not merely a source/target block pair. The implemented object
contains:

```text
BlockEdge:
    BlockEdgeId
    source block
    target block
    ordered ProgramValueRef arguments
```

First-class occurrences are required because two semantic arms may have the
same source and target while carrying different arguments. Register allocation
also needs a place to associate one parallel move bundle with one transfer, and
critical-edge splitting operates on an individual transfer rather than an
unordered pair of blocks.

An edge does not carry a universal semantic kind such as `True`, `False`, or
`SwitchCase`. Those meanings belong to the source terminator:

```text
UnconditionalBranch:
    destination edge

ConditionalBranch:
    condition
    true edge
    false edge

Switch:
    (case value, edge)...
    default edge
```

This avoids a growing edge-kind taxonomy and keeps meaning in the operation
that owns it. The true and false edges must be distinct edge objects even when
they target the same block.

The terminator instruction is authoritative for the source block's outgoing
edges. The source block does not maintain a second successor vector. Its generic
CFG accessor delegates to the terminator:

```cpp
TerminatorInstruction::BlockSuccessorEdges
Block::block_successor_edges() const
{
    return terminator().block_successor_edges();
}
```

The target block maintains the reverse index of incoming block edges so that
predecessor traversal does not require a whole-graph scan:

```text
source terminator -> outgoing block edge references
block edge        -> source, target, and arguments
target block      -> incoming block edge references
```

Graph mutation must keep these views consistent. A terminator and the edge
occurrences it names are immutable. Rewriting a definition used by an edge
argument creates a replacement edge with normalized arguments and reconstructs
the owning terminator; it does not mutate the old edge. The rewrite transaction
then rebuilds predecessor indexes at commit.

`GraphRewriter::stage_edge_splits()` is the initial topology-editing operation.
It creates a pass-through block with one representation-matched parameter per
original edge argument and an outgoing edge to the original target. The caller
selects `AfterSource` or `BeforeTarget` placement explicitly. Staged blocks
participate in the same query-free instruction rewrite and become visible only
when that transaction commits; this lets block-entry insertions populate an
edge-transfer block without publishing an intermediate CFG. The initial
operation does not combine edge splitting with block-parameter compaction.
Other redirection and adding or removing outgoing edges remain unimplemented.

`GraphBuilder::make_block_edge()` checks graph membership in debug builds and
allocates a source-owned edge without attaching it to the target. This matches
translation order: `emplace_n_blocks()` first creates the complete program-order
block vector, `block_at()` retrieves each block by that dense order while the
translator builds instructions and outgoing edges, and then incoming edges are
derived. During
`finalize()`, the builder walks source blocks again in program order and appends
each terminator edge to its target's predecessor vector before verification.
Predecessor order is therefore deterministic construction order but has no
semantic significance; the edge itself is the durable identity.

## Block Parameters and Edge Arguments

Ordered block-parameter instructions are stored separately from the block body.
They are definitions available at block entry and use the same instruction
identity and typed `ProgramValueRef` mechanism as ordinary defs. Each immutable
`BlockEdge` stores its corresponding ordered `ProgramValueRef` argument uses.

Block parameters are ordered SSA definitions owned by the target block. Edge
arguments are ordered SSA uses at the source edge. For:

```text
A:
    ...
    UnconditionalBranch edge0(%v0, %v1)

B(%p0, %p1):
    ...
```

where `edge0` transfers from `A` to `B`, the binding is:

```text
edge0.argument[0] = %v0  ->  B.parameter[0] = %p0
edge0.argument[1] = %v1  ->  B.parameter[1] = %p1
```

For frame-state-carrying bytecode blocks, the bytecode state tracker owns the
positional contract shared by target block parameters and every incoming edge.
That contract is internal to block transfer. Snapshot and recovery construction
query the same semantic state but own their separate physical layout and
structural descriptors; block-edge arguments do not contain frame-header holes.

An edge argument need not be defined in the immediate source block. Its
definition must be available at the source terminator and therefore dominate
the edge. The target parameter is a new SSA definition available in the target
block.

Core IR defines a distinct block-parameter instruction kind for each
`ValueRepresentation`, initially the tagged `Parameter` and
`ParameterF64`. Every corresponding edge argument must have the same
intrinsic representation. Semantic IR parameters have no Core representation.
A Core representation mismatch is repaired by an explicit conversion in the
predecessor or an edge block, never by register-allocation coercion.

The complete edge transfer has parallel-copy semantics. Argument zero is not
assigned before argument one, and one transfer may safely encode swaps or
cycles. The backend resolves those copies only after locations are assigned.

Every incoming block edge supplies exactly one compatible argument for every
target parameter. Adding or removing a target parameter therefore requires a
coordinated update of all incoming edge argument lists.

## Block Edges and Side Exits

Block-to-block CFG transfer and non-returning side exit behavior are orthogonal
to whether an instruction terminates its block:

| Operation shape | Continues to next instruction | Block successors | Side exit | Block terminator |
|---|---:|---:|---:|---:|
| Guard with deoptimization | Yes | 0 | 1 | No |
| Conditional branch | No | 2 | 0 | Yes |
| Unconditional branch | No | 1 | 0 | Yes |
| Return or bare return | No | 0 | 0 | Yes |
| Unconditional deoptimization | No | 0 | 1 | Yes |

Block edges participate in block arguments, RPO, dominance, loops, and backend
edge moves. A non-returning side exit instead belongs to the exact instruction
position that may take it and carries recovery state such as a Snapshot. It is
not included in `block_successor_edges()` and does not become a predecessor of
an IR block.

Snapshot operands and other recovery data remain point uses for liveness even
though their side exit is absent from the block CFG. A backend may lower a
guard side exit and a conditional block branch with the same target branch
primitive; that target-level commonality does not merge their IR semantics.
When a Snapshot captures a result marked sunk, allocation liveness reaches
through its recovery-only dependency closure to the non-sunk physical frontier;
the sunk instructions still do not become CFG blocks or edges.
It may also split one compiler basic block into several machine-code fragments
at side-exit branches without introducing CFG blocks or edges.

## Verification

CFG verification is a normal compiler operation, not only a debugging aid. It
should run after initial construction and after every CFG-mutating pass in
debug and test configurations. Focused rewrite and editor tests should verify
after each completed mutation. Verification before backend lowering protects
later stages that assume a complete graph.

The implemented structural verifier returns `CfgVerificationResult`, containing
a `valid` flag and one diagnostic string. It does not depend on dominance, loop
structure, or other cached analyses. It checks:

- every graph block appears exactly once in graph block order;
- the entry block exists and is the first block in graph order;
- every block is nonempty and has exactly one final block terminator;
- instructions are non-null and occur in only one position within the graph;
- every operand definition is a parameter of the same block or an earlier
  instruction in that block;
- every terminator has the required semantic block edges for its kind;
- every outgoing edge names the containing block as its source;
- every edge target belongs to the same graph;
- every outgoing edge occurs exactly once in its target's predecessor index;
- every predecessor edge is referenced exactly once by its source terminator;
- the true and false arms of a conditional branch use distinct edge objects.
- every edge argument list has the same arity as its target parameter list;
- every edge argument has the target parameter's value representation;
- every edge argument definition is available in its immediate source block.

The result type preserves a useful diagnostic boundary; it does not make an
invalid graph a recoverable compilation outcome. Builder finalization and
pass-boundary verification diagnose and hard-assert on failure because a
malformed graph is a compiler logic error. Allocation and other resource
failures follow the separate whole-compilation fallback path.

The verifier inspects the raw instruction list rather than calling the checked
`Block::terminator()` accessor, allowing it to diagnose an empty block or a
malformed final instruction directly.

The verifier is scoped to one `ControlFlowGraph`; it does not maintain a
storage-wide placement index merely to diagnose an instruction placed in two
graphs.

Later cross-block SSA verification will replace the current immediate-source
restriction with dominance and add these checks:

- every ordinary operand definition dominates its use;
- every edge argument definition dominates its source edge;
- every block parameter dominates ordinary uses in its block and dominated
  blocks;
- every Snapshot operand is available at each consuming side exit;
- instruction results, parameter types, and edge argument types satisfy their
  operation contracts.

Future block-argument diagnostics should name stable serials and semantic
source roles, for example:

```text
block edge e7:
    source: b3 ConditionalBranch.true
    target: b8
    arguments: (%v12, %v19)
    target parameters: (%p4, %p5)
```

## Alternatives Not Chosen

### Block-only predecessor and successor pointers

Plain block adjacency is insufficient for block arguments. It cannot distinguish
two transfers with the same source and target, gives no natural home to the
argument vector or backend move bundle, and makes semantic roles depend on
parallel-vector conventions.

### Inline successor records without edge identity

A terminator can store `{target, arguments}` records directly. The target then
needs a reverse reference such as `(source block, successor slot)`. That pair is
an edge identity in another form. A first-class block edge makes this occurrence
explicit and gives graph mutation and backend tables a stable key.

### Semantic kind stored on every edge

An `EdgeKind::True` or `EdgeKind::False` duplicates knowledge already owned by
the conditional branch and does not generalize cleanly to switch cases or
future terminator forms. Semantic roles remain named positions in the source
terminator.

### Terminator stored beside the instruction list

A separate block terminator slot requires synchronization with instruction
order and prevents one linear instruction walk from seeing the complete block.
The final instruction is the sole authoritative terminator.

## Implemented Surface and Deferred Work

The current implementation establishes:

- `ControlFlowGraph` owns explicit block order and treats the first added block
  as its entry, publication state, mutation generation, and query caches;
- `Block` stores ordered parameter definitions, an ordered body-instruction
  list, and an ordered predecessor-edge index;
- `BlockEdge` stores a typed serial, source block, target block, and ordered
  `ProgramValueRef` arguments;
- `CompilationSession` owns the `CompilationStorage`, which allocates blocks,
  block edges, and instructions from their designated pools;
- schema-generated concrete instruction views provide typed branch conditions,
  return operands, block parameters, and terminator edges;
- `GraphBuilder` supports make/append/emplace construction, derives predecessor
  indexes, verifies, and publishes one graph;
- the structural verifier checks Core-kind legality, unique placement,
  same-block definition-before-use, result classes, value representations,
  terminators, edge arguments, and edge/index consistency;
- `GraphRewriter` stages body-instruction rewrites and edge-split blocks and
  commits one graph generation at a time, reconstructing immutable edges and
  their owning terminators when argument definitions are normalized;
- use lists record edge arguments as `BlockArgumentUse` occurrences separately
  from ordinary instruction operand uses.

Cross-block SSA dominance and CFG-topology editing remain deferred.

## Open Decisions After Initial Implementation

- whether measured editing pressure justifies replacing the vectors for block
  order, instruction order, parameters, or incoming edges;
- the topology-editor API for redirecting edges and rebuilding predecessor
  indexes;
- how unreachable but still live blocks participate in verification and
  cleanup;
- the dominance-analysis boundary for cross-block SSA verification.

## Related Documents

- [JIT Instruction Representation](jit-instruction-representation.md)
- [JIT Compiler and IR](jit-compiler-and-ir.md)
- [Semantic IR and Specialization](jit-semantic-ir-and-specialization.md)
- [Decision Log](decision-log.md)
