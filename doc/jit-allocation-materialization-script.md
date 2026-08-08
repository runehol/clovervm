# JIT Allocation Materialization Script

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Proposed |
| Scope | Replacement of the post-allocation CFG materializer |
| Owning layer | Generic register allocation materialization |
| Inputs | Frozen allocated CFG, `PreparedAllocationProblem`, `AllocationConstraints`, and `RegisterAllocationResult` |
| Outputs | Fresh materialized CFG, `LocationAssignments`, and managed-frame spill extent |

The allocation materializer will stop rewriting the allocated CFG in place.
It will first derive a complete materialization script from the frozen input and
then execute that script once to construct a new CFG. Initially every original
instruction is cloned. Correctness does not depend on sharing instructions
between the two graphs.

This replaces the current coupling between graph-rewriter normalization and
physical carrier selection. A register-allocation transfer creates another
physical carrier for a value; it does not globally replace that value's SSA
definition. Materialization must therefore resolve each occurrence through its
bundle and the carrier state at that exact point.

## Required Invariants

- The input CFG remains published, frozen, and readable until construction of
  the output CFG is complete.
- `OccurrenceId`, occurrence anchors, liveness positions, `BundleId`, and the
  transfer schedule always describe the input CFG. They are never updated to
  refer to partially materialized instructions.
- Planning performs every fallible allocation-materialization decision before
  creating the output graph. Script execution is mechanical; an inconsistency
  at that stage is a compiler bug.
- Every input block, block parameter, ordinary instruction, edge, and side-exit
  region has an explicit output counterpart.
- Initially, every input instruction has exactly one newly allocated clone.
  Transfer and fixed-copy instructions are additional definitions.
- A use is resolved by its occurrence, not by globally normalizing its original
  `InstructionId`.
- The script assigns its own dense definition IDs to cloned parameters,
  instruction results, and physical-transfer results. Graph construction only
  translates those script IDs into fresh `InstructionId`s.
- Each output operand is defined in its block or supplied through a block
  parameter according to the CFG's existing locality rules.
- The output CFG is not published until it verifies successfully.
- Only the output CFG and its `LocationAssignments` proceed to transition
  planning and machine-code emission.

## Phase Boundary

Materialization has two explicit phases:

```text
frozen input CFG + allocation products
                 |
                 v
        plan_materialization()
                 |
                 v
       MaterializationScript
                 |
                 v
    build_materialized_graph()
                 |
                 v
 fresh CFG + locations + spill extent
```

`plan_materialization()` may return `RegisterAllocationError`. It resolves
abstract spill slots, indexes occurrences by bundle, orders parallel copies,
legalizes stack-to-stack copies through target-provided scratch registers,
groups actions at their structural insertion sites, and validates that every
required carrier can be established.

`build_materialized_graph()` consumes the immutable script using
`GraphBuilder`. It does not run `GraphRewriter`, recompute liveness, reinterpret
the allocation, or make transfer-placement decisions.

## Script Vocabulary

The precise storage layout may be adjusted while implementing, but the script
must express the following structure:

```cpp
struct MaterializationScript
{
    IRLevel ir_level;
    std::optional<BytecodeStateOrder> bytecode_state_order;
    std::vector<BlockScript> blocks;
    uint32_t managed_frame_spill_extent;
};

struct BlockScript
{
    std::variant<OriginalBlockScript, EdgeTransferBlockScript> origin;
    uint32_t loop_depth;
    EntryBlockKind entry_kind;
    std::vector<ParameterScript> parameters;
    std::vector<InstructionScript> body;
    TerminatorScript terminator;
};

struct InstructionScript
{
    InstructionId original_instruction;
    BoundaryProgram prelude;
    std::vector<OperandScript> operands;
    std::optional<ResultScript> result;
    std::vector<TemporaryScript> temporaries;
};

struct EdgeScript
{
    const BlockEdge *original_edge;
    std::vector<EdgeParameterScript> parameters;
    std::vector<BoundaryProgram> phases;
    std::vector<BundleId> outgoing_bundles;
};

using ScriptDefinitionId = DenseId<ScriptDefinition>;

struct OperandScript
{
    uint32_t operand_index;
    OccurrenceId occurrence;
    ScriptDefinitionId source;
};

struct PlannedMove
{
    OrderedMoveSource source;
    PhysicalLocation source_location;
    PhysicalLocation destination;
    RegisterClass register_class;
    ScriptDefinitionId output;
};

struct BoundaryProgram
{
    std::vector<ScriptDefinitionId> assignment_sources;
    std::vector<PlannedMove> moves;
    std::vector<BoundaryBinding> bindings;
};
```

These are structural sketches, not a requirement to introduce
`EntryBlockKind` or to use a separate heap allocation for each vector. In
particular, instruction order is the primary ordering for a block. Liveness
positions validate and merge allocator events into instruction-boundary slots;
the executor does not sort an undifferentiated collection of events and infer
what they mean.

`ScriptDefinitionId` is local to the immutable script. Every planned move that
will emit an SSA definition receives one, as does every cloned ProgramValue
parameter or result. Each original parallel assignment also names its result
definition. For a physical no-op that result is its existing source definition;
it does not invent an instruction.

Occurrences and transfers retain different authority:

- an occurrence identifies the bundle required by a particular original use,
  definition, edge argument, or temporary;
- the transfer schedule identifies when a new carrier must be established;
- an ordered parallel assignment identifies the legal sequence of physical
  moves implementing that transfer.

Script creation fully resolves parallel assignments, including scratch moves,
ordinary authoritative transfers, fixed operand copies, and reused-input
copies. A `BoundaryBinding` labels an assignment result as a destination-bundle
carrier or as an override for one operand of the following instruction. Graph
construction does not rerun the parallel-assignment resolver or decide the
relative order of these operations.

The materialized input is Machine IR. Snapshot instructions and Snapshot
operands have already been removed by side-exit lowering, so every main-CFG
ProgramValue operand has an allocation occurrence. If a future Python-call
representation introduces nonphysical operands, that design must extend the
script explicitly rather than adding an implicit fallback to the original
definition ID.

## Carrier State

While creating the script, the planner maintains block-local carrier state:

```cpp
struct Carrier
{
    ScriptDefinitionId definition;
    PhysicalLocation location;
};

using CarrierState = absl::flat_hash_map<BundleId, Carrier>;
```

The map answers: "which planned definition carries this allocated bundle at
this point in this block?" It is temporary planning state and is not stored in
the script or output CFG.

For an operand occurrence, construction follows:

```text
OccurrenceId
    -> BundleId
    -> current block-local planned Carrier
    -> ScriptDefinitionId recorded in the script
```

Executing a logical transfer during planning binds its destination bundle to
the assignment result. That result may be the source `ScriptDefinitionId` when
the locations already alias, or a new ID produced by a planned move. It does
not erase or globally replace the source bundle's carrier. This permits one
semantic input value to be available simultaneously in, for example, its
ordinary register, a canonical stack slot used by a transition program, and an
ABI argument register.

When a coalesced bundle represents successive non-overlapping SSA definitions,
cloning the later definition rebinds that bundle to the later fresh instruction
script ID. The liveness-derived script determines where that rebinding is valid.

The graph builder uses a separate dense translation table:

```cpp
std::vector<std::optional<InstructionId>>
    instruction_by_script_definition;
```

Creating a parameter, transfer result, or cloned instruction fills its assigned
slot. Every scripted operand directly names a previously filled slot. The
builder therefore does not interpret bundles or liveness and cannot select a
different carrier from the one that was validated.

Temporary occurrences never enter carrier state because they have no SSA
definition. Their script entries contain only the occurrence and final physical
location to publish for the cloned instruction's temporary index. Script
validation rejects a bundle transfer whose source is a temporary live range.

## Constructing the Fresh Graph

Construction follows the same broad shape as bytecode-to-CFG construction:

1. Create a `GraphBuilder` with the input IR level and copy graph-wide metadata,
   including `BytecodeStateOrder` when present.
2. Create all block skeletons before filling any block. This includes every
   original block and every planned edge-transfer helper block. Preserve
   program order, loop depth, the normal entry block, and all exception entry
   blocks.
3. Walk the block scripts one at a time. Create that block's parameters, emit
   its already planned boundary programs and cloned body instructions, create
   its outgoing edges, and finally create its terminator. All possible targets
   already have block skeletons, while every edge argument is available from
   the completed source body.
4. When cloning an instruction that owns a side exit, clone or retrieve its
   side-exit region through the construction memo before creating the owner.
5. Finalize `LocationAssignments`, verify and publish the new CFG, and return
   it with the managed-frame spill extent.

Instruction reconstruction must always use fresh instruction storage in the
initial implementation. Its resolver must support both first-class
`BlockEdge *` attributes and `SideExitRegionId` attributes so no reference into
the old graph leaks into a clone.

Side exits are discovered while walking owner instructions rather than eagerly
enumerated in the script. Construction maintains an explicit
`old SideExitRegionId -> new SideExitRegionId` memo. The first owner clones the
region's parameters and instructions using a region-local definition map; later
owners reuse the memoized clone. This preserves sharing and prevents a valid but
unintended reference to an old storage-owned region. Transition-only
instructions are structural clones; allocation occurrences still belong only
to physical main-CFG values represented in the prepared allocation problem.

## Edge Transfers

The script represents a transfer as belonging to an original edge. The
allocator does not decide whether its instructions ultimately live in the
source block, destination block, or a split-edge block.

The first implementation uses one helper block for every edge with non-empty
transfers:

```text
source -- incoming carriers --> helper
helper -- transferred carriers --> destination
```

The helper's parameters receive the source-side carriers. Its body performs
the already ordered transfer phases, and its outgoing edge supplies the
resulting carriers to the destination. Each helper inherits its source block's
loop depth. Its script records the incoming parameter bundle and location and
the outgoing bundle separately for every argument; duplicate arguments and
intentional carrier aliases are preserved. This is valid for unconditional,
conditional, and critical edges without needing path-sensitive placement rules
during initial construction.

This is deliberately a construction policy rather than part of the script's
meaning. A later placement optimization may put a transfer in the source when
it is safe on every outgoing path, put it at a single-predecessor destination,
remove redundant transfers, or retain an edge helper. That optimization must
not change bundle selection or occurrence resolution.

## Representative Carrier Split

For an original value `%17` that must remain in `d3`, be stored for a side
exit, and be copied into the `BoxF64` ABI input `d0`, the script can establish
three bundles at their scheduled boundaries:

```text
original:
    %17 = add_f64 ...
    %18 = box_f64 %17

materialized:
    %203 = add_f64 ...       ; ordinary carrier in d3
    %204 = store_stack %203  ; side-exit carrier in spill[-4]
    %205 = move_f64 %203     ; call operand carrier in d0
    %206 = box_f64 %205
```

The side-exit argument used by the transition program resolves through the
bundle carried by `%204`; the `BoxF64` operand occurrence resolves through the
bundle carried by `%205`. Neither transfer globally replaces `%203`.

## Reused Inputs

`SameAsInput` must be supported by the completed design. Successful affinity
coalescing requires no materialization. When the input and result cannot share
one bundle, allocation records an explicit reused-input fixup rather than
returning `RequiresConstraintFixup`:

```cpp
struct ReusedInputFixup
{
    InstructionId instruction;
    uint32_t operand_index;
    OccurrenceId source;
    OccurrenceId result;
};
```

Script creation adds a pre-instruction assignment from the source carrier to
the result bundle's physical location. That assignment is planned together
with every ordinary transfer and fixed operand copy at the boundary. Its result
becomes an operand override for the destructive input; it does not become the
instruction result. Cloning the instruction subsequently assigns the fresh
result definition to that same physical location and binds the result bundle.
The original source carrier remains available if its live range continues.

This sequence preserves the distinction between access timing and list order:
all boundary copies execute before the instruction, Early and Late operand
requirements are checked against their selected carriers, and the destructive
result becomes authoritative only after the instruction. The script retains
the original occurrence timing so validation can check the reused-input
contract and future timing-sensitive fixups without reconstructing it.

## Script Validation

The script is validated before a `GraphBuilder` is created. Validation is a
read-only symbolic execution over script-local definition IDs, independent of
the graph-construction executor.

It checks two classes of invariant:

1. Structural accounting:
   - every original block, parameter, instruction, terminator, and edge appears
     exactly once;
   - every generated helper corresponds to one planned transferred edge;
   - every operand, result, edge argument, and temporary occurrence is attached
     to its original anchor;
   - every allocator transfer, fixed operand copy, and reused-input fixup is
     consumed exactly once;
   - every script definition is introduced once and used only where it is
     available.
2. Physical flow:
   - every operand's script definition has the representation and physical
     location assigned to its occurrence's bundle;
   - each planned move reads the symbolic value occupying its stated source
     location and establishes the same symbolic value at its destination;
   - writing a destination invalidates an unrelated previous occupant while
     preserving intentional aliases and physical no-ops;
   - instruction clobbers invalidate their registers after permitted Early
     uses, Late uses do not depend on clobbered registers, and results establish
     their assigned locations;
   - reused-input copies are present before destructive instructions and their
     result locations match the cloned instruction results;
   - temporary locations satisfy their constraints but never appear as SSA
     carriers.

The validator uses symbolic value identities, not output `InstructionId`s.
Normal instruction results create a symbolic value, forwarding definitions and
physical transfers preserve one, and block parameters begin a block with
abstract symbolic inputs. This lets it detect a stale bundle-to-definition
binding after its physical location has been overwritten without complicating
the construction executor.

After construction, ordinary CFG verification checks the published graph. A
small realization audit additionally checks that every script definition which
requires an instruction was translated to exactly one new `InstructionId`, all
published locations use those new IDs, every side-exit owner uses the memoized
new region, and no structural reference leaks back to the input CFG.

## Public Result and Call Flow

The materialized allocation must identify the fresh graph:

```cpp
class MaterializedAllocation
{
public:
    ControlFlowGraph &graph() const;
    const LocationAssignments &locations() const;
    uint32_t managed_frame_spill_extent() const;

private:
    ControlFlowGraph *graph_;
    LocationAssignments locations_;
    uint32_t managed_frame_spill_extent_;
};

Result<MaterializedAllocation, RegisterAllocationError> allocate_registers(
    CompilationSession &session,
    const ControlFlowGraph &input,
    const AllocationConstraints &constraints);
```

`ControlFlowGraph *` is storage-owned, like other CFGs created through
`GraphBuilder`; `MaterializedAllocation` does not own or delete it. Backend
observation and emission use `materialized.graph()`, never the input graph.
Constraints and allocation products continue to refer to the input and are
discarded after script execution.

## Implementation Slices

### 1. Product and cloning foundation

- Add a test-only fresh-graph cloning foundation that clones blocks,
  parameters, instructions, edges, entry metadata, loop depths, graph metadata,
  and side-exit regions.
- Follow the bytecode-to-CFG construction shape: create every original and
  helper block skeleton first, then fill blocks one at a time through parameters,
  body, edges, and terminator.
- Extend instruction reconstruction so both `BlockEdge *` and
  `SideExitRegionId` attributes can be remapped into fresh storage.
- Verify exact structural cloning while leaving the production allocator and
  backend on the existing materializer. A clone without materialized transfers
  is not a valid allocated graph and must not become a production intermediate
  state.

### 2. Materialization script construction

- Extract current spill-location resolution, transfer-source discovery,
  parallel-copy ordering, stack-copy legalization, and fixed-copy planning into
  a read-only script builder.
- Build the explicit occurrence-to-bundle index once.
- Assign dense script definition IDs while symbolically resolving block-local
  carrier state.
- Produce block and edge scripts, including helper blocks and ordered edge
  phases, in deterministic graph/instruction order.
- Plan ordinary transfers, scratch moves, fixed operand copies, and
  reused-input copies into unified boundary programs.
- Validate structural accounting and symbolic physical flow before graph
  construction.
- Unit-test scripts directly, without depending on generated instruction IDs.

### 3. Block-local script execution

- Execute block-entry and before-instruction boundary programs without rerunning
  transfer planning.
- Resolve cloned operands through the dense script-definition translation
  table.
- Clone every instruction and publish locations for results and temporaries.
- Execute fixed and reused-input copies as operand-local definitions rather
  than global replacements.
- Cover simultaneous ordinary, spill, and ABI carriers of one value.

### 4. Edge scripts and helper blocks

- Materialize every transferred edge through a dedicated helper block.
- Clone unaffected edges directly.
- Preserve ordered block-exit and edge-transfer phases and their explicit
  incoming, intermediate, and outgoing bundle bindings.
- Cover conditional edges, joins, loops, critical edges, parallel cycles, and
  mixed register/stack transfers.
- Verify that all arguments are locally available and that predecessor indexes
  are correct after publication.

### 5. Side exits and backend cutover

- Integrate cloned side-exit regions and remapped owning-instruction attributes
  with script-definition-based carrier selection. Discover regions at owner
  instructions and memoize shared clones.
- Cover transition operands that intentionally load from stack carriers.
- Extend `MaterializedAllocation` to carry the output CFG, make the allocation
  input CFG const, and switch backend observation and emission to the returned
  graph in one cutover.
- Remove the old graph-rewriter materializer and the temporary emitter-side
  `BoxF64` location fixup once the new materializer supplies the required ABI
  carrier.
- Run the full debug test suite and inspect representative AArch64 JIT dumps.

The slices may temporarily coexist with the old materializer behind tests, but
there must be one production materialization path at cutover. No compatibility
normalization layer should survive merely to accommodate the old in-place
model.

## Deferred Optimizations

- Place edge transfers in existing source or destination blocks where legal.
- Eliminate empty helper blocks and redundant physical copies.
- Consider reusing original instruction storage for unchanged instructions.

Instruction reuse is not assumed to be valid. Before implementing it, prove
that sharing an instruction between graph generations preserves operand
references, instruction ownership expectations, location identity, side-exit
references, and graph verification. If cloning remains cheap, retaining the
simpler invariant may be preferable.

These optimizations must operate behind the script/executor boundary. They do
not alter allocator results, occurrence identity, or the definition of carrier
state.

## Verification

Tests should establish:

- the input CFG retains exactly its original blocks, edges, instructions, and
  references after allocation;
- every original instruction and side-exit instruction receives a distinct
  output instruction ID;
- normal and exception entries, loops, edges, parameters, and side-exit region
  references are preserved;
- shared side-exit regions are cloned once and every owner refers to the
  memoized clone rather than the old storage-owned region;
- each operand occurrence resolves to its planned script definition and the
  carrier for its allocated bundle;
- transfers create destination carriers without destroying still-live source
  carriers;
- fixed operand copies and reused-input copies affect only their selected
  operands, and destructive results reuse the copied location;
- edge transfers remain path-specific on branches and compose correctly at
  joins and loops;
- stack cycles use legal scratch sequences;
- `LocationAssignments` mention only the output graph and all emitter-visible
  physical definitions and temporaries have locations;
- backend observation and emission consume the fresh graph;
- failed planning never publishes or mutates a graph.

The existing allocation materializer and AArch64 execution tests provide the
behavioral cases. New script tests should additionally make planning failures
and exact carrier selection visible without relying on graph-rewriter
normalization.
