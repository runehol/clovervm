# JIT Bytecode State Tracking and Translation

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Partial: shared state tracking and the initial `CoreBytecodeTranslator` structural slice are implemented; broader Core opcode coverage and Semantic translation remain |
| Scope | Shared symbolic bytecode state, target-driven bytecode-to-IR translation, block state transfer, multiple results, and Snapshot state queries |
| Owning layers | The bytecode decoder owns decoded locations and structural blocks; the shared JIT state layer owns opcode-blind bytecode state tracking; each concrete translator owns traversal, IR construction, opcode semantics, and Snapshot layout |
| Validated against | `tests/test_jit_bytecode_state.cpp`, `tests/test_jit_core_bytecode_translator.cpp`, `tests/test_jit_cfg.cpp`, and `tests/test_jit_arena.cpp` |
| Supersedes | The open decoded-bytecode input and `BuilderContext` shape in [JIT Compiler Bring-up Plan](jit-compiler-bring-up-plan.md) |

This document defines the shared machinery used to translate decoded clovervm
bytecode into either Core IR or the optional Semantic IR. It deliberately does
not define type or shape inference. Its job is narrower: track which IR
`ProgramValueRef` represents the accumulator and each bytecode register at a
particular program point, and carry that state through bytecode blocks.

The intended pipeline is:

```text
BytecodeDecoder
    -> CoreBytecodeTranslator or SemanticBytecodeTranslator
         -> BytecodeStateTracker
    -> Core IR or Semantic IR
```

The decoder already provides normalized semantic instructions, explicit value
source and destination locations, structural blocks and normal edges, and a
compilation-local inline-cache snapshot. The translation layer consumes those
facilities rather than introducing another decoded instruction stream or CFG.

## Ownership and Design Boundary

The responsibilities are divided as follows.

`BytecodeState` is one copyable symbolic accumulator/register mapping.

`BytecodeStateTracker`:

- initializes function-entry and ordinary-block states;
- maps decoded bytecode locations to state entries;
- resolves instruction source locations to `ProgramValueRef`s;
- applies zero, one, or several result references to destination locations;
- copies and exports complete states for edges;
- does not inspect bytecode opcodes.

Each concrete bytecode translator:

- walks decoded bytecode blocks and instructions;
- pre-creates target IR blocks;
- creates a block's eager parameters immediately before walking that block;
- threads `BytecodeState` through ordinary instructions;
- forks state for edge-specific conditional results;
- connects outgoing state to target block arguments;
- creates target instructions through the `GraphBuilder`;
- receives the target block for the decoded bytecode block;
- interprets each decoded opcode and its non-value operands;
- returns the `ProgramValueRef`s produced by the operation;
- emits Core expansion or an atomic Semantic operation;
- emits terminators and instruction-local side exits;
- queries complete bytecode state when it needs to construct a target-specific
  Snapshot or frame-state object.

Type inference, shape inference, specialization partitions, and effect analysis
are separate consumers of IR. They do not belong in bytecode state tracking.

There is intentionally no generic translation driver or callback protocol in
the initial implementation. Core translation owns its complete walk and may
handle an awkward opcode directly. Semantic translation may later reuse the
same state tracker while making different traversal or construction decisions.
Common driving machinery should be extracted only after both translators
demonstrate a stable shared shape.

## Bytecode State

The state is parameterized by the target's program-value reference type:

```cpp
template <typename Ref>
class BytecodeState;

template <typename Ref>
class BytecodeStateTracker;
```

Core and Semantic IR may ultimately use the same `ProgramValueRef` type. Keeping
the state machinery parameterized avoids making that an architectural
requirement before Semantic IR exists.

`BytecodeState` is cheap to copy. Conditional translation uses an ordinary copy
for each outgoing edge; it does not use persistent maps, rollback logs, or a
special state-forking subsystem.

### State coordinates and ordering

`BytecodeState` represents semantic locations rather than one externally
observable flat layout:

```text
accumulator
parameters, addressed by parameter/register index
locals, addressed by local/register index
temporaries, addressed by temporary/register index
```

ABI parameter padding and frame-header fields are not state entries in the
initial non-inlined tracker. `BytecodeStateTracker` privately maps the raw
register indices in `BytecodeValueLocation` onto the corresponding semantic
entry.

Block parameters and edge arguments require a deterministic positional
contract. The tracker owns that block-transfer order and uses the same order
for every block signature and edge argument vector. This order is internal to
block transfer rather than a universal serialized bytecode-state format.

Snapshot construction queries the same `BytecodeState` by semantic location,
but the Snapshot and recovery layer owns its own physical ordering and
structural descriptors. In particular, it may encode the accumulator action
separately and may later insert inlined frame-header structure. Consistency
comes from querying one authoritative state, not from coupling block arguments
and Snapshot operands to one flat position sequence.

### Initial values

The function-entry state is initialized differently from an ordinary block:

- parameters receive target-created entry `ProgramValueRef`s;
- locals receive a target-created `Uninitialized` reference;
- temporaries receive a target-created `Uninitialized` reference;
- the accumulator receives the same `Uninitialized` reference as an
  uninitialized temporary.

Every tracked location therefore contains a `ProgramValueRef`, including the
initial accumulator and semantically uninitialized locals and temporaries.
Checked local loads preserve Python behavior by testing the local sentinel.
Accumulator and temporary definite initialization remain bytecode-generation
invariants rather than another dataflow domain in the state tracker.

A bytecode instruction must define the accumulator before any operation whose
semantics require a normal accumulator value. A Snapshot queries the sentinel
reference through the same semantic-location interface as every other value;
the recovery layer decides whether to encode that sentinel structurally or
materialize it.

An ordinary non-entry block begins with eager target block parameters in
the tracker's block-transfer order. The register allocator may merge their live
ranges and eliminate unnecessary edge moves. The translator does not need a
separate trivial-parameter construction policy.

## Opcode-Blind State Updates

`BytecodeStateTracker` operates on decoded value locations rather than opcodes.
Its central interface is equivalent to:

```cpp
std::vector<Ref>
read(const BytecodeState<Ref> &state,
     std::span<const BytecodeValueLocation> sources) const;

void write(BytecodeState<Ref> &state,
           std::span<const BytecodeValueLocation> destinations,
           std::span<const Ref> results) const;
```

All sources are resolved against the pre-instruction state before any
destination is updated. The write is semantically simultaneous, which preserves
read-modify-write behavior and permits overlapping source and destination
locations.

The decoded instruction exposes two different notions that must remain
distinct:

```text
instruction.operands()       encoded constants, registers, cache indices,
                             counts, jump displacements, and other metadata

instruction.sources()        bytecode value locations read by the instruction

instruction.destinations()   bytecode value locations written by the instruction
```

The concrete translator resolves `sources()` through the tracker and examines
`operands()` when it needs non-value metadata.

The ordinary translation sequence is:

```cpp
auto inputs = tracker.read(state, instruction.sources());
auto outputs = translate_instruction(builder, block, instruction, inputs);
tracker.write(state, instruction.destinations(), outputs);
```

The target must return exactly one result for every decoded destination.
Instructions with no destinations return no results. Multiple results are a
first-class bytecode property rather than a special case.

### Aliasing bytecodes

Bytecodes such as `Ldar`, `Star`, and `Mov` need not create a new IR value.
Their translation may return an input reference unchanged:

```text
Ldar r0:
    input  = state[r0]
    output = input
    destination accumulator = output
```

The state tracker then records that the source and destination denote the same
SSA value. It does not emit a target `Mov` merely because the bytecode changes
an interpreter-visible location.

## Blocks, Parameters, and Edge Arguments

The concrete translator first creates stable target block identities:

```cpp
builder.emplace_n_blocks(decoder.blocks().size());
```

It does not pre-create every block's start state. Immediately before walking a
bytecode block, it creates that target block's eager parameters and initializes
the corresponding `BytecodeState`.

This ordering supports both forward edges and backedges without incomplete-phi
machinery:

- a forward edge can record its canonical argument vector before its target
  parameters are created;
- the target parameters are created when the target block is walked;
- a backedge is attached later to parameters that already exist;
- final CFG verification checks parameter/argument arity and representation.

Block parameters and edge arguments are the IR's native SSA merge mechanism.
The state tracker does not construct a parallel phi representation.

In the initial design, one decoded bytecode block maps to one target IR block.
Expanding one bytecode operation may append several target instructions, but it
does not introduce internal CFG blocks. Guards and non-returning side exits
remain instruction-local exits and are not block successors. This preserves the
existing CFG, analysis, and register-allocation contracts.

The target receives the `GraphBuilder` and the block into which it appends the
operation's target instructions:

```cpp
Block *block = builder.block_at(bytecode_block.id());
```

Outgoing bytecode edges originate from that block's terminator. Introducing
target-internal CFG blocks is deferred until an implemented lowering proves
that they are required.

### Decoded edge occurrences

Conditional arms are edge occurrences, not merely a set of successor blocks.
Two arms may target the same bytecode block while carrying different state
arguments. The decoder must therefore preserve both occurrences in stable
semantic order, initially fallthrough followed by jump.

The current decoder's successor deduplication came from treating successors and
predecessors as unique adjacent block IDs. It serves no translation requirement
and must not erase conditional edge occurrences. If a later analysis needs a
unique adjacent-block set, it can derive that set from the occurrence list.

## Conditional and Edge-Specific Results

A conditional bytecode may write the same destination locations on every
outgoing edge while writing different values to those locations. Multiple
results and edge-specific results are orthogonal:

```text
edge A -> [result for destination 0, result for destination 1, ...]
edge B -> [result for destination 0, result for destination 1, ...]
```

The concrete translator handles this by copying the pre-terminator state once
per edge:

```cpp
auto inputs = tracker.read(state, instruction.sources());
auto edge_results = translate_edges(builder, block, instruction, inputs);

for(const EdgeResult &result: edge_results)
{
    BytecodeState edge_state = state;
    tracker.write(edge_state, instruction.destinations(), result.outputs);
    connect_edge(builder, result.edge, edge_state);
}
```

Core represents ordinary conditional control flow with one
`ConditionalBranch(condition, true_edge, false_edge)` terminator. Bytecode
branch polarity only determines how decoded edge occurrences map onto those
two attributes:

```text
JumpIfTrue:  true edge = jump,        false edge = fallthrough
JumpIfFalse: true edge = fallthrough, false edge = jump
```

The two `BlockEdge`s carry their independently constructed state arguments.
Core does not need separate branch instruction kinds for the two bytecode
polarities.

This facility is generic. Its soundness does not depend on the current exact
shape of the older range-loop macro bytecodes. A future fast-iterator bytecode
can use the same contract whenever its outgoing states differ.

There is no shared result protocol combining ordinary continuation,
conditional edges, return, unsupported bytecode, and side exit. The concrete
translator owns those control decisions. In particular, an unsupported
fallthrough bytecode may emit a generic `Snapshot` plus
`ResumeInInterpreter`. `ResumeInInterpreter` has the exact `ExitJIT` effect but
is not a CFG terminator: the translator continues generating the rest of the
decoded block, which must still end in an ordinary CFG terminator.

## Complete State Capture and Snapshots

The concrete translator can query the state at any instruction boundary:

```cpp
BytecodeState snapshot_state = state;
```

Core uses that state to construct a zero-code `Snapshot` for a guard, call,
unsupported bytecode, or other exit. Generic unsupported translation is a
`Snapshot` followed by `ResumeInInterpreter`. The Snapshot owns the resume PC
and recovery state; `ResumeInInterpreter` therefore needs only the Snapshot
operand.
Semantic IR may instead associate the same logical state with an atomic
semantic instruction.

Pre-effect recovery captures the state before applying the instruction's
results and resumes at the current bytecode. Post-commit recovery captures the
appropriate updated state and resumes at its later bytecode position. The
concrete translator owns that semantic choice; the state tracker only supplies
the requested state.

Snapshot construction does not consume an edge's argument vector. It queries
the accumulator and logical register groups from `BytecodeState`, then encodes
them in the recovery layout owned by one private translator helper. The initial
structural implementation uses accumulator, parameter, local, and temporary
group order, but that private order is not yet a recovery ABI.

Snapshots support interpreter handoff for exceptions without requiring compiled
exceptional edges. Initial JIT code may recover through a Snapshot and let the
interpreter perform exception-table dispatch. Compiled exception-handler entry
is separate future work: it will require explicit exceptional edges and a
definition of the handler's incoming state.

## Concrete Translators

The initial Core entry point is a concrete target-driven translator:

```cpp
CoreBytecodeTranslator translator(code_object, builder);
ControlFlowGraph *graph = translator.translate();
```

Its implementation owns the block walk, opcode dispatch, state queries,
Snapshot placement, unsupported-operation exits, and graph finalization. The
optional Semantic translator will own an analogous walk when it is implemented.

The first Core slice lowers `LdaConstant`, `LdaSmi`, `LdaTrue`, `LdaFalse`,
`LdaNone`, `Ldar`, `Star`, `Mov`, `Nop`, `Jump`, `JumpIfTrue`, `JumpIfFalse`,
and `Return`. Unsupported bytecodes capture their pre-instruction state in a
Snapshot and emit `ResumeInInterpreter`. Because that exit is not a CFG
terminator, each unsupported destination is then bound to an `Uninitialized`
poison value so structural translation can continue. No generated execution
reaches those poisoned values.

Both translators reuse `BytecodeState` and `BytecodeStateTracker`. They may also
reuse small helpers for block signatures or edge argument construction. They do
not initially implement a common callback interface or inherit from a shared
translation base class.

## Construction Invariants

Initial translation and CFG verification must establish:

- every decoded source location is available when read;
- every ordinary translation returns exactly as many results as the decoded
  instruction has destinations;
- every conditional edge returns exactly as many results as the decoded
  instruction has destinations;
- all state writes are based on sources resolved before the write;
- each non-entry target block has eager state parameters in the tracker's
  block-transfer order;
- each incoming edge has one compatible argument for every corresponding block
  parameter;
- every block signature and edge argument vector uses the tracker's
  block-transfer order;
- Snapshot construction queries state by semantic location rather than assuming
  the block-transfer order;
- bytecode definite-initialization invariants prevent normal operations from
  consuming the initial accumulator or temporary sentinel;
- every completed target block has a valid terminator;
- `ResumeInInterpreter` is an instruction-local `ExitJIT` operation rather than
  a CFG terminator, so translation continues after emitting it;
- every unsupported destination is assigned a structural `Uninitialized`
  poison reference after `ResumeInInterpreter`;
- instruction-local guards and side exits do not become CFG block successors.

Focused tests should cover:

- entry initialization of parameters, sentinel-filled locals and temporaries,
  and the accumulator sharing the temporary sentinel;
- `Ldar`, `Star`, and `Mov` preserving `ProgramValueRef` identity;
- one-result, zero-result, and multiple-result state updates;
- simultaneous read-modify-write updates;
- forward edges and loop backedges with eager block arguments;
- conditional edges carrying different results for the same destinations;
- complete Snapshot capture before and after a state update;
- deterministic graph and state dumps across repeated translations.

## Deferred Extensions

The following are deliberately outside the initial state-tracking design:

- type, shape, range, truthiness, or effect inference;
- a generic shared translation driver or callback protocol;
- pruned block-parameter construction;
- inlined logical-frame composition and frame-header descriptors;
- compiled exceptional edges and handler-entry state;
- target-internal CFG blocks introduced by instruction expansion;
- general edge-specific source or destination location lists when both edges
  use the same decoded locations;
- optimization of state copies before measurement demonstrates a need.

These extensions must preserve the central boundary: decoded locations and
target-produced `ProgramValueRef`s are connected by opcode-blind shared state
tracking, while opcode semantics remain in the concrete translators.

A bytecode entry block having a predecessor is illegal. The decoder asserts
this invariant; Core translation does not introduce a synthetic entry block to
support such bytecode.
