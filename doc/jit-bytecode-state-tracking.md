# JIT Bytecode State Tracking and Translation

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Partial: canonical outer-frame state ordering without ThreadState, typed frame-header entry definitions, CFG attachment, shared state tracking, and an executable straight-line Core translation slice with basic control flow are implemented; broader semantic coverage, executable side exits, and Semantic translation remain |
| Scope | Shared symbolic bytecode state, target-driven bytecode-to-IR translation, block state transfer, multiple results, and Snapshot state queries |
| Owning layers | The bytecode decoder owns decoded locations and structural blocks; the shared JIT state layer owns opcode-blind bytecode state tracking and its canonical ordering description; each concrete translator owns traversal, IR construction, opcode semantics, and Snapshot extent |
| Validated against | `tests/test_jit_bytecode_state.cpp`, `tests/test_jit_core_bytecode_translator.cpp`, `tests/test_jit_cfg.cpp`, and `tests/test_jit_storage.cpp` |
| Supersedes | Earlier decoded-bytecode input and `BuilderContext` sketches |

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
- owns the canonical state ordering used by non-entry block parameters, edge
  arguments, and Snapshots;
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

`BytecodeState` is stored in one canonical flat order:

```text
position 0: accumulator
position 1: highest canonical stack slot
position 2: next descending canonical stack slot
...
```

The active `ThreadState *` is execution context rather than bytecode state. On
AArch64 compiled code receives it in the reserved `x25` JIT thread register; it
is not a block parameter or Snapshot position.

For an outer function with parameters, the highest stack slot is the first
parameter's `CodeObject::encode_reg(0)` slot. For a zero-parameter function, it
is `FrameHeaderReturnPcOffset`, the highest frame-header slot. The outer
`CodeObject` supplies the arity and frame dimensions needed to calculate this
starting point.

The sequence then descends without gaps through:

```text
actual parameters
ABI parameter padding
frame-header slots
locals
temporaries
```

Padding and frame-header slots are real state positions. Every position contains
a `ProgramValueRef`. Raw FP and PC header values use the pointer representation,
while the return `CodeObject` remains tagged. Function entry defines those four
values as parameters fixed to their existing canonical stack positions.

`BytecodeStateOrder` describes this mapping. Position zero is the distinguished
accumulator and has no frame offset or canonical frame home. The tracker uses
positions one onward to translate decoded `BytecodeValueLocation`s into
canonical stack positions. The same description is attached to a
bytecode-derived CFG so later verification, register allocation, and recovery
can interpret positions without consulting the mutable translation
environment.

A decoded `BytecodeValueLocation` is either the accumulator or one signed frame
offset. The decoder converts a bytecode register operand directly with
`CodeObject::encode_reg()`; it does not classify the location as a parameter,
local, or temporary and then require the tracker to reverse that classification.
The sign is semantically meaningful because locals and temporaries normally lie
below `fp`. When inlining is added, the translation environment will compose
this frame-relative coordinate with the active inline-frame prefix; the decoded
instruction itself remains relative to its own `CodeObject`.

For every non-entry block, one position has one meaning across all relevant
interfaces:

```text
state position i
    = block parameter i
    = incoming edge argument i
    = Snapshot operand i, when the Snapshot includes it
```

Position zero identifies the accumulator, and positions one onward identify
canonical frame homes. A canonical home is a preferred spill and
frame-synchronization location rather than a claim that it is continuously
current.

### Initial values

The function-entry state is initialized differently from an ordinary block:

- parameters receive target-created entry `ProgramValueRef`s;
- locals receive a target-created `Uninitialized` reference;
- temporaries receive a target-created `Uninitialized` reference;
- the accumulator receives the same `Uninitialized` reference as an
  uninitialized temporary.
- ABI padding positions receive an unavailable reference;
- frame-header positions receive typed parameter definitions of their actual
  entry contents.

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

The structural Core translator appends four frame-header parameters after the
ordinary function parameters. Previous FP, compiled return PC, and interpreter
return PC are pointer values; the return `CodeObject` is tagged. Target
constraints place them at exact stack offsets `fp[0]` through `fp[3]` using the
ordinary stack-location model. Non-entry block parameters preserve the same
representations. No special frame-header stack-location category or reserved
allocator area is required.

An ordinary non-entry block begins with eager target block parameters in
the tracker's canonical order. The register allocator may merge their live
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

Bytecodes that only copy a value between interpreter-visible state locations
need not create a new IR value. Their translation may return an input reference
unchanged:

```text
copy state[r0] to accumulator:
    input                   = state[r0]
    output                  = input
    destination accumulator = output
```

The state tracker then records that the source and destination denote the same
SSA value. It does not emit a target copy merely because the bytecode changes an
interpreter-visible location.

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

Every ordinary block initially receives an eager parameter for every position
in the complete canonical state order, including padding and frame-header
positions. Every incoming edge initially supplies the complete corresponding
argument vector. Core dead-code elimination later removes unused non-entry
parameters and the matching argument from every incoming edge before register
allocation.

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

One polarity maps the jump occurrence to the true edge and fallthrough to the
false edge; the opposite polarity reverses that mapping.

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
`ResumeInInterpreter`. `ResumeInInterpreter` has
`SideExit | ControlFlow | TerminateBlock` effects, and translation of that
decoded block stops immediately.

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

Snapshot construction reads a prefix of the same canonical `BytecodeState`
sequence used by block parameters and edge arguments. Position zero is always
present and denotes the accumulator; an unavailable accumulator is represented
by its `Uninitialized` reference rather than by changing the ordering. A
Snapshot may omit an unused trailing suffix, but it cannot omit an interior
position. Its operand count therefore describes its extent without an
arbitrary position map.

This prefix rule includes ABI padding and frame-header positions whenever the
Snapshot extends through them. Inlining extends the same descending stack-slot
sequence across outgoing arguments, nested frame headers, locals, and
temporaries. Outgoing argument and callee parameter slots that occupy the same
canonical home appear only once.

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

The implemented Core slice covers canonical constants, symbolic state
movement, a small pure comparison family, basic control flow, and return.
Unsupported bytecodes capture their pre-instruction state in a Snapshot and
emit the `ResumeInInterpreter` terminator. Blocks pre-created from the bytecode
CFG may consequently have no predecessors; later reachability cleanup may
remove them. Exact opcode coverage belongs to translator tests and dispatch
code rather than this design contract.

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
  complete canonical order;
- each incoming edge has one compatible argument for every corresponding block
  parameter;
- every block signature, edge argument vector, and Snapshot prefix uses the
  tracker's canonical order;
- every Snapshot is a prefix of the complete state order;
- bytecode definite-initialization invariants prevent normal operations from
  consuming the initial accumulator or temporary sentinel;
- every completed target block has a valid terminator;
- `ResumeInInterpreter` is an instruction-local
  `SideExit | ControlFlow | TerminateBlock` operation;
- translation emits no ordinary continuation or successor edge after
  `ResumeInInterpreter`;
- instruction-local guards and side exits do not become CFG block successors.

Focused tests should cover:

- entry initialization of parameters, sentinel-filled locals and temporaries,
  the accumulator sharing the temporary sentinel, and all intervening physical
  state positions;
- canonical position calculation for parameterized and zero-parameter
  functions;
- state-location copies preserving `ProgramValueRef` identity;
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
- inlined logical-frame composition;
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
