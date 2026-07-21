# JIT Control-Flow Graph

| Field | Value |
|---|---|
| Document type | Architecture contract |
| Status | Accepted |
| Implementation | Partial: the structural CFG and temporary terminators are implemented; block parameters and edge arguments are deferred |
| Scope | Structural CFG shared by Core IR and an optional Semantic IR, plus the planned block-argument extension |
| Owning layers | The JIT CFG owns block order, block edges, block parameters, instruction placement, and structural verification; individual IR levels own instruction semantics and side exits |
| Validated against | Working tree on 2026-07-20; focused JIT tests and the full debug `all check` suite |
| Supersedes | N/A |

This document describes the implemented structural control-flow representation
shared in shape by Core IR and the optional Semantic IR. It also specifies the
agreed block-parameter extension, which is not implemented yet. It refines the
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
    ordered parameters [planned]

Block terminator instruction
    semantically named outgoing block edges

Block edge
    source block
    target block
    ordered arguments for the target block parameters [planned]
```

Block edges and non-returning side exits are different concepts even when
both eventually lower to machine branches.

`ControlFlowGraph` represents one compiled control-flow region, not one Python
function. Inlining may put instructions originating from several `CodeObject`s
in the same graph while the graph retains one entry block. Logical inline
frames and interpreter recovery state are separate metadata and are not modeled
as nested CFG `Function` objects. Every graph records one immutable IR level so
construction, editing, verification, and concrete analysis attachments can
enforce the instruction kinds legal at that level. The unprefixed CFG storage
types remain shared in `cl::jit`; the level does not require duplicating the
physical CFG classes.

## Block Identity, Order, and Numbering

An arena allocation serial identifies a block for its lifetime. The graph also
owns an explicit mutable block order. These are deliberately separate:

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

The accepted instruction representation uses stable pointers for semantic
references and zero-overhead typed pointer views for instruction results. The
pointed-to objects also carry typed, stable arena serials. Compilation output
must not depend on pointer values or unordered container iteration; serials
provide diagnostics and deterministic tie-breaking rather than another
reference mechanism.

`ControlFlowGraph` borrows its `CompilationArena`; the arena must outlive the
graph and every use of its blocks, edges, and instructions. Blocks, block edges,
and instructions are allocated in separate arena pools, so each object kind has
its own serial sequence. Multiple temporary instruction subclasses share the
one `Instruction` pool and serial sequence until the fixed-size representation
replaces them.

A compilation session owns at most one `ControlFlowGraph` for its arena-backed
instruction allocation domain. The CFG allocator must assert or fail if asked to
allocate a second graph in the same session. That session-level invariant makes
placing one `Instruction *` in two graphs unrepresentable in current
compilation, so the per-graph verifier does not need to discover cross-graph
instruction placement.

## Blocks and Terminators

The block terminator is the final ordinary instruction in the block's ordered
instruction list. It is not stored beside the list and is not a separately
synchronized object.

The implemented accessor is equivalent to:

```cpp
TerminatorInstruction *Block::terminator() const
{
    assert(!instructions_.empty());
    Instruction *instruction = instructions_.back();
    assert(instruction != nullptr);
    assert(instruction->is_block_terminator());
    return static_cast<TerminatorInstruction *>(instruction);
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

The bring-up implementation uses this temporary hierarchy:

```text
Instruction
    TerminatorInstruction
        ConditionalBranchInstruction
        UnconditionalBranchInstruction
        ReturnInstruction
```

`Instruction` has a virtual destructor because its arena pool owns subclasses
through base pointers. A private construction tag records whether an instruction
is a block terminator; only `TerminatorInstruction` can set it. The terminator
also carries a `TerminatorKind` and its ordered block-successor edges. The
concrete branch classes provide the semantic edge accessors.

This hierarchy is scaffolding, not a commitment to permanent virtual
instructions. The accepted fixed-size, type-erased instruction representation
will replace it and must continue to provide both:

- a generic ordered block-successor interface for CFG algorithms;
- checked semantic accessors such as `edge()`, `true_edge()`, and
  `false_edge()` for clients that understand the terminator kind.

The typed terminator views and their underlying fixed instruction payload will
implement this contract without virtual dispatch or C++ RTTI. See
[JIT Instruction Representation](jit-instruction-representation.md).

## First-Class Block Edges

A block edge represents one occurrence of control transfer between blocks in
the same CFG, not merely a source/target block pair. The implemented object
contains:

```text
BlockEdge:
    serial
    source block
    target block
```

The block-argument extension will add an ordered argument list to this same
edge object.

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
const TerminatorInstruction::BlockSuccessorEdges &
Block::block_successor_edges() const
{
    return terminator()->block_successor_edges();
}
```

The target block maintains the reverse index of incoming block edges so that
predecessor traversal does not require a whole-graph scan:

```text
source terminator -> outgoing block edge references
block edge        -> source, target, and arguments [arguments planned]
target block      -> incoming block edge references
```

Graph mutation must keep these views consistent. Redirecting an edge updates
its target and the old and new target predecessor indexes while preserving its
semantic role in the source terminator. Replacing a terminator may introduce or
remove outgoing edges. Neither mutation is implemented yet: `BlockEdge` source
and target are currently immutable, and there is no CFG editor.

`ControlFlowGraph::make_block_edge()` is the supported construction path. It
checks graph membership in debug builds, allocates the edge, and adds it to the
target's predecessor index. `CompilationArena::make_block_edge()` performs raw
allocation only and does not maintain graph indexes; its current public
visibility is under review before production builders are added.

## Planned Block Parameters and Edge Arguments

Block parameters and edge arguments in this section are accepted design but
are not part of the current C++ representation or structural verifier.

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
| Return | No | 0 | 0 | Yes |
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
It may also split one compiler basic block into several machine-code fragments
at side-exit branches without introducing CFG blocks or edges.

## Verification

CFG verification is a normal compiler operation, not only a debugging aid. It
should run after initial construction and after every CFG-mutating pass in
debug and test configurations. Focused editor tests should verify after each
completed mutation. Verification before backend lowering protects consumers
that assume a complete graph.

The implemented structural verifier returns `CfgVerificationResult`, containing
a `valid` flag and one diagnostic string. It does not depend on dominance, loop
structure, or other cached analyses. It checks:

- every graph block appears exactly once in graph block order;
- the entry block exists and is the first block in graph order;
- every block is nonempty and has exactly one final block terminator;
- instructions are non-null and occur in only one position within the graph;
- every terminator has the required semantic block edges for its kind;
- every outgoing edge names the containing block as its source;
- every edge target belongs to the same graph;
- every outgoing edge occurs exactly once in its target's predecessor index;
- every predecessor edge is referenced exactly once by its source terminator;
- the true and false arms of a conditional branch use distinct edge objects.

The result type preserves a useful diagnostic boundary; it does not make an
invalid graph a recoverable compilation outcome. Builder finalization and
pass-boundary verification diagnose and hard-assert on failure because a
malformed graph is a compiler logic error. Allocation and other resource
failures follow the separate whole-compilation fallback path.

The verifier inspects the raw instruction list rather than calling the checked
`Block::terminator()` accessor, allowing it to diagnose an empty block or a
malformed final instruction directly.

The verifier is scoped to one `ControlFlowGraph`. Cross-graph instruction
placement is excluded by the compilation-session rule that one arena-backed
instruction allocation domain owns at most one CFG.

The block-argument extension and later SSA verification will add these checks:

- every placed instruction is live and its kind permits the graph's immutable
  IR level according to `src/jit/instruction.def`;
- every instruction and result reference stays within one graph and IR level;
- every edge argument list matches the target parameter list in arity and
  required `OperandClass`; Core arguments additionally match the parameter's
  `ValueRepresentation`, plus any later phase-specific contract;
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

The current implementation establishes the structural graph needed for CFG
construction and verification:

- `ControlFlowGraph` owns explicit block order and treats the first added block
  as its entry;
- `Block` stores an ordered instruction list and an ordered predecessor-edge
  index;
- `BlockEdge` stores a typed serial, source block, and target block;
- `CompilationArena` allocates blocks, block edges, and instructions from
  their designated pools;
- the temporary polymorphic instruction representation contains
  `TerminatorInstruction`, `ConditionalBranchInstruction`,
  `UnconditionalBranchInstruction`, and `ReturnInstruction`;
- the structural verifier reports a small success/failure result with one
  diagnostic string.

Conditions, return operands, block parameters, edge arguments, side exits,
graph editing, and SSA verification are intentionally absent from the current
implementation. The absence of block arguments is an implementation staging
choice, not a change to the accepted block-argument design above.

## Open Decisions After Initial Implementation

- whether editing pressure justifies replacing the initial vectors for block
  order, instruction order, or incoming edges;
- the concrete graph-builder and mutation-editor API shapes;
- the physical representation of block parameters within the instruction
  identity scheme;
- how unreachable but still live blocks participate in verification and
  cleanup;
- the boundary between structural CFG verification and broader SSA or
  operation verification.

## Related Documents

- [JIT Instruction Representation](jit-instruction-representation.md)
- [JIT Compiler and IR](jit-compiler-and-ir.md)
- [Semantic IR and Specialization](jit-semantic-ir-and-specialization.md)
- [JIT Compiler Bring-up Plan](jit-compiler-bring-up-plan.md)
- [Decision Log](decision-log.md)
