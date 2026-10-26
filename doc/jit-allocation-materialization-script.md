# JIT Allocation Materialization Script

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Accepted |
| Implementation | Not started |
| Scope | Replacement of the post-allocation CFG materializer and extraction of shared structural mutation machinery |
| Owning layers | Generic register allocation owns carrier planning; JIT graph infrastructure owns structural mutation and commit |
| Validated against | `cca2079b6fd3e48d00456a7845b133b5bba8faf4`, 2026-10-25 |
| Supersedes | The fresh-graph construction direction in earlier revisions of this document (`f5e3a25f`, 2026-08-08; refined in `528a64e4`, 2026-08-09) |
| Inputs | Frozen Machine CFG, `PreparedAllocationProblem`, `AllocationConstraints`, and `RegisterAllocationResult` |
| Outputs | The same CFG after planned mutation, `LocationAssignments`, and managed-frame spill extent |

The allocation materializer will derive a complete carrier script from the
frozen input and apply it through a dedicated executor. It will retain existing
graph structure wherever valid and stage only necessary changes. It will bypass
`GraphRewriter`'s rewrite engine while sharing structural allocation and commit
machinery with it. This is the accepted direction; API names and layouts below
are sketches, and no replacement implementation has landed.

Semantic rewriting and allocation materialization have distinct purposes. The
rewriter replaces computations and normalizes references to their definitions.
A register-allocation transfer establishes another physical carrier for a value;
it does not globally replace that value's SSA definition. Materialization must
resolve each occurrence through its bundle and the carrier state at that exact
point, then realize those exact bindings without inferred substitutions.

The August proposal coupled that separation to unconditional fresh-graph
construction. A dedicated application path removes that requirement. Existing
instructions are immutable, but their IDs can remain in the graph when their
resolved operands and structural references are unchanged. Existing blocks,
parameters, edges, and side-exit regions can likewise be retained. Neither a
complete graph clone nor a second graph generation is required.

## Required Invariants

- The input CFG remains published, frozen, and readable through planning and
  staging. No published sequences or executable edges change until commit.
- `OccurrenceId`, occurrence anchors, liveness positions, `BundleId`, and the
  transfer schedule describe that input generation. They are never retargeted
  to partially materialized instructions and are discarded after application.
- Planning performs every fallible allocation-materialization decision before
  realization. Realization translates planned identities and stages structural
  edits; an inconsistency there is a compiler bug.
- Every original block, parameter, instruction, and edge is accounted for.
  Original blocks and parameter columns remain; affected instructions and edges
  may be replaced, and transfers may add helper blocks and definitions.
- A use is resolved by its occurrence, not by globally normalizing its original
  `InstructionId`.
- Script definition IDs identify planned carriers. Their realization may retain
  an existing `InstructionId` or allocate a new one. A physical no-op may bind
  an existing carrier without emitting any instruction.
- Each operand is defined in its block or supplied through a block parameter,
  according to existing CFG locality rules.
- Updates are staged lazily. Unchanged blocks retain their existing instruction
  vectors; changed vectors may contain both retained and newly allocated IDs.
- Commit updates structure coherently, maintains predecessor indexes and
  mutation generation, and validates surviving references. Materialization does
  not poison replaced instructions.
- Locations describe retained and new definitions in the committed CFG. Backend
  observation and emission continue to consume that same CFG object.

## Phase Boundary

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
     realize_materialization()
                 |
                 v
 staged structural edits + locations
                 |
                 v
      shared structural commit
                 |
                 v
 same CFG, materialized + locations + spill extent
```

`plan_materialization()` may return `RegisterAllocationError`. It resolves
abstract spill slots, indexes occurrences by bundle, orders parallel copies,
legalizes stack-to-stack copies through target-provided scratch registers,
selects edge helpers and boundary actions, and establishes every required
carrier binding. An independent symbolic validator checks the script in debug
and test configurations; production runs need not execute it.

`realize_materialization()` consumes the script without running rewrite
callbacks, recomputing liveness, reinterpreting allocation, or making placement
decisions. Its definition table translates previously selected carriers to
actual retained or newly allocated instructions. It stages edits through the
shared mutation layer and commits only after realization is complete.

Planning and traversal still have costs. Arena allocation does not remove
initialization or memory traffic. The intended common path avoids replacement
instruction allocation, block-vector copying, edge allocation, and predecessor
index rebuilding when those structures are unchanged. These are properties to
verify; compilation-time and memory improvements have not been measured.

## Script Vocabulary

The precise storage layout may be adjusted while implementing, but the script
must express the following structure:

```cpp
using ScriptDefinitionId = DenseId<ScriptDefinition>;

struct ScriptDefinition
{
    ValueRepresentation representation;
    PhysicalLocation location;
    ScriptDefinitionOrigin origin;
};

struct MaterializationProvenance
{
    MaterializationInputKind kind;
    size_t set_index;
    size_t element_index;
};

// Index into this script's realization table.
using MaterializationRealizationId = DenseId<MaterializationRealization>;

struct MaterializationRealization
{
    MaterializationProvenance input;
    // Original edge for a path-specific expansion; null for a local action.
    const BlockEdge *edge;
};

struct PlannedMove
{
    OrderedMoveSource source;
    PhysicalLocation source_location;
    PhysicalLocation destination;
    RegisterClass register_class;
    ScriptDefinitionId output;
};

struct RealizationBinding
{
    MaterializationRealizationId realization;
    ScriptDefinitionId result;
};

struct BoundaryProgram
{
    std::vector<ScriptDefinitionId> assignment_sources;
    std::vector<PlannedMove> moves;
    std::vector<BoundaryBinding> bindings;
    std::vector<RealizationBinding> realizations;
};

struct OperandScript
{
    uint32_t operand_index;
    OccurrenceId occurrence;
    ScriptDefinitionId source;
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
    // Source-local definitions passed to helper parameters, by position.
    std::vector<ScriptDefinitionId> incoming_arguments;
    std::vector<EdgeParameterScript> parameters;
    std::vector<BoundaryProgram> phases;
    // Helper-local definitions passed to destination parameters, by position.
    std::vector<ScriptDefinitionId> outgoing_arguments;
};

struct TerminatorScript
{
    InstructionScript instruction;
    std::vector<EdgeScript> edges;
};

struct BlockScript
{
    std::variant<OriginalBlockScript, EdgeTransferBlockScript> origin;
    uint32_t loop_depth;
    EntryBlockKind entry_kind;
    std::vector<ParameterScript> parameters;
    BoundaryProgram entry;
    std::vector<InstructionScript> body;
    TerminatorScript terminator;
};

struct MaterializationScript
{
    IRLevel ir_level;
    uint64_t input_generation;
    std::vector<ScriptDefinition> definitions;
    std::vector<MaterializationRealization> realizations;
    std::vector<BlockScript> blocks;
    uint32_t managed_frame_spill_extent;
};
```

These are structural sketches, not a requirement to introduce
`EntryBlockKind` or to use a separate heap allocation for each vector. In
particular, instruction order is the primary ordering for a block. Liveness
positions validate and merge allocator events into instruction-boundary slots;
the executor does not sort an undifferentiated collection of events and infer
what they mean.

`ScriptDefinitionId` is local to one immutable materialization script. It is a
typed dense index only so planning can name definitions before the corresponding
realized instructions exist and execution can use an indexed translation
table. It is not allocated by, stored in, or meaningful to
`CompilationStorage`, and it does not survive materialization.

A script definition identifies one planned physical carrier, not the abstract
logical value by itself. The same logical value may therefore have several
script definitions simultaneously: for example, its ordinary carrier in `d3`,
a spill carrier in `spill[-4]`, and an ABI argument carrier in `d0`. This is the
representation that lets planning preserve a value in one location while
creating another usable copy of it. Conversely, when a physical no-op aliases
two bundles, both may name the same script definition.

Every planned move that will emit an SSA definition receives a new script
definition. Original ProgramValue parameters and results also receive script
identities, independently of whether realization retains their instruction IDs.
Each original parallel assignment also names its result definition. For a physical
no-op that result is its existing source definition; it does not invent an
instruction or a new script definition.

Every script definition records its representation and physical location.
Register class alone is insufficient because tagged values and raw pointers
both occupy GPRs but require different transfer instruction families. Its
origin identifies the parameter, original instruction result, helper
parameter, or planned move that creates it.

Occurrences and transfers retain different authority:

- an occurrence identifies the bundle required by a particular original use,
  definition, edge argument, or temporary;
- the transfer schedule identifies when a new carrier must be established;
- an ordered parallel assignment identifies the legal sequence of physical
  moves implementing that transfer.

Script creation fully resolves parallel assignments, including scratch moves,
ordinary authoritative transfers, fixed operand copies, and reused-input
copies. A `BoundaryBinding` labels an assignment result as a destination-bundle
carrier or as an override for one operand of the following instruction. Script
realization does not rerun the parallel-assignment resolver or decide the
relative order of these operations.

A `BoundaryProgram` is a sequential program produced from one or more ordered
parallel phases, not a claim that every contained move is simultaneous. Later
phases may name definitions produced by earlier phases. Phase-local
`OrderedMoveSource` indices are resolved or rebased while the script is built,
so the executor receives an unambiguous dependency order.

Every original action retains its coordinate in the allocation product: transfer
set and element index, fixed-copy index, or reused-input-fixup index. Planning
accounts for each original action once and records its required realizations.
A local action has one realization. A conditional block-exit transfer may have
one realization on each applicable outgoing edge, all referring to the same
original action. Realization identity includes that expansion site; structurally
identical actions and distinct outgoing edges are not interchangeable.

The script assigns each realization an ID. Boundary programs record each ID's
result definition exactly once, including logical no-op bindings that emit no
move. Different realizations may name the same result definition. Scratch moves
and stack-copy legalization implement the physical boundary program and can
support several requests; they are not extra consumptions of original schedule
entries and need not belong exclusively to one realization. Validation checks
both original-action coverage and the complete required set of realization
bindings. Neither a consumed-bit vector over original transfers nor a count of
emitted moves is sufficient to establish this accounting.

### Grouping Compatible Physical Assignment Requests

Keep logical requests, physical assignments, and final operand bindings distinct.
Resolve requests at their required parallel phase, after earlier phases have
established their source carriers. Within that phase, group requests by physical
destination before invoking the existing parallel-assignment resolver, which
rejects duplicate non-no-op destinations.

Requests may share a group only when they require the same semantic value and
compatible representation. Establish value equality from original definitions,
forwarding semantics, or known aliases, not from register class or coincident
locations. A request for a different value or incompatible representation at the
same destination is a planning conflict, not an ordering problem. Do not group
across instruction boundaries, sequential phases, or different edges.

Choose one available, legal source carrier deterministically per compatible
group. Prefer an already valid destination carrier when available, making the
physical assignment a no-op. Source selection must respect the boundary's
reservations and the complete parallel assignment's dependencies. Submit one
physical assignment per destination to the resolver and retain a mapping from
each logical realization to its assignment group. Resolver assignment indices
now identify groups, not individual operand requests. Expand group results back
to every request after copy ordering and legalization; these grouping maps are
planning state, not executor-side carrier selection.

```text
requests: F0 / R0: operand 0 needs a in x0
          F1 / R1: operand 1 needs a in x0
source:   S carries a in x3

group:    G0: x3 -> x0
result:   M = move S
bindings: operand 0 -> M; operand 1 -> M
accounts: R0 -> M; R1 -> M
```

If a valid source already carries `a` in `x0`, the group result is that existing
definition, both operand bindings and realization records still exist, and no
move is emitted. Cycle scratch definitions may feed multiple groups; each group
still resolves to its final result before expansion to per-request bindings.

Each original fixed-copy request retains its own provenance and realization,
even when requests share physical work. `BoundaryBinding`/`OperandScript` retain
the individual operand overrides, while `RealizationBinding` records each
request's result. Validation independently checks every original request's value,
destination, and operand binding and permits shared result definitions. Physical
sharing never erases logical accounting.

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
the script or committed CFG.

For an operand occurrence, planning follows:

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
planning the later definition rebinds that bundle to its script ID. The
liveness-derived script determines where that rebinding is valid.

The executor uses a separate dense translation table:

```cpp
std::vector<std::optional<InstructionId>>
    instruction_by_script_definition;
```

Realizing a parameter or result fills its assigned slot with the retained or
new instruction ID. Transfers and helper parameters receive newly allocated
IDs. A no-op assignment names an already available slot. Every scripted operand
names its selected definition explicitly; the executor does not reinterpret
bundles or liveness to choose another carrier.

An original result's script identity does not promise that its instruction ID
will be retained. If reconstructing its producer changes that ID, later scripted
consumers resolve to the new ID. This is dependency translation, not a rule that
all uses of one semantic value must use the latest physical copy.

Temporary occurrences never enter carrier state because they have no SSA
definition. Their entries contain the occurrence and final location to publish
against the realized instruction's temporary index. Validation rejects a bundle
transfer whose source is a temporary live range.

## Shared Structural Mutation

The current APIs do not provide a standalone published-CFG mutation interface.
[`GraphBuilder`](../src/jit/graph_builder.h) constructs unpublished graphs;
`GraphRewriter` currently owns
access to published block sequences, staged edge splits, and commit. Extract a
small structural layer usable by both clients, leaving their planning engines
separate. Proposed names and signatures illustrate the boundary:

```cpp
struct BlockUpdate
{
    Block *block;
    // Absence retains the existing sequence without copying it.
    std::optional<std::vector<InstructionId>> parameters;
    std::optional<std::vector<InstructionId>> instructions;
};

class GraphMutation
{
public:
    GraphMutation(CompilationSession &session, ControlFlowGraph &graph,
                  uint64_t expected_generation);

    template <typename T, typename... Args>
    T make_instruction(Args &&...args);
    Block *make_block(uint32_t loop_depth);
    BlockEdge *make_block_edge(Block *source, Block *target,
                              std::span<const ProgramValueRef> arguments);
    SideExitRegion *make_side_exit_region(
        std::span<const InstructionId> parameters,
        std::span<const InstructionId> instructions);

    void stage(BlockUpdate update);
    void set_block_order(std::vector<Block *> blocks);
    void set_ir_level(IRLevel level);
    void commit();
};
```

Allocated entities are owned by `CompilationStorage`; the mutation object owns
its temporary staging vectors. Preparing helper blocks and edges does not attach
them to the published block order or executable terminators. Original block
ownership and normal/exception entry metadata remain intact. New helper blocks
are associated with the same graph and inherit their source's loop depth.

Commit checks the expected input mutation generation before installing edits.
Before swapping sequences, the mutation layer compares each changed block's
original and staged terminator successor-edge references, including their
semantic arm positions. It also accounts for edges introduced by newly published
helpers. Untouched block sequences need no comparison. A replacement edge counts
as a change even when only its arguments differ and its endpoints are identical:
predecessor indexes store edge objects, not merely source/target pairs. Allocating
an unattached edge alone does not constitute an executable edge change.

Clients do not declare edge changes through a separate flag. The mutation layer
derives predecessor maintenance from the staged structure before installing the
new sequences. It swaps only supplied sequences, installs a changed block order
when needed, rebuilds predecessor indexes only when executable edge references
change, applies any requested IR-level change, and advances mutation generation
when the graph changes. That generation invalidates cached use lists and
tagged-value analysis through their existing generation checks. A locations-only
realization need not mutate the graph or advance its generation.

Poisoning is client policy, not part of shared structural commit. The rewriter
retains its existing removed-instruction accounting and poisoning behavior after
structural installation. Materialization needs neither a retirement list nor
poisoning: replaced entries remain in compilation-owned storage until that
storage is released. They are absent from the committed graph's sequences, but
still being readable in storage does not make them valid operand definitions.
No reclamation or extra compatibility mechanism is required.

Initially retain the existing full `rebuild_predecessor_edge_index()` operation:
when executable edge references change, clear predecessor indexes across the CFG
and rebuild them by walking all executable edges. Even an argument-only edge
replacement incurs this graph-wide pass. Automatic detection avoids the pass
when edge references are unchanged; it does not make maintenance incremental.
Do not add per-destination removal/insertion machinery in the initial extraction
or materializer cutover. Consider incremental maintenance only if measurements
justify its extra edge-identity and ordering bookkeeping.

Cheap structural preconditions reject foreign blocks, repeated updates, and
stale generations. Debug auditing additionally checks unique instruction
placement, reference availability in the resulting graph, and CFG structure. There are no callbacks or
graph queries during installation. Commit is coherent publication, not a
recoverable rollback transaction: expected allocation failures occur during
planning; malformed realized edits are compiler bugs. Allocation products must
not be queried as though they describe the committed generation.

This boundary follows the existing `StagedBlockRewrite` and commit tail in
[`graph_rewriter.cpp`](../src/jit/graph_rewriter.cpp). The rewriter keeps its
callback dispatch, allocation tracking, erasure and replacement contracts,
normalization maps, parameter conversions, removed-instruction poisoning, and
rewrite summary. The materializer
keeps bundles, occurrences, carrier bindings, copy ordering, and locations. The
shared layer knows none of those semantics. Edge splitting is prepared by the
client as blocks, edges, and sequences; it is not a normalization operation.

Do not add `replace_all_uses`, carrier redirection, or arbitrary rewrite callbacks
to this layer. Share structural commit responsibilities rather than creating a
second general rewrite engine. Whether allocation helpers live on the mutation
object or a separate factory remains an API-layout choice within this boundary.

## Realizing Planned Edits and Reusing Instructions

1. Capture the input generation in the script. Prepare only helper block
   skeletons selected by planning; retain all original blocks and parameters.
2. Walk block scripts in instruction order, emitting their already ordered
   entry, instruction, and terminator preludes. Resolve operands through the
   script-definition table. Use
   [`rebuild_instruction_with_references()`](../src/jit/instruction_reconstruction.h)
   with `ReuseIfUnchanged` to retain instructions whose resolved references agree.
3. On the first changed sequence element, initialize a staged vector from the
   unchanged prefix, then append retained and new IDs. Do not eagerly copy the
   instruction vector of every block. Omit replaced IDs from the final sequence
   without recording a retirement list or poisoning their storage entries.
4. Resolve outgoing arguments independently by position. Retain an edge when
   its endpoints and arguments are unchanged; otherwise allocate its replacement.
   Current `BlockEdge` has no mutation setters. Reconstruct the source terminator
   when its operand or edge references change. Only helpers require new parameter
   columns. Stage block order when needed; the mutation layer derives
   predecessor-index maintenance from staged terminators.
5. Finalize locations against realized IDs, audit the realization in debug/test
   configurations, and commit the staged edits. Backend observation and emission
   use the mutated CFG and these locations.

Instruction reuse depends on reference identity, not directly on allocation
splitting. A producer can survive a split while added transfers create other
carriers. An unsplit result can still require a reconstructed producer because
one of its operands now uses a reload or fixed copy. That producer's new ID then
propagates through its explicitly scripted consumers. Original block parameters
retain their IDs, so changing an incoming argument does not force rebuilding
the destination body. CFG locality prevents arbitrary cross-block SSA uses.

Reuse the existing schema-driven reconstruction helper rather than duplicating
instruction constructors. It supports operand-index-specific resolution, so
duplicate operands can select different carriers. Terminator edge attributes
must resolve to the exact retained or replacement edges.

Side-exit regions remain storage-owned with region-local parameters and bodies.
Changing the carrier arguments supplied by an owner does not change the region
and does not require cloning it. This is already covered by
`RewritesSideExitArgumentsWithoutChangingInputs` in
[`test_jit_graph_rewrites.cpp`](../tests/test_jit_graph_rewrites.cpp). A reconstructed owner can retain its original
`SideExitRegionId`, preserving existing sharing. This replacement does not change
transition-only region bodies; physical allocation occurrences belong to the
main CFG. A future transformation of region bodies would need its own explicit
reference and sharing contract.

## Block-Exit and Edge Transfer Placement

`BlockExit` is a logical allocation position, not a universal executable slot.
Planning chooses placement according to the source terminator:

- For an unconditional branch, place the block-exit program before the jump.
  Its outgoing arguments resolve through the resulting carriers. Emit each
  logical exit transfer once in the source block.
- For a conditional branch, expand each block-exit transfer onto the outgoing
  edges that require it. Keep comparison and branch together: their emission
  consumes register sources and deliberately avoids exposing flag-register
  lifetime. Do not move exit transfers ahead of that combined operation.
- Edge-specific reconciliation follows the source-side exit program. It remains
  attached to its particular original edge, independently of exit provenance.

```text
unconditional:
    source: exit program; jump --> [edge reconciliation helper] --> destination

conditional:
                         helper T: exit realization(s); edge program --> T
    compare + branch --<
                         helper F: exit realization(s); edge program --> F
```

For a conditional, either or both outgoing edges may need splitting solely for
block-exit work, even without a scheduled edge-specific physical transfer. Each
edge receives the subset of exit transfers needed by its outgoing argument
occurrences, not a blind copy of every source-block transfer. A helper performs
ordered exit and edge phases; these are sequential phases, not one parallel
assignment. A later phase can use the carriers established by the earlier one.

The initial policy uses helpers for edge-local programs that emit physical
moves. Pure no-op bindings alone do not require splitting an edge and remain
explicit in the script. For an unconditional branch, exit moves alone do not
require a helper; an edge reconciliation program may still require one.

A helper's parameters receive source-side carriers and its outgoing edge supplies
the selected destination carriers. It inherits its source's loop depth. Record
incoming bundle/location and outgoing bindings separately for every argument,
preserving duplicate positions and intentional aliases. Helpers use their own
parameters, never predecessor-local definitions directly.

Planning records exact definition bindings on both sides of every helper:

```text
source -> helper:
    incoming_arguments[i] -> helper parameter definition[i]

helper -> destination:
    outgoing_arguments[i] -> original destination parameter[i]
```

Each helper parameter has its own script definition recorded in
`EdgeParameterScript`, together with its incoming bundle and location for
validation. The incoming argument names a definition available in the source
at its terminator, after any source-local exit program. The outgoing argument
names a helper parameter or a transfer result available after all helper phases.
Both vectors preserve argument order and duplicates. Shared incoming definitions
do not collapse positional helper parameters.

Bundle IDs may remain as validation provenance, but neither outgoing arguments
nor their physical carriers are chosen by looking up bundles during execution.
Planning resolves the bindings once. The executor only translates their script
definition IDs to actual instruction IDs and constructs the edges. An edge with
no helper similarly records exact source-local arguments directly for its
original destination; it has no fictitious helper parameters or helper-local
bindings.

For the duplicate-edge alias case, both incoming positions may name the same
source definition. Inside the helper, the moved outgoing position names the
move's result definition and the unchanged position names an existing helper
parameter definition. Later phases may overwrite locations, so outgoing bindings
must describe the final valid carriers rather than an earlier intermediate state.

For example, one original exit transfer `A -> B` needed on both conditional
successors expands to `(exit transfer, true edge)` and `(exit transfer, false
edge)`. The true helper may then perform `B -> C`, while the false helper performs
`B -> D`. The schedule entry is accounted for once; each required realization
must occur once. If one successor does not need `B`, there is no exit realization
for that successor. A missing required realization and an extra realization on
an unrelated edge are both validation failures.

This is planner-owned placement, not an executor or rewrite-engine decision.
Further placement optimization may avoid helpers where legal, but must preserve
carrier selection, conditional exit-placement rules, and realization accounting.

The exit-placement rule does not prohibit ordinary `BeforeInstruction(branch)`
work between a comparison and its conditional branch. Such a transfer may break
adjacency and disable the emitter's comparison/branch peephole. The normal
boolean-producing comparison followed by a branch on that boolean remains a
valid fallback. Accept this optimization loss; do not hoist required transfers
across the comparison or add allocation/scheduling constraints solely to preserve
fusion.

## Representative Carrier Split

For an original value `%17` that must remain in `d3`, be stored for a side
exit, and be copied into the `BoxF64` ABI input `d0`, the script can establish
three bundles at their scheduled boundaries:

```text
original:
    %17 = add_f64 ...
    %18 = box_f64 %17

materialized:
    %17 = add_f64 ...        ; retained ordinary carrier in d3
    %204 = store_stack %17   ; side-exit carrier in spill[-4]
    %205 = move_f64 %17      ; call operand carrier in d0
    %206 = box_f64 %205
```

The side-exit argument used by the transition program resolves through the
bundle carried by `%204`; the `BoxF64` operand occurrence resolves through the
bundle carried by `%205`. Neither transfer globally replaces `%17`. The producer
is retained when its resolved operands are unchanged; `BoxF64` is reconstructed
for its selected input.

## Prelude Carrier Reservations

Materialization copies execute before their owning instruction. Their
destinations must therefore be represented in allocation liveness; a script
cannot safely copy into a location that the allocator considers available only
at the instruction's Late boundary.

### Fixed operand copies

Each `FixedOperandCopy` creates a conditional physical-register reservation for
the interval `[Early, Late)`. The reservation excludes every live bundle and
temporary except the bundle containing that copy's source occurrence:

```cpp
struct FixedOperandCopyReservation
{
    InstructionId instruction;
    OccurrenceId permitted_source;
    PhysicalRegister destination;
    LivenessRange range;
};
```

If the source bundle is assigned the destination register, the operand already
has its required carrier and no copy is emitted. Otherwise the reserved
register is guaranteed not to hold another Early operand, and the script may
copy the source into it before the instruction. Constraint validation rejects
two fixed-copy operands requiring the same destination for different semantic
values; identical values may share the one carrier. Use the compatible-request
grouping described above, preserving one binding and accounting record per
original operand-copy request even when only one physical copy is needed.

This reservation is separate from the instruction's Late clobber. The
reservation protects the copy at instruction entry; the clobber permits the
instruction to destroy it after its Early use.

### Reused inputs

`SameAsInput` must be supported by the completed design. Its contract is an
Early ProgramValue input and a Late result. Constraint validation rejects other
timing combinations until a concrete instruction establishes a need for them.

Successful affinity coalescing requires no materialization: the dying input and
result already occupy one physical carrier. Same-as-input affinities are
processed before ordinary block-edge affinities. When one cannot be coalesced,
the result's physical live range is extended back to the instruction's Early
boundary and given an instruction-spanning carrier occurrence. This
pre-definition portion reserves the eventual result location while containing
the copied input bits:

```text
reused-input carrier       [Early, Late)
actual result              [Late, NextEarly)
protected physical range   [Early, NextEarly)
```

The protected range cannot be split at Late. It forces the result bundle to
interfere with every other Early operand and temporary while keeping one
location across the copied input and destructive result. This is explicitly a
physical-carrier range: before the instruction it contains the selected input;
after the instruction it contains the result. It does not claim that the SSA
result is semantically defined at Early.

Initial bundle construction therefore proceeds in this order:

1. Build ordinary SSA live ranges and one initial bundle per range.
2. Process all `SameAsInput` affinities and coalesce the legal ones.
3. For each unresolved affinity, extend the result live range and its current
   bundle fragment to Early, add the carrier occurrence, and record the fixup.
4. Process block-edge affinities against these protected bundles.

Later normalization and pressure splitting must treat the carrier occurrence's
minimum coverage like any other irreducible occurrence coverage, so they cannot
separate the prelude carrier from the result.

Allocation also records the materialization contract:

```cpp
struct ReusedInputFixup
{
    InstructionId instruction;
    uint32_t operand_index;
    OccurrenceId source;
    OccurrenceId result;
};
```

The prepared problem represents this with
`OccurrenceKind::ReusedInputCarrier` and an
`OccurrenceAnchor::reused_input_carrier(instruction, operand_index)`. It is
attached to the result live range at Early with minimum coverage
`[Early, NextEarly)`. It is not an SSA definition and receives no
`LocationAssignment`; its coverage exists to keep the carrier and result in one
unsplittable physical range. Allocation verification checks that it belongs to
the named result live range and has a matching `ReusedInputFixup`.

Script creation then adds a pre-instruction assignment from the source carrier
to the protected result-bundle location. That assignment is planned together
with every ordinary transfer and fixed operand copy at the boundary. Its result
becomes an operand override for the destructive input; it does not become the
instruction result. Realizing the instruction subsequently assigns its
result definition to that same physical location and rebinds the result bundle.
The original source carrier remains available if its live range continues.

This sequence preserves the distinction between access timing and list order:
all boundary copies execute before the instruction, Early and Late operand
requirements are checked against their selected carriers, and the destructive
result becomes authoritative only after the instruction. The script retains
the original occurrence timing so validation can check the reused-input
contract and future timing-sensitive fixups without reconstructing it.

## Script Validation

In debug and test configurations, validate the script before realization.
Validation is a read-only symbolic execution over script-local definition IDs,
independent of the executor. It may be disabled for production compilations;
planning must still report unsupported allocation cases before commit.

It checks two classes of invariant:

1. Structural accounting:
   - every original block, parameter, instruction, terminator, and edge appears
     exactly once;
   - every generated helper corresponds to one planned transferred edge;
   - every operand, result, edge argument, and temporary occurrence is attached
     to its original anchor;
   - every original allocator transfer, fixed operand copy, and reused-input
     fixup has one accounting record with its required realization set;
   - unconditional block-exit actions have a source-local realization before
     the jump; conditional exit actions have precisely the required edge-local
     realizations, independently checked against outgoing argument occurrences;
   - every required realization is accounted for exactly once, including no-op
     bindings, and no extra realization occurs on an unrelated edge;
   - every script definition is introduced once and used only where it is
     available;
   - each helper incoming argument binds an exact source-local definition to
     the matching helper parameter; each outgoing argument binds an exact final
     helper-local definition to the matching destination parameter;
   - a definition from one block appears in another block only as an outgoing
     edge argument; helper bodies use their own parameters rather than source
     definitions directly.
2. Physical flow:
   - every operand's script definition has the required representation and
     effective operand location, derived from the allocated bundle or applicable
     copy/fixup requirement, and carries the original occurrence's symbolic value;
   - each planned move reads the symbolic value occupying its stated source
     location and establishes the same symbolic value at its destination;
   - writing a destination invalidates an unrelated previous occupant while
     preserving intentional aliases and physical no-ops;
   - instruction clobbers invalidate their registers after permitted Early
     uses, Late uses do not depend on clobbered registers, and results establish
     their assigned locations;
   - reused-input copies are present before destructive instructions and their
     result locations match the realized instruction results;
   - temporary locations satisfy their constraints but never appear as SSA
     carriers.

The validator uses symbolic value identities, not realized `InstructionId`s.
Derive each use's expected identity independently from its original occurrence's
anchor and definition in the frozen input CFG, never from the script's selected
source. Original ordinary computation results establish identities; realization
or reconstruction of those computations preserves that correspondence.
Forwarding definitions and physical copies preserve their source's identity.
Original block parameters establish block-local abstract input identities.

Derive each operand's effective location independently from the allocation and
applicable constraint/fixup records:

- Ordinary operands use their occurrence's allocated bundle location.
- A fixed-copy operand uses the destination specified by its fixed-copy request,
  which may differ from its ordinary source bundle's location.
- A reused-input operand uses the protected result-bundle location specified by
  the matching reused-input fixup; the pre-instruction carrier contains the
  original input value, not the instruction's not-yet-defined result value.

Do not infer the effective requirement from the script's selected override.
Check a copy's source against the required source carrier and its current value
before the assignment, then check the operand's selected carrier against the
effective destination at the operand's access timing. A no-op or grouped copy
still has the same per-request requirement. Copies affect only their selected
operands; later ordinary uses continue to use their own bundle requirements.

At each operand use, check both the selected carrier's physical location against
this independently derived effective requirement and its symbolic identity
against the original value.
Availability, representation, correct location, and faithful execution of moves
are necessary but insufficient: a script can consistently move the wrong value
into the right register. These checks also detect stale carrier bindings after
an unrelated value overwrites a location, without complicating the executor.

Block parameters are symbolic rebinding boundaries. Validation does not seek a
fixed point or require all predecessors of a loop to carry one symbolic
identity. Instead, it checks each incoming edge independently: the source or
helper output must carry the original edge argument occurrence's required
symbolic value and have the destination parameter's representation and assigned
physical location at the matching argument index. Check that source-side value
before rebinding it at the destination. The destination block then
starts with a fresh abstract identity for that parameter. This covers joins,
self-edges, and exception-free normal predecessors without path-sensitive
symbolic execution; normal and exception entry parameters are checked directly
against their entry constraints because they have no CFG predecessor.

Helper parameters receiving duplicate source arguments must retain their known
symbolic equality while remaining distinct positional definitions. They cannot
be treated as unrelated abstract values merely because their IDs differ. This
alias information comes from the planned incoming arguments, not from physical
location coincidence alone; unrelated values at different times can occupy the
same location. Helper parameters inherit the checked identities of their
source-to-helper arguments; helper creation is not a fresh semantic rebinding
that can hide an incorrect value. Outgoing helper arguments are checked against
the corresponding original edge argument occurrences before destination
parameter rebinding. This preserves block-local checking without whole-loop
symbolic execution.

Require a negative validator test that substitutes a different same-representation
value for an original operand while placing it at the required physical location.
The definition must be available and the planned copies individually legal, so
rejection specifically establishes the original-occurrence value check. Include
the equivalent wrong-value case on a helper's outgoing edge: representation and
destination location alone must not allow it to pass.

Also require negative validator tests for operand-local location requirements:

- Bind a fixed-copy operand to its ordinary source carrier when that carrier is
  outside the required copy destination. The value and representation are right,
  but the operand location is wrong, so validation must reject the script.
- Copy the correct value into a different register from the request's fixed
  destination and bind the operand to that copy. An available, correctly typed
  copy is insufficient when it violates the actual requirement.
- When reused-input support is implemented, bind its operand to a carrier
  outside the protected result location and require rejection even if its value
  is correct. Check that the instruction result subsequently uses that same
  protected location with its new semantic identity.

Retain positive coverage where the ordinary source stays in one location, the
selected operand uses its required copy destination, and a later ordinary use
still reads the original carrier. Include valid already-located no-op and shared
copy results, so location checking does not incorrectly demand a new move.

A realization audit checks that retained definitions keep their IDs, newly
emitted definitions receive distinct IDs, and every surviving operand and edge
argument refers to an available definition in the committed graph. Check graph
membership, block locality, and definition-before-use rather than relying on
poison bits or storage-ID validity. Locations must use the realized IDs, including
temporaries of reconstructed instructions, without depending on definitions
removed from the graph. Retained side-exit region references remain subject to
their existing region-local availability checks. Unchanged side-exit regions and their sharing remain intact.
Ordinary CFG verification checks the committed graph in debug configurations.

## Public Result and Call Flow

Keep the current result shape and mutable graph argument:

```cpp
Result<MaterializedAllocation, RegisterAllocationError> allocate_registers(
    CompilationSession &session,
    ControlFlowGraph &graph,
    const AllocationConstraints &constraints);
```

`MaterializedAllocation` returns `LocationAssignments` and managed-frame spill
extent; it does not need a fresh-graph pointer. Preparation and allocation refer
to the frozen input generation. The materializer plans, optionally validates,
realizes, and commits against that generation. Backend observation and emission
continue using `graph`, with locations finalized against retained and new IDs.
Planning failure leaves the published graph unchanged.

## Implementation Slices

The first delivery replaces materialization for allocation products supported by
production today. It extracts structural mutation before introducing the
materializer executor, keeps the old production path until the new path handles
those products completely, and then removes the old path. Rewriter simplification
and expanded destructive-input support are separate follow-ups.

```text
1. shared allocation/commit boundary; existing rewriter uses it
                          |
2. script planning for current allocation products
                          |
3. block-local realization with reuse and lazy staging
                          |
4. complete edges and side-exit integration; alias regressions
                          |
5. production cutover
                          |
6. audit and remove materializer-only rewriter functionality

Prelude reservations and unresolved SameAsInput support:
    review dependencies during planning; implement required safety reservations
    before relying on their copies; add expanded support as a separate slice.
```

Each slice leaves a reviewable, working production configuration. Before cutover,
exercise the new path through tests on separately constructed input graphs; do
not run both materializers sequentially on one mutated graph or introduce a
production fallback to the old engine. No slice publishes a partial
materialization. Instruction reuse and lazy staging must be demonstrated before
cutover, rather than postponed as optimizations.

### 1. Extract structural allocation and commit

**Boundary:** graph infrastructure only; no change to carrier selection or
rewriter semantics.

- Introduce the small mutation interface around storage-owned allocation,
  optional block updates, helper publication and block order, predecessor
  rebuilding, IR-level updates, and mutation generation. Keep poisoning out of
  this shared API; the rewriter retains its existing poisoning policy.
- Move the existing rewriter's commit responsibilities behind it. Preserve
  rewrite callbacks, normalization, replacement checks, allocation tracking,
  summaries, and staged edge-split behavior in their current owning layer.
- Keep both register allocation and the production materializer on their
  existing algorithms. Do not remove materializer-specific rewriter features yet.

**Acceptance:** existing rewrite tests pass; focused coverage checks stale
mutation generations, retained instruction IDs, unchanged vectors, the rewriter's
existing poisoning behavior, helper publication, predecessor indexes, and analysis
invalidation. The materializer path must leave replaced storage entries
unpoisoned while rejecting references to definitions absent from the result.
A no-change mutation does not advance generation or rebuild predecessor indexes.
Cover argument-only edge replacement with unchanged endpoints, helper insertion,
and changed instructions with identical terminator edges. The first two require
predecessor rebuilding; the last does not. Edge allocation without publication
must not trigger rebuilding. No caller-supplied edge-change flag is required.

### 2. Plan scripts from current allocation products

**Boundary:** read-only planning and independent validation; no production
materializer cutover.

- Extract spill resolution, transfer-source discovery, parallel-copy ordering,
  stack-copy legalization, and current fixed-copy planning.
- Build the occurrence-to-bundle index once. Resolve block-local carrier state
  into script definition IDs with exact operand bindings, result/temporary
  locations, input generation, and source-product provenance.
- Plan entry and instruction boundary programs and ordered edge phases. Place
  unconditional exit transfers before the jump; expand conditional exit work
  per applicable edge. Record source accounting separately from realization
  accounting and select helpers where edge-local physical moves require them.
  Preserve all argument
  positions and no-op assignment results, even when aliases share one carrier.
- Group compatible same-destination requests within each parallel phase before
  resolution. Map each request to the physical group result, retaining separate
  operand bindings and realization accounting. Reject conflicting destinations
  before invoking the resolver; do not coalesce requests across phases or edges.
- Add structural accounting and symbolic physical-flow validation for debug and
  tests. Keep it independent from realization and optional in production.
- Audit prelude-copy safety for the supported allocation products. If an existing
  supported copy requires a missing reservation, establish that prerequisite
  before cutover; a fresh executor cannot make an unsafe allocation correct.
  Do not silently expand the set of supported `SameAsInput` cases.

**Acceptance:** direct script tests cover unchanged blocks, ordinary/spill/ABI
carriers, fixed operand copies, ordered phases, cycles and legalization failures,
and the duplicate-edge move/no-op case below. Verify source-action coverage,
per-realization accounting, known helper-parameter aliases, and unchanged
published input on planning errors. Include a conditional exit transfer needed
on both edges, one needed on only one edge, and composition with edge-specific
transfers. Reject missing and duplicated realizations. Reject a script that
supplies the wrong symbolic value at the correct location for an operand or a
helper outgoing argument, using the original occurrence as the oracle.
Cover identical fixed-copy requests sharing one emitted move, requests sharing
an existing no-op carrier, equality established by known aliases, and shared
results through cycle/scratch legalization. Verify every per-request binding and
accounting record survives grouping. Reject different-value or incompatible-
representation requests for one destination, and keep sequential phases and
separate edges distinct. Tests must not depend on incidental bundle, register,
or instruction numbering. Derive effective operand locations from bundle and
copy/fixup records; reject fixed-copy scripts that select the ordinary source
outside the required destination or a correct-value copy in the wrong register.
Apply the corresponding protected-location negative test when reused-input
support is enabled, and retain positive ordinary-source/later-use coverage.

### 3. Realize block-local edits with reuse

**Boundary:** test-driven execution of entry and instruction programs through the
shared mutation layer; production still uses the old materializer.

- Translate planned definitions to retained or new IDs. Use the existing
  schema-driven reconstruction helper with exact operand-index bindings.
- Retain original parameters. Retain an instruction if its resolved references
  are unchanged, regardless of whether its allocation range was split.
- Propagate reconstructed result IDs through their explicitly scripted consumers;
  do not add a general replacement/normalization map.
- Start a staged instruction vector only at the first sequence change, copying
  its unchanged prefix once. Never copy all block vectors eagerly.
- Assign locations directly to retained and new results and to realized
  temporaries. Fixed copies override only their selected operand occurrences.

**Acceptance:** an unchanged input retains instruction IDs and vector storage;
locations alone do not mutate the graph. A changed block retains unaffected IDs,
reconstructs required consumers, and leaves replaced storage entries unpoisoned. Cover a
split value whose producer survives and an unsplit result whose producer changes
because an operand selects a different carrier. Multi-carrier cases preserve
still-live source carriers. Add a dependency-chain test where changing the first
instruction's operand reconstructs its transitive consumers, while an independent
instruction in the same block retains its ID. Verify that all consumer references
use the realized IDs and no definition removed from the graph remains referenced,
including when the removed storage entry is unpoisoned. Retain the
original destination parameter IDs and verify that changing the outgoing argument
does not reconstruct the destination body. All published test outputs are
complete for their supported cases.

### 4. Complete edge and side-exit realization

**Boundary:** make the test-only path handle the full current materialization
surface, including the observed abort, before switching production callers.

- Emit unconditional exit programs before their jumps. For conditionals,
  allocate helpers on every edge requiring physical exit or reconciliation
  work without inserting exit moves between comparison and branch. Test both
  outgoing paths. Separately cover required pre-branch transfers that break
  adjacency: permit fusion loss and verify the normal boolean/branch fallback.
- Allocate planned helpers with exact source-to-helper definition bindings,
  explicit parameter definitions and locations, ordered exit-then-edge phases,
  and exact helper-to-destination definition bindings. Preserve duplicate
  argument positions and intentional aliases.
- Retain unchanged edges. Allocate replacements when arguments or endpoints
  change and reconstruct only affected source terminators. Pure no-op bindings
  do not by themselves require helpers.
- Retain original destination parameter IDs across incoming-edge changes. Cover
  joins, self-edges, loops, critical edges, and mixed stack/register cycles.
- Retain side-exit regions and their sharing when reconstructing owners; resolve
  physical owner arguments through the script, including stack carriers.
- Finalize locations for the staged result and commit coherently. Keep original
  block order and entry metadata except for explicitly planned helper insertion.

**Acceptance:** the focused duplicate-edge regression passes script validation,
realization auditing, CFG verification, and location checks. The moved argument
uses a new definition; the unchanged argument may name an existing helper
parameter without a fictitious insertion output. Unrelated blocks and side-exit
regions are retained, and predecessor indexes match committed terminators.
Compile the reduced Python function directly through the JIT test path so tiering
settings cannot hide a materialization failure.

### 5. Cut over production materialization

**Boundary:** one production path for the existing supported allocation surface;
no new graph-return API and no opportunistic allocator feature expansion.

- Replace the old rewriter-driven materializer with script planning, optional
  validation, dedicated realization, and shared structural commit.
- Keep the mutable CFG argument and current `MaterializedAllocation` result.
  Backend observation and emission use that same graph with finalized locations.
- Remove the old materializer. Remove the emitter-side `BoxF64` location fixup
  only once coverage proves the new materializer supplies its ABI carrier.
- Resolve any prelude safety dependencies identified in slice 2. Unsupported
  cases retain explicit compilation failures rather than partially publishing
  edits or falling back to normalization.

**Acceptance:** run the full debug checks, JIT-enabled release regressions,
representative AArch64 dumps/execution tests, and the original bisect reproducer.
Verify equivalent behavior with independent validation enabled and disabled.
Check deterministic structural costs: no unnecessary reconstruction, vector
copying, helper allocation, or predecessor rebuilding on unchanged cases.
Record representative compilation-time and transient-memory observations before
claiming speed or memory improvements; do not conflate interpreter-only passes
with JIT verification.

### 6. Simplify the rewriter separately

After cutover, audit remaining callers of insertion transfers, edge-split
support, and other suspected materializer accommodations. Remove a feature only
when its remaining uses show it is materializer-only. Retain independently used
rewrite functionality and the shared structural mutation layer. Run the relevant
rewriter and integration checks after removal. This cleanup neither blocks
carrier correctness nor justifies a compatibility normalization layer.

### Separate allocator extension: prelude carriers and reused inputs

The full design still requires the mechanisms in Prelude Carrier Reservations.
They are not an automatic first step for replacing the materializer. Required
reservations for already supported copies are safety dependencies of cutover;
expanding unresolved destructive-input support remains independently reviewable.

- Restrict `SameAsInput` to an Early input and Late result.
- Add conditional `[Early, Late)` fixed-copy reservations permitting only the
  source bundle to occupy the copy destination.
- Process same-as-input affinities first; extend unresolved result ranges to
  Early with `ReusedInputCarrier` occurrences and record `ReusedInputFixup`s.
- Preserve instruction-spanning coverage through constraint and pressure
  splitting. Extend allocation verification for reservation conflicts,
  carrier/fixup correspondence, and unsplittable coverage.
- Extend script planning and realization for these fixups, using the existing
  boundary-program and operand-binding contract rather than another engine.

Acceptance includes another Early operand occupying the preferred result
register, a reused source remaining live, already correctly located fixed-copy
sources, and conflicting fixed-copy destinations. Each destructive instruction
must also establish compatibility with its side-exit recovery contract before
being enabled. Keep this work distinct from the duplicate-edge alias repair.

## Deferred Optimizations and Cost Verification

Retaining unchanged instructions and lazy block updates are baseline
requirements, not deferred optimizations. Further work may improve transfer
placement in existing blocks and remove redundant moves or helpers. Those
optimizations must preserve scripted carrier selection and occurrence identity.

Measure materialization time, retained and reconstructed original instruction
counts, newly emitted transfer instruction counts, helper/edge counts, and
transient memory on representative small and larger JIT compilations before
claiming a performance improvement. Distinguish reconstructed consumers from
new transfer definitions; allocation split counts are not a proxy for actual
reconstruction cost. Measure predecessor-index rebuild time and rebuild counts
separately from planning and instruction realization, including a single
argument-only edge replacement in a larger graph and unchanged-edge cases where
no rebuild should occur. Include unchanged graphs, long dependency chains with
one changed input, and graphs with side exits. Dense script storage and arena allocation may reduce overhead,
but the script still incurs planning, initialization, and traversal costs.
Do not introduce a separate heap allocation for every conceptual script node
merely because the vocabulary sketch uses nested vectors.

## Verification

Tests should establish:

- planning and staging leave published block sequences and edges unchanged;
- unchanged instructions retain their IDs, unchanged blocks retain their
  instruction vectors, and locations-only materialization does not mutate CFG
  structure or advance its generation;
- reconstructed instructions and transfers receive new IDs, with every affected
  consumer resolving to its planned carrier; a changed input propagates through
  its dependency chain while independent instructions retain their IDs, and
  original parameter boundaries stop reconstruction spreading between blocks;
- normal and exception entries, loops, edges, parameters, and side-exit region
  references are preserved;
- unchanged side-exit regions and their sharing survive owner reconstruction;
- each operand occurrence resolves to its planned script definition at the
  independently derived bundle or applicable copy/fixup location, carrying the
  independently derived original symbolic value; wrong-value/correct-location scripts fail validation for
  ordinary operands and outgoing helper arguments;
- transfers create destination carriers without destroying still-live source
  carriers;
- fixed operand copies and reused-input copies affect only their selected
  operands, and destructive results reuse the copied location;
- prelude-copy destinations cannot overwrite a different Early operand or
  temporary, while an already correctly located copy source remains legal;
- block-entry and pre-terminator boundary programs are neither dropped nor
  emitted at an adjacent instruction;
- unconditional exit transfers execute before the jump without forcing helper
  allocation; conditional exit transfers expand onto exactly the applicable
  outgoing edges without inserting exit work between comparison and branch;
- an eligible adjacent comparison/branch pair still uses fused emission;
  a required pre-branch transfer may break adjacency and must produce correct
  boolean-producing comparison and branch fallback, including both branch
  outcomes, without hoisting the transfer merely to preserve fusion;
- source actions and their required realizations are accounted for separately;
  both conditional paths compose exit work with edge-specific reconciliation;
- edge transfers remain path-specific on branches and compose correctly at
  joins and loops;
- stack cycles use legal scratch sequences;
- `LocationAssignments` mention retained or new definitions in the committed
  graph, with locations for all emitter-visible physical definitions and
  temporaries and none relying on definitions absent from the committed graph;
- helpers and replacement edges are allocated only when needed; mutation-layer
  comparison of staged terminator edges detects argument-only replacements and
  helper insertion without a caller flag; unchanged executable edge references
  do not trigger predecessor rebuilding, while changed references initially use
  the established full-graph rebuild;
- commit rejects stale input generations and invalidates analyses through
  mutation generation; materialization leaves replaced entries unpoisoned,
  while the rewriter preserves its existing poisoning behavior;
- reference validation rejects operands and edge arguments naming definitions
  absent from the resulting graph even when their storage IDs remain readable;
- backend observation and emission consume the same materialized graph object;
- failed planning leaves the published graph unchanged;
- independent validation may be disabled without changing materialized behavior.

Adversarial allocation tests include a destructive result whose Late register
would otherwise alias another Early operand, a reused source that remains live,
duplicate fixed-copy destinations, block-entry reloads, return-ABI transfers,
and loops whose backedge carries a different symbolic identity into one block
parameter.

The existing allocation materializer and AArch64 execution tests provide the
behavioral cases. New script tests should additionally make planning failures
and exact carrier selection visible without relying on graph-rewriter
normalization.

### Required duplicate-edge alias regressions

Retain this reduced Python reproducer as end-to-end regression coverage for
the JIT abort investigated at `f064e71f58c121307406c923c8e82ce72e5b56d8`
on 2026-10-25:

```python
def f(x, lo, hi):
    while lo < hi:
        mid = lo + hi
        if x < mid:
            hi = mid
        else:
            lo = mid + 1
    return lo

for i in range(101):
    assert f(100, 0, 2) == 3
```

The test must exercise JIT compilation and complete with the asserted result.
The 101 calls trigger the current tiering budget; adapt the harness if that
budget changes so an interpreter-only pass cannot satisfy the regression.
Also compile the function directly through the JIT test path: the failure
occurs during materialization, before generated code executes. A debug VM run
with ordinary JIT tiering disabled does not cover it.

Require a focused script and graph-mutation regression for the underlying
duplicate-edge alias case. In `bisect._right`, the `hi = mid` edge passes `mid`
twice, once as the new bound and once as the retained temporary. The observed
allocation gives both incoming helper parameters bundle `b38` in `x6`, but
their outgoing arguments require `b31` in `x13` and `b38` in `x6`, respectively:

```text
incoming: p3(b38, x6), p10(b38, x6)   ; same semantic value
body:     moved = move p3             ; b31, x13
outgoing argument 3:  moved           ; b31, x13
outgoing argument 10: p3              ; b38, x6, physical no-op
```

Register numbers, bundle numbers, and parameter positions describe the
observed failure; tests must establish the alias/move/no-op relationship
without depending on incidental allocator numbering. Verify that:

- both incoming argument positions and their intentional alias survive;
- the moved argument receives a new carrier at its destination while the
  unchanged argument retains a valid carrier at the original location;
- the no-op assignment may reuse an existing helper-parameter definition
  without inventing a move or claiming that parameter was newly emitted;
- incoming helper arguments and outgoing destination arguments name exact
  script definitions by position, with no executor-side bundle selection;
- outgoing edge arguments resolve independently to those selected carriers;
- script validation, committed CFG verification, and location assignments agree;
- planning and staging preserve the input generation until coherent commit,
  retaining unrelated instructions and structure.

The old materializer collapses the two parameters through its bundle-to-value
map, then treats the second parameter's substitution with the first as an
insertion transfer. The first parameter was not emitted by that insertion,
so the graph rewriter correctly rejects it. Required coverage must demonstrate
correct carrier binding rather than suppressing that invariant. Keep these
cases as cutover acceptance criteria; recording them here does not implement
or authorize a repair to the current materializer.
