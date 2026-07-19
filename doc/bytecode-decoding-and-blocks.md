# Bytecode Decoding and Block Structure

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | Implemented |
| Scope | Shared bytecode schema, instruction decoding, lightweight bytecode blocks, and compilation-time inline-cache snapshots |
| Owning layers | The bytecode layer owns encoded-format metadata, decoding, and bytecode block discovery; each JIT owns its IR CFG, architectural-state flow, SSA construction, and later analyses |
| Validated against | N/A |
| Supersedes | N/A |

Clover bytecode has one producer, `CodeObjectBuilder`, and several consumers:

- the performance-critical interpreter;
- the bytecode printer and instruction tracer;
- two JIT compilation paths.

The interpreter should retain direct access to encoded bytes and its specialized
dispatch shape. The other consumers need a shared description of instruction
lengths and a structured semantic view of operands, accumulator effects,
control flow, and inline-cache feedback. The JITs additionally need a
lightweight basic-block structure before they begin constructing their own IR.

This design introduces three public concepts:

```text
BytecodeInstruction
BytecodeBlock
BytecodeDecoder
```

`BytecodeInstruction` is the semantic decoding of one instruction.
`BytecodeBlock` is a range in the encoded bytecode with bytecode-level control
flow. `BytecodeDecoder` owns compilation-scoped feedback snapshots and the list
of bytecode blocks.

The authoritative physical opcode schema, standalone semantic decoding,
printer and tracer migration, compilation-scoped feedback snapshots, and
bytecode block discovery are implemented.

## Ownership and Pipeline

The shared bytecode layer provides this pipeline:

```text
CodeObject::code
    -> opcode-format metadata
    -> structural scan
    -> BytecodeBlock list in ascending PC-offset order
    -> block-local BytecodeInstruction iteration
```

Each JIT then owns a separate pipeline:

```text
BytecodeBlock entry
    -> one or more JIT IR blocks
    -> complete architectural-state edges
    -> snapshots and SSA construction
    -> analyses over the completed JIT CFG
```

A `BytecodeBlock` is not required to correspond one-to-one with a block in a
JIT IR. Lowering may introduce internal blocks for guards, specialized cache
cases, operator continuations, calls, post-call checks, side exits, or backend
requirements. Consequently, bytecode dominance is not computed. An optimizer
that needs dominance computes it over the completed JIT CFG.

## Opcode Format Metadata

There must be one authoritative mapping from an encoded opcode to the
information required for structural scanning:

- encoded instruction length;
- control-flow classification;
- location and encoding of any explicit branch target;
- whether execution falls through;
- whether the opcode terminates bytecode control flow;
- any role in a compound semantic operation.

The exact representation may be a definition table, generated table, or
constexpr mapping. It must drive structural scanning and be shared with the
standalone decoder. The interpreter remains free to use literal operand
offsets and lengths in its handlers, but tests and debug validation must detect
disagreement with the authoritative format metadata.

The structural format is distinct from the semantic instruction span. For
example, an operator and its encoded `CheckOperatorNotImplemented`
continuation are separate physical opcodes but may decode as one semantic
`BytecodeInstruction`.

## Standalone Instruction Decoding

The fundamental semantic decoding API remains available without constructing
a `BytecodeDecoder`:

```cpp
BytecodeInstruction
decode_instruction(const CodeObject &code_object, uint32_t pc_offset);
```

It decodes one semantic instruction starting at `pc_offset` within the supplied
code object. The returned value owns its operands and value effects rather than
referring to temporary decoder storage. It provides at least:

```cpp
class BytecodeInstruction
{
public:
    uint32_t pc_offset() const;
    uint32_t next_pc_offset() const;
    std::optional<uint32_t> continuation_pc_offset() const;
    std::optional<uint32_t> jump_target_pc_offset() const;

    Bytecode encoded_opcode() const;
    Bytecode semantic_opcode() const;
    const std::vector<BytecodeOperand> &operands() const;
    const std::vector<BytecodeValueLocation> &sources() const;
    const std::vector<BytecodeValueLocation> &destinations() const;

    std::optional<InlineCacheReference> cache() const;
    std::optional<InlineCacheReference> cache2() const;
};
```

`encoded_opcode()` preserves compact physical spellings such as `Star0`, while
`semantic_opcode()` normalizes them to their semantic operation such as
`Star`. The second cache member is an intentionally temporary concession for
the few current instructions that carry two cache indexes; it is not a general
multi-cache abstraction.

The semantic requirements are:

- accumulator reads and writes are explicit;
- register operands are decoded into semantic locations;
- constants and inline caches retain typed indexes rather than raw bytes;
- relative jump operands retain their signed encoded displacement, while
  `jump_target_pc_offset` exposes the resolved code-object-relative target;
- `next_pc_offset` is the first offset after the complete semantic operation;
- `continuation_pc_offset` preserves a separately resumable internal
  continuation;
- standalone decoding does not snapshot or attach inline-cache state.

For a standalone result, `cache()` and, where necessary, `cache2()` identify
whether the encoded instruction has a cache and which typed cache entry it
addresses. There are no snapshot pointers because there is no
compilation-scoped snapshot. The block iterator will resolve these references
against decoder-owned snapshot storage.

The full bytecode printer and instruction tracer are built on this API. Full
printing repeatedly advances by `next_pc_offset`; tracing converts the current
interpreter PC to its code-object-relative offset before decoding. Neither
diagnostic consumer pays the cost of snapshotting all inline caches.

## Compound Operator Instructions

Cached reflectable operators use an encoded continuation opcode to handle a
result of `NotImplemented`. JIT consumers should see the operator protocol as
one semantic instruction:

```text
pc_offset               operator entry and pre-effect recovery point
continuation_pc_offset  CheckOperatorNotImplemented continuation
next_pc_offset          first instruction after the complete operator protocol
```

The continuation offset remains observable compiler metadata even though it
is not an ordinary bytecode-block entrance. A lowering may ignore the
continuation when a snapshotted trusted cache action cannot produce
`NotImplemented`, or use it for an untrusted call path or post-effect recovery.

Ordinary jumps, exception-table boundaries, and block entrances must not split
a compound semantic instruction. The bytecode verifier must reject such a
target. Standalone decoding at the continuation offset remains supported
because the interpreter tracer may observe execution of that physical
continuation opcode.

## BytecodeDecoder Lifetime and Feedback

`BytecodeDecoder` is a compilation-scoped object:

```cpp
class BytecodeDecoder
{
public:
    explicit BytecodeDecoder(const CodeObject &code_object);

    const std::vector<BytecodeBlock> &blocks() const;
};
```

Construction occurs in this order:

1. Copy every allocated inline-cache table into decoder-owned snapshot
   storage.
2. Structurally scan the complete encoded bytecode.
3. Validate instruction boundaries, control-flow targets, exception-table
   boundaries, and compound-operation boundaries.
4. Construct the `BytecodeBlock` list in ascending PC-offset order.

All instructions produced through a block iterator refer to the same feedback
snapshot. The iterator uses the standalone semantic decoder, resolves its
typed cache reference against the decoder-owned snapshot tables, and attaches
a pointer to stable immutable snapshot storage.

`CodeObject` groups its seven cache vectors in an `InlineCacheTables`
aggregate. The decoder copies that aggregate before structural scanning. The
snapshot tables therefore use the same inline-cache structs as `CodeObject`;
there is no parallel snapshot type hierarchy. Copying a
`KeywordCallInlineCache` deep-copies its keyword-destination register array
using the cache's stored `n_kw_args` length. All other current cache structs
are directly copyable.

The snapshot convention is:

```text
no encoded IC       cache reference = none
                    snapshot pointer = null

uninitialized IC    cache reference = typed index
                    snapshot pointer = non-null Uninitialized snapshot

initialized IC      cache reference = typed index
                    snapshot pointer = non-null initialized snapshot
```

The decoder and the storage referenced by its blocks, instruction views, and
snapshot pointers must remain stable for the lifetime of those views. The
implementation may make the decoder immovable or place its backing storage at
stable addresses.

One decoder represents one logically coherent observation of feedback, even
when that observation is stale by the time compilation completes. The initial
implementation may rely on the current single-threaded cache-mutation policy.
When caches can mutate concurrently with compilation, synchronization is added
inside decoder construction without changing consumers. Copying the tables
without synchronization is not a coherent snapshot; all writers and snapshot
readers will need a shared locking, atomic-publication, or equivalent protocol.

Snapshot entries that retain heap pointers must also keep those objects alive
for the compilation or replace them with stable semantic identities. The
concrete ownership mechanism is part of the future concurrent compilation and
reclamation design.

## Bytecode Blocks

The structural scan decodes only enough information to determine physical
instruction boundaries and control flow. It does not construct and retain a
fully decoded semantic instruction stream.

A block is a half-open range of code-object-relative bytecode offsets with
bytecode-level connectivity:

```cpp
class BytecodeBlock
{
public:
    BytecodeBlockId id() const;
    uint32_t start_pc_offset() const;
    uint32_t end_pc_offset() const;

    const std::vector<BytecodeBlockId> &predecessors() const;
    const std::vector<BytecodeBlockId> &successors() const;

    std::optional<uint32_t> exception_handler_index() const;
    const std::vector<uint32_t> &exception_entrances() const;

    InstructionRange instructions() const;
};
```

`BytecodeBlock` is a lightweight view tied to its owning decoder. Its
instruction iterator decodes semantic instructions lazily within
`[start_pc_offset(), end_pc_offset())` and attaches the decoder's feedback
snapshots.

Block leaders include:

- function entry;
- explicit jump targets;
- fallthrough after conditional branches;
- the encoded instruction after a non-fallthrough terminator when more code
  follows, so unreachable bytecode remains representable;
- exception-table range starts and ends;
- exception-handler offsets.

Function calls, safepoints, and merely fallible instructions do not split
blocks. Their runtime behavior is represented during JIT lowering rather than
as bytecode CFG structure.

Every explicit target and exception-table boundary must be a valid physical
instruction boundary. Every ordinary block boundary must also be a valid
semantic boundary rather than an internal continuation.

## Exception Entrances

Splitting at every exception-table start and end makes the applicable
exception handler constant throughout a block. `exception_handler_index()`
records the first covering exception-table entry according to the existing
table priority rule.

Exception handlers are secondary entrances, not ordinary branches or calls.
An entrance annotation identifies the exception-table entries that may enter a
handler block and preserves the distinct entry contract: the logical frame is
already active and a pending exception is present.

The initial JIT may exit to the interpreter for exception dispatch. In that
policy the bytecode block graph does not need an exceptional edge from every
fallible instruction to its handler. A future JIT that compiles exception
dispatch may turn the annotations into explicit IR control flow. That control
flow belongs to the JIT CFG rather than changing the lightweight bytecode
block representation.

## Instruction Value Effects

`BytecodeInstruction` exposes value sources and destinations independently of
the accumulator/register encoding. For example:

```text
Add temporary
    sources:      temporary, accumulator
    destinations: accumulator

Star temporary
    sources:      accumulator
    destinations: temporary
```

The decoder preserves semantic location categories such as accumulator,
local-slot storage, and definitely assigned temporaries. A possibly unbound
local is read only by a local-aware operation such as `LoadLocalChecked`;
ordinary arithmetic, calls, and comparisons cannot treat that storage slot as
a definitely defined value source. Reading an undefined temporary is a
bytecode or compiler invariant failure rather than Python-visible behavior.

The iteration bytecodes deliberately avoid edge-specific accumulator effects.
`ForIter`, `ForIterRange1`, and `ForIterRangeStep` define the accumulator on
both control edges: the body edge receives the next item and the exhausted edge
receives `None`. They therefore have the same accumulator destination
regardless of the successor.

`ForPrepRange1`, `ForPrepRange2`, and `ForPrepRange3` expose every range
register as both a source and a destination:

```text
sources:      each range register
destinations: each range register
```

On a fallback outcome a destination may retain its input value; on a prepared
outcome it contains the specialized range state. Both outcomes nevertheless
have the same location-level source and destination shape. The generic decoder
therefore does not need a general edge-specific value-effect mechanism.

## JIT Consumption

Each JIT pre-creates an IR entry block for every `BytecodeBlock`, including its
complete architectural-state interface, and then visits bytecode blocks in
ascending PC-offset order:

```cpp
BytecodeDecoder decoder(code_object);

for(const BytecodeBlock &block: decoder.blocks())
{
    for(BytecodeInstruction instruction: block.instructions())
    {
        lower(instruction);
    }
}
```

The architectural environment includes the accumulator and frame locations
needed for exact interpreter recovery. Location states such as resident and
undefined remain distinct; a complete environment does not invent an SSA
value for an undefined temporary.

Every bytecode edge carries the complete outgoing architectural state expected
by its target entry. This deliberately avoids an initial liveness pass and
block-parameter pruning. It also makes lowering order independent of dominance:
target parameters exist before predecessor arguments are attached, including
for loop backedges.

PC-offset order is the standard frontend traversal order. It provides
deterministic output, straightforward correspondence with bytecode dumps, and
good decoding locality. It also visits unreachable ranges and secondary
handler entrances.

The JIT is free to expand one bytecode block into multiple IR blocks and to
choose how complete state flows through those internal blocks. After lowering,
its own CFG is authoritative for dominance, loop analysis, optimization, edge
moves, and backend layout.

## Validation

The implementation should include structural tests for:

- the authoritative encoded length of every opcode;
- agreement between builder output, decoding, printing, tracing, and
  interpreter handler lengths;
- full-stream iteration ending exactly at `CodeObject::code.size()`;
- forward and backward branches landing on instruction boundaries;
- conditional fallthrough and non-fallthrough terminators;
- unreachable encoded ranges;
- exception range splitting, handler entrances, and table priority;
- rejection of targets into operands or compound continuations;
- operator decoding with correct `pc_offset`, `continuation_pc_offset`, and
  `next_pc_offset`;
- standalone decoding without feedback snapshots;
- decoder-owned uninitialized and initialized feedback snapshots;
- stable snapshot pointers throughout decoder lifetime;
- explicit accumulator and register source/destination effects;
- identical accumulator destinations on both `ForIter` control edges;
- read-modify-write effects for every `ForPrepRange*` form.

Printer tests remain high-value end-to-end decoder tests. JIT tests should also
compare block connectivity and semantic instruction streams against known
bytecode shapes before testing target-specific lowering.
