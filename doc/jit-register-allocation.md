# JIT Register Allocation

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | Target register vocabulary, structural constraint validation, and the initial AArch64 platform-ABI constraint producer implemented |
| Scope | Allocation constraints, allocator-local numbering, liveness, bundles, backtracking allocation, live-range splitting, block-argument moves, clobbers, spills, and post-allocation location assignments |
| Owning layers | Target backends own allocation-constraint construction; the generic register allocator owns numbering, liveness, bundles, splitting, allocation, spill decisions, and allocator-induced move resolution; publication and recovery planners own canonical-state synchronization; machine-code emission consumes final assignments and resolved moves |
| Validated against | `tests/test_jit_allocation_constraints.cpp`, `tests/test_aarch64_allocation_constraints.cpp` |
| Supersedes | The open register-allocation direction in [JIT Compiler and IR](jit-compiler-and-ir.md) and [JIT Compiler Bring-up Plan](jit-compiler-bring-up-plan.md) |

This document defines the register-allocation contract for the clovervm JIT. It
fits the Core IR and CFG contracts in [JIT Compiler and IR](jit-compiler-and-ir.md)
and [JIT Control-Flow Graph](jit-control-flow-graph.md). Core IR remains a
target-independent SSA CFG with block parameters and edge arguments. Target
backends describe physical requirements through `AllocationConstraints`; the
generic allocator computes liveness, splits ranges, assigns locations, and
produces `LocationAssignments`.

The allocator is target-independent. It may know about register classes,
physical registers, stack slots, clobber masks, operand access timing, and
operand constraints, but it does not know the semantics of AArch64, x86-64, or
any concrete Core instruction kind. Unlike a fully IR-independent allocator,
it may consume clovervm's generic Core and CFG interfaces directly: blocks,
SSA defs and uses, block parameters, edge arguments, Snapshots, and structural
occurrence anchors are part of the common compiler contract.

## Phase Products

The allocation pipeline has three main products:

```text
BackendPreparation
    selected lowerings, constant decisions, and target constraints

AllocationConstraints
    target-produced pre-allocation requirements for operand occurrences

LocationAssignments
    allocator-produced post-allocation locations and move bundles
```

`AllocationConstraints` replace the earlier `LocationSummary` terminology. They
describe requirements, not chosen locations.

`LocationAssignments` are the durable post-allocation result. They map program
value occurrences to registers, spill slots, constants, or other
backend-supported physical locations at the points where code emission or
recovery needs them. They also contain allocator-induced split moves and
block-edge parallel-move bundles. Canonical VM homes and whether they currently
contain an up-to-date value remain separate state; they are not silently
converted into allocator-owned spill slots.

Allocator-local position numbers, live intervals, split children, spill weights,
and coalescing state are scratch data. They do not survive the allocator pass
and are not stored in Core IR, backend preparation, or machine-code emission
metadata.

## Target Register Vocabulary

The target-independent allocator uses a small common vocabulary for register
storage while leaving register availability and encoding policy to the target:

```cpp
enum class RegisterClass : uint8_t
{
    GPR,
    SIMD,
    Count,
};

class PhysicalRegister
{
public:
    constexpr PhysicalRegister(RegisterClass register_class, uint8_t number)
        : register_class_(register_class), number_(number)
    {
    }

    constexpr RegisterClass register_class() const
    {
        return register_class_;
    }

    constexpr uint8_t number() const { return number_; }

    friend constexpr bool
    operator==(PhysicalRegister, PhysicalRegister) = default;

private:
    RegisterClass register_class_;
    uint8_t number_;
};

class RegisterSet
{
public:
    static constexpr size_t MaxRegistersPerClass = 64;

    bool contains(PhysicalRegister reg) const;
    void insert(PhysicalRegister reg);
    void erase(PhysicalRegister reg);

private:
    std::array<std::bitset<MaxRegistersPerClass>,
               static_cast<size_t>(RegisterClass::Count)>
        members_{};
};

class RegisterClassDefinition
{
public:
    RegisterClassDefinition(
        RegisterClass register_class,
        std::span<const PhysicalRegister> allocation_order);

    RegisterClass register_class() const;
    const RegisterSet &members() const;
    const std::vector<PhysicalRegister> &allocation_order() const;

private:
    RegisterClass register_class_;
    RegisterSet members_;
    std::vector<PhysicalRegister> allocation_order_;
};
```

`RegisterClass` identifies the architectural storage class. `GPR` and `SIMD`
are initially sufficient; `Count` is an implementation sentinel rather than a
class. Another target need not provide definitions for classes it does not use,
and future storage classes may extend the enum.

`PhysicalRegister` identifies one indivisible allocation unit by class and
target-local number. Architectural width views are not separate physical
registers:

```text
AArch64 X0 / W0       -> { GPR, 0 }
AArch64 V0 / D0 / S0  -> { SIMD, 0 }
x86-64 RAX/EAX/AX/AL  -> { GPR, 0 }
x86-64 XMM0/YMM0/ZMM0 -> { SIMD, 0 }
```

The emitter converts a `PhysicalRegister` to the target instruction's required
view. A partial-width x86 definition still occupies and conflicts on the whole
GPR. If the preserved upper bits are semantically part of the result, the
lowering exposes the old destination as a use and constrains the result to the
same register. Otherwise the result owns the whole allocation unit and any
unwritten upper bits are unspecified until an explicit extension. Legacy x86
high-byte registers are not exposed to the allocator.

One `RegisterClassDefinition` enables and defines a class for an allocation
attempt. Its constructor copies the target's allocation order into owned
storage and derives `members` from it, making it impossible for the two views
to disagree or for the order to outlive its backing storage. The
order contains every allocatable physical register exactly once and excludes
stack pointers, reserved registers, and other unavailable locations. It is the
target's default priority order when several registers are equally legal.
Fixed constraints, bundle affinities, and later cost heuristics may override
that preference. A `RegisterSet` may contain registers from several classes,
which keeps large call-clobber masks compact. The initial contract permits at
most 64 physical registers in each class.

## Allocation Constraints

An allocation constraint is anchored to a structural occurrence in prepared
Core, not to a numeric program position. Target-authored constraints are
grouped by instruction:

```text
InstructionAllocationConstraints
    input overrides
    result override
    temporary constraints
    clobber masks
```

Parameters are instructions, so function-entry and block-parameter constraints
remain instruction-anchored. Block-edge argument transfers and affinities are
derived generically from first-class `BlockEdge` objects rather than supplied
as target constraints.

The anchor determines the SSA value and whether the occurrence is a use or a
definition:

```text
Instruction input      -> use of that input's ProgramValueRef
Instruction result     -> definition of that result's ProgramValueRef
Block edge argument    -> use at the source edge
Block parameter        -> definition at target block entry
Instruction temporary  -> non-SSA value occupying both timing phases
Instruction clobber    -> physical register mask
```

Access kind and access timing are independent:

```text
access kind     Use | Def
access timing   Early | Late
```

Ordinary inputs are early uses and ordinary results are late defs. The
separation permits a selected multi-instruction lowering to expose a late use,
an early def, or a temporary spanning both phases without lowering completely
to Machine IR. A deferred emitter choice is legal only when every possible
encoding obeys the prepared timing, temporary, and clobber contract.

A temporary spanning the selected sequence is conceptually an early def and a
late use of the same allocator-local temporary, reserving its register across
both phases. A multi-instruction lowering may instead give an ordinary input a
late use when the sequence does not finish consuming it until after other early
actions. Likewise, an output written before every input has been consumed is an
early def. `Use` therefore never implies `Early`, and `Def` never implies
`Late`.

The common layer derives ordinary register requirements directly from Core
`ValueRepresentation`:

```text
TaggedValue input  -> Use Early, Any(GPR)
F64 input          -> Use Early, Any(SIMD)
TaggedValue result -> Def Late, Any(GPR)
F64 result         -> Def Late, Any(SIMD)
Snapshot operand   -> captured values used Late
```

Target constraints are sparse overrides of these defaults. An instruction with
ordinary inputs and an ordinary result needs no target-authored constraint
object. Parameters, returns, calls, reused-input operations, multi-instruction
lowerings, and instructions needing temporaries or clobbers provide only their
exceptional requirements.

The structural constraint representation is:

```cpp
enum class AccessTiming : uint8_t
{
    Early,
    Late,
};

class RegisterRequirement
{
public:
    enum class Kind : uint8_t
    {
        Any,
        Fixed,
        SameAsInput,
    };

    static RegisterRequirement any(RegisterClass register_class);
    static RegisterRequirement fixed(PhysicalRegister reg);
    static RegisterRequirement same_as_input(uint32_t operand_index);

    Kind kind() const;
    RegisterClass register_class() const;
    PhysicalRegister fixed_register() const;
    uint32_t input_index() const;

private:
    RegisterRequirement(Kind kind, uint32_t payload);

    Kind kind_;
    uint32_t payload_;
};

struct ProgramValueUseConstraint
{
    ProgramValueUseConstraint(uint32_t operand_index, AccessTiming timing,
                              RegisterRequirement requirement);

    uint32_t operand_index;
    AccessTiming timing;
    RegisterRequirement requirement;
};

struct ResultConstraint
{
    AccessTiming timing;
    RegisterRequirement requirement;
};

struct TemporaryConstraint
{
    explicit TemporaryConstraint(RegisterRequirement requirement);

    RegisterRequirement requirement;
};

class InstructionAllocationConstraints
{
public:
    InstructionAllocationConstraints(
        const Instruction *instruction,
        std::vector<ProgramValueUseConstraint> input_overrides = {},
        std::optional<ResultConstraint> result_override = std::nullopt,
        std::vector<TemporaryConstraint> temporaries = {},
        RegisterSet clobbers = {});

    void validate() const;

    const Instruction *instruction() const;
    const std::vector<ProgramValueUseConstraint> &input_overrides() const;
    const std::optional<ResultConstraint> &result_override() const;
    const std::vector<TemporaryConstraint> &temporaries() const;
    const RegisterSet &clobbers() const;

private:
    const Instruction *instruction_;
    std::vector<ProgramValueUseConstraint> input_overrides_;
    std::optional<ResultConstraint> result_override_;
    std::vector<TemporaryConstraint> temporaries_;
    RegisterSet clobbers_;
};

ProgramValueUseConstraint default_program_value_use_constraint(
    uint32_t operand_index, ValueRepresentation representation);
ResultConstraint
default_result_constraint(ValueRepresentation representation);
constexpr AccessTiming default_snapshot_use_timing();
```

Backend preparation stores constraint objects only for instructions with at
least one override, temporary, or clobber, so ordinary instructions allocate no
collection storage and sparse objects do not pay for speculative inline
capacity. Read-only accessors prevent later mutation from invalidating a
completed check. Debug constructors call `validate()` automatically. Release
constructors only store the sparse data; compiler stages may call `validate()`
explicitly when they need an exhaustive check.

The backend publishes the enabled register-class definitions and sparse
instruction overrides together as one read-only `AllocationConstraints`
product. Overrides remain in CFG traversal order. The allocator combines this
product with the common representation-derived defaults and generic block-edge
and Snapshot rules.

`AllocationConstraints` are valid for the exact published graph generation
from which the backend produced them. The CFG must remain frozen until
allocation finishes; rewriting the graph first invalidates the product and
requires rebuilding it. This phase contract avoids permanent placement metadata
or per-access generation checks in the allocator.

`RegisterRequirement::Any` names a register class;
`RegisterRequirement::Fixed` names one physical register; and
`RegisterRequirement::SameAsInput` names the ProgramValue input whose assigned
register the result must reuse. Contextual constructors reject
`SameAsInput` for inputs and temporaries, so it remains a result-only
requirement without a second variant-based representation. The compact
allocator representation may encode these alternatives differently.

The allocator does not allocate a `SnapshotRef`. At each executable instruction
that consumes one, allocator preparation expands the captured
`ProgramValueRef`s into unconstrained late point uses. Repeated consumers of one
virtual Snapshot expand independently.

The heterogeneous ProgramValue references stored by the virtual `Snapshot`
instruction itself have no direct input constraints at the Snapshot's
definition position. They become allocation uses only through this expansion at
each Snapshot-consuming instruction.

A temporary takes either an `Any` or `Fixed` register requirement and reserves
the chosen register across the selected target sequence. `clobbers` instead
describes registers destroyed implicitly by the operation. Structural
preparation retains the compact register set. Allocator preparation expands
each member into a late def of a fresh throwaway value, with no uses and a fixed
constraint naming that physical register.

Every physical register written by an instruction must be represented either
by an explicit def, including an allocated temporary, or by a clobber, but not
both. A clobber may coincide with a fixed early use, but it may not collide with
a fixed def or fixed late use. This is the
[`regalloc2` clobber contract](https://docs.rs/regalloc2/latest/aarch64-unknown-linux-gnu/regalloc2/trait.Function.html#tymethod.inst_clobbers).

A potentially long branch therefore requests a GPR temporary; it does not
needlessly clobber a predetermined register. The emitter may ignore the
assigned temporary when the short branch form fits.

Register requirements and spill compatibility must agree with the Core
`ValueRepresentation` of the value. A constraint may narrow that representation
to a target class or fixed register, but it must not change representation
semantics. Representation changes remain explicit Core instructions.

## Initial AArch64 Bring-up Contract

The first AArch64 constraint producer temporarily follows the platform ABI.
This is a bring-up choice, not the final CloverVM calling convention:

- the enabled GPR class contains `x0` through `x15`, in that allocation order;
- the enabled SIMD class contains caller-saved `v0` through `v7` followed by
  `v16` through `v31`;
- `x16` and `x17` remain unavailable until all branch and call scratch
  requirements are represented;
- platform-reserved `x18` is unavailable;
- callee-saved GPRs and `v8` through `v15` remain unavailable until prologue
  and epilogue generation preserves them;
- tagged entry-block parameters zero through seven have fixed result
  constraints `x0` through `x7`;
- tagged internal block parameters use the ordinary `Any(GPR)` default;
- F64 internal block parameters use the ordinary `Any(SIMD)` default;
- a `Return` input has a fixed `x0` constraint;
- conditional and unconditional branches request one `Any(GPR)` temporary for
  a possible long form;
- `Const`, SMI bitwise instructions, and the virtual `Snapshot` instruction
  need no target override.

Stack-passed entry parameters, F64 entry parameters, calls, and instruction
kinds without a bring-up lowering hard-fail instead of silently receiving an
incomplete contract. Later CloverVM ABI work replaces the entry and return
overrides without changing the generic constraint representation.

Constraint validation enforces:

- each input override names one allocatable ProgramValue operand, and no
  occurrence has two overrides;
- a result override occurs only on a ProgramValue-producing instruction;
- `SameAsInput` names a valid ProgramValue input and occurs only on a result;
- fixed registers and `Any` classes are compatible with the occurrence's
  `ValueRepresentation`;
- every `RegisterClassDefinition::allocation_order` is a permutation of its
  members;
- clobbers do not collide with explicit fixed defs, fixed late uses, or fixed
  temporaries, including the fixed register obtained after resolving
  `SameAsInput`.

Parameter instructions use the same default result constraint, with target
overrides for ABI-fixed entry parameters. Their placement in a block's
parameter list anchors the def at block entry rather than at an executable
instruction phase.

Instruction records may retain a narrower packed operand count, but traversal,
constraint APIs, and operand-index arithmetic use `uint32_t`. Widening at the
representation boundary avoids narrow-integer wrap semantics in ordinary
compiler loops.

## Durable Anchors and Ephemeral Positions

Constraints are durable within a backend preparation and allocation attempt
because they are anchored to instructions, blocks, block parameters, and block
edges. They are not anchored to integer positions.

Immediately before allocation, the allocator builds an ephemeral numbering from
the current prepared CFG order. The numbering uses two positions per executable
instruction and two positions for each relevant block boundary:

```text
position = 2 * index + phase

phase 0: before
phase 1: after
```

Early operands occur at `before`; late operands occur at `after`. Access kind
does not determine timing. This lets a fixed-register call argument use and a
fixed-register call result definition share the same physical register at
different phases of the same instruction. It also makes same-as-input and
destructive-operation lowering explicit without treating two SSA values as one
value.

Block entry and block exit each provide use and definition phases. This gives
block parameters, edge arguments, and edge moves stable allocation points
without making integer positions durable:

```text
predecessor block exit use   -> edge arguments are live here
successor block entry def    -> block parameters are defined here
```

Allocator numbering, liveness, live ranges, bundles, and partial assignments are
local scratch state. The allocator does not publish or incrementally maintain a
position map.

## Live Ranges and Bundles

A live range records where one SSA value needs storage. One `ProgramValueRef`
may have several discontiguous ranges in linear program order because of CFG
structure. Block arguments end predecessor ranges at edge exits; successor
block parameters begin new SSA ranges at block entry.

A bundle is the allocator's unit of assignment: a set of non-overlapping live
ranges that should receive one location. Bundles recover physical continuity
between distinct SSA values without changing their semantic identities. The
allocator initially attempts to merge ranges related by:

- block-argument transfers;
- explicit machine-value moves;
- reused-input constraints;
- other backend-declared allocation equalities.

Bundle merging is set union subject to the invariant that two ranges in one
bundle never overlap. Repeating an already completed merge is a no-op.
Splitting a bundle creates smaller bundles but does not change the underlying
SSA ranges.

Allocation priority and eviction weight are distinct. Priority determines which
bundle is processed next. Weight determines whether an unallocated bundle may
evict conflicting allocated bundles. Both are recomputed for children created
by splitting. Initial weights may consider use frequency, loop depth,
fixed-register pressure, Snapshot/recovery uses, and rematerialization cost.

## Constraint Normalization and Fixups

The allocator normalizes awkward occurrence constraints into live ranges plus
deferred fixup moves before its core assignment loop.

If one SSA value is required in two fixed registers at the same instruction,
one constrained occurrence remains on the range and the other becomes a fixup
copy at that occurrence. A reused-input result is treated as a new range
starting at the input phase, with a fixup copy from the input and a high-priority
merge opportunity between the two ranges. If they can share a location the
copy disappears; otherwise it remains part of the final move set.

This normalization preserves the invariant that a live-range fragment occupies
one location at a point. It keeps special instruction shapes at the boundary of
the allocator rather than complicating every bundle-placement decision.

## Initial Algorithm

The first general allocator is an SSA bundle-based backtracking allocator. It
uses linear program positions to build liveness and ranges, but it does not
allocate in ordinary linear-scan order.

The allocator runs over prepared IR:

```text
build ephemeral positions
compute precise allocation liveness
build live ranges and attach constrained occurrences
normalize constraints and record fixups
merge related non-overlapping ranges into bundles
enqueue bundles by allocation priority
assign a fitting register, evict lower-weight bundles, or split
spill when splitting or register allocation is no longer legal or worthwhile
collect split, fixup, explicit, and block-edge moves
resolve parallel moves
produce LocationAssignments
```

Assignment guarantees forward progress by evicting only lower-weight bundles
and reducing weight when a bundle is split. A useful first split point is the
first conflicting constrained use or the first point at which a candidate
register ceases to fit. If no legal assignment remains for an unspillable
single-use bundle, compilation fails rather than emitting incorrect code.

Loop-aware split placement is later code-quality tuning. Once the basic
allocator is producing inspectable code, measure whether connectors, canonical
stores, or reloads are being placed inside hot loops. If that is material,
prefer split points with cheaper connecting moves and hoist boundaries outside
loops when legal. The initial allocator does not need this heuristic.

Fixed-register constraints and clobbers are ordinary pressure at their anchored
positions. A bundle covering a fixed-register occurrence must use that register.
A value live across a clobber must be split, spilled, or assigned to a
non-clobbered location.

## Liveness and Splitting

The register allocator owns precise liveness for allocation. It walks generic
Core def/use information, block parameters, edge arguments, and CFG edges. It
also consumes target `AllocationConstraints` to attach constrained occurrences
to allocator-local live ranges.

`Snapshot` instructions themselves produce no allocatable location, and their
captured operands are not allocation uses at the Snapshot definition. Whenever
an executable instruction consumes a `SnapshotRef`, liveness expands that
reference into point uses of every captured `ProgramValueRef` at the consumer's
declared timing. After this scan, Snapshot-derived uses require no special
treatment by the bundle allocator.

The allocator may internally build:

```text
ProgramValueRef -> live-range IDs
live-range ID -> stable SSA range and constrained occurrences
bundle -> set of non-overlapping live-range IDs
bundle or split child -> assigned register or spill location
```

Splitting refines bundles and range fragments; it does not mutate the Core SSA
graph or require regenerating structural constraints. The child covering a
constrained occurrence must satisfy that constraint. Moves reconnect adjacent
children after assignment.

## Block Parameters and Edge Moves

Block parameters and edge arguments are not the same SSA value. A target block
parameter is a new definition at block entry; each incoming edge argument is a
use at the predecessor edge. The transfer has parallel-copy semantics.

For:

```text
then:
    branch join(a)

else:
    branch join(b)

join(p):
    use p
```

`a`, `b`, and `p` are distinct SSA values. The edge transfers propose bundle
merges:

```text
then -> join: a -> p
else -> join: b -> p
```

The initial allocator must attempt these merges early. This is not just a
code-size optimization: clovervm block
arguments often carry broad logical frame state for safepoints and recovery, so
missing obvious coalescing would create large move bundles at ordinary joins and
loop backedges.

Edge coalescing is still a preference, not a correctness requirement. Allocation
may coalesce an edge argument and its target parameter when their live ranges do
not interfere and their constraints permit a shared location. If the assigned
locations differ, the allocator records a parallel move bundle on the
corresponding `BlockEdge`.

First-class `BlockEdge` objects make these transfers directly addressable.
Each ordered edge argument already pairs with the block parameter at the same
index, and distinct edges remain distinguishable even when they have the same
source and target. Allocation may tag the two boundary occurrences with the
edge and argument index, or expose their assigned entry and exit locations
through structural occurrence IDs. Final move generation can then walk each
edge once and fill its parallel-move bundle directly. It does not need
regalloc2's collect-sort-join scheme for matching source and destination
"half-moves", which compensates for edge arguments being embedded in branch
instructions rather than represented by durable edge objects.

Merge ordering and the weights of bundles created across edges should account
for:

- edge execution weight when available;
- loop depth;
- number of values transferred on the edge;
- whether the value participates in Snapshot or recovery state;
- whether either side has a fixed-register, clobber, or same-as-input pressure
  that makes coalescing unlikely.

Fixed constraints, clobbers, and real overlap override merging. A failed merge
does not constrain later location assignment; it merely leaves an edge move if
the separately allocated locations differ.

Several canonical interpreter homes may name the same machine value. In that
case the edge argument list references the same `ProgramValueRef` at each
corresponding logical position, and the successor interpreter-location map
associates those homes with one block parameter. The allocator therefore sees
one predecessor range and one successor range, not overlapping duplicate SSA
definitions. Repeating the same edge transfer or proposed bundle merge is
idempotent.

Critical-edge splitting is an implementation choice made when a move bundle has
no legal insertion point on the original edge. The semantic CFG retains
first-class `BlockEdge` objects and ordered edge arguments.

## Calls, Clobbers, and Temporaries

Calls are represented by ordinary allocation constraints:

```text
argument uses      -> early or late uses in fixed ABI registers or stack slots
result definitions -> early or late defs in fixed return registers
temporaries        -> target register classes
clobbers           -> caller-saved register masks
```

A value live across a call must be assigned to a non-clobbered location or split
around the call. The target describes clobbers; the allocator decides whether to
keep the value in a callee-preserved register, spill it, or insert split moves.

An explicit call result owns its fixed return register at the late point, so
that register is omitted from the call's clobber set:

```text
argument 0  -> Use Early, Fixed x0
argument 1  -> Use Early, Fixed x1
result      -> Def Late, Fixed x0
clobbers    -> caller-saved registers except x0
```

The late throwaway def for the `x1` clobber may follow the early argument use.
The explicit late result def supplies the required interference for `x0`
instead. A resultless call includes `x0` in its clobber set when the ABI permits
the call to destroy it.

Hidden scratch registers are not allowed. If a selected lowering needs a
temporary, its `AllocationConstraints` must expose that temporary. A reserved
global scratch during bring-up is still modeled as unavailable or clobbered in
the allocator problem rather than being consumed invisibly by emission.

## Unified Parallel Moves

After assigning locations, the allocator collects every physical transfer it
introduced:

- connectors between split bundle children;
- normalized fixed-register and reused-input fixups;
- explicit machine-value moves;
- block-edge argument transfers;
- spill loads and stores;
- ABI argument shuffles.

Transfers at one program point form one parallel move set. Resolving them
together avoids move chains created when block arguments, spills, and
instruction fixups are lowered independently. The resolver handles cycles with
an available temporary register or stack location and handles memory-to-memory
transfers through a legal target temporary.

Canonical-state synchronization may contribute additional transfers at the same
point, but the allocator does not decide which VM homes require publication.
`HomeState`, safepoint planning, or recovery planning owns that semantic
decision. A shared physical move resolver may combine its transfers with the
allocator-produced set once both are known.

The register allocator should expose reusable physical-move machinery rather
than hide it inside allocation. Canonical synchronization may reuse the same
location representation, parallel-move set, cycle detection, scratch
selection, register-to-register moves, spill loads and stores, and
memory-to-memory fallback. It supplies those mechanisms with transfers chosen
by `HomeState` or recovery planning; it does not ask the allocator to infer
which canonical homes are semantically current.

Redundant Move Elimination is a possible later quality pass, not part of the
initial allocator. It would symbolically track which value each physical
location already contains and remove a move or canonical-home store whose
destination is already current. The first implementation should emit the
straightforward resolved move sequence, inspect generated-code quality, and add
this analysis only if redundant transfers are material in practice.

### Open Question: Guaranteed Move Scratch

Parallel-move resolution must account for a cycle at a point where every
suitable register is occupied. Memory-to-memory transfers introduce the same
requirement even without a cycle. The initial design has no ordinary compiler
spill area, so it cannot yet copy regalloc2's complete fallback of using a
temporary stack slot and, when necessary, briefly spilling a victim register.

The implementation must choose a complete policy for every register class
before parallel moves are emitted. Plausible choices are to reserve a scratch
register, introduce an allocator-visible emergency stack area, add ordinary
spill slots, or make scratch exhaustion abort this compilation and return to
the interpreter. Probing the allocation map for a free register should remain
the cheap first choice, but it is not a correctness guarantee. This question is
separate from instruction-declared temporaries: a parallel-move cycle is known
only after allocation.

## Interpreter Locations and Spillability

Machine-value liveness, allocation continuity, and interpreter state are
separate:

```text
live range                 where one SSA value needs physical storage
bundle                     ranges that prefer one allocation
interpreter-location map   canonical VM homes naming the value over time
HomeState                  which homes currently contain an up-to-date copy
```

A bytecode-level move may change the interpreter-location map without creating
a new SSA value or machine move. Several canonical homes may name the same
`ProgramValueRef`, and the set of homes may change over its lifetime.

The first allocator derives position-bounded canonical-home opportunities from
Snapshot consumers instead of maintaining this map continuously through Core
IR. Each instruction that consumes a `SnapshotRef` seeds the canonical slots
required by that Snapshot at the consumer's position. A backward walk propagates
those requirements through the value's live ranges. The nearest later consumer
on each path wins; an earlier consumer replaces the future demand for the
section preceding it. Repeated consumers of one virtual Snapshot seed the
analysis independently.

At CFG joins, a canonical-home opportunity propagates into a predecessor only
when the nearest consumers on all applicable successor paths agree. Otherwise
the allocator splits the range at the divergence or declines to use a canonical
home there. Block parameters and edge arguments translate the demanded
`ProgramValueRef` across the edge.

A canonical home is not automatically the value's permanent spill slot.
Synchronizing a value there is valid only over the derived range section, and
the write must update `HomeState`. The initial implementation has no ordinary
compiler spill area: it may rematerialize a value or place it in a derived
canonical home, but otherwise the value is register-only. A later design may
add ordinary spill slots without changing the snapshot-demand analysis.

An unspillable bundle may evict spillable work, but if splitting and eviction
cannot produce a legal register assignment, compilation fails and execution
remains interpreted.

## Recovery and Safepoints

Snapshots remain semantic recovery descriptions. Allocation does not rewrite
Snapshot contents. Instead, Snapshot operands become point uses at the
instructions that consume them for exits or safepoints.

After allocation, recovery planning combines:

```text
Snapshot + LocationAssignments + HomeState -> RecoveryPlan
```

`LocationAssignments` identify where each captured `ProgramValueRef` lives at
the exit position. `HomeState` identifies canonical frame homes that already
contain required values. Recovery planning may then emit register moves, spill
loads, canonical-home stores, constant materialization, F64 boxing, and future
reification work without adding a second semantic state model.

Safepoint maps and deoptimization translations may share physical location
encodings, but they remain separate consumers. Safepoint maps need only managed
roots; deoptimization translations need complete interpreter-visible state.

## Verification

The verifier should be able to check the allocation boundary at three levels:

- every prepared executable instruction has matching allocation constraints for
  its allocatable inputs, outputs, temporaries, and clobbers;
- every constraint anchor resolves to a live prepared Core occurrence with a
  compatible `ValueRepresentation`, access kind, and early/late timing;
- every post-allocation assignment satisfies the relevant constraint at its
  occurrence position, including fixed registers, clobbers, reuse constraints,
  split moves, and block-edge parallel-copy bundles;
- symbolic execution of the resolved moves and assigned instruction operands
  preserves the original SSA def/use connectivity on every CFG path.

Diagnostics should report the durable anchor: instruction serial and operand
index, block parameter, block entry or exit, or block-edge argument. Ephemeral
numeric positions remain optional diagnostic detail.

The allocator should be developed with a generated SSA-CFG test input and a
symbolic allocation checker from its first non-trivial stages. The generator,
SSA validator, liveness implementation, allocator, and checker should be
cross-checked rather than relying only on hand-written examples. Fuzzing must
cover duplicate uses, fixed-register conflicts, early and late accesses,
reused inputs, loops, irreducible control flow, duplicate edge arguments,
spills, clobbers, split connectors, and cyclic parallel moves.

## Implementation Discipline

Register allocation is compilation-time-sensitive. Allocator-local objects
should use dense integer IDs and compact contiguous storage. Live ranges within
bundles remain sorted so overlap checks and merges use linear scans rather than
pointer-linked insertion and random lookup. Building precise liveness may cost
more upfront but can reduce allocation work substantially by avoiding false
interference and unnecessary splits.

Heuristic state such as priority and eviction weight should be compact and
observable in allocator dumps. Correctness must not depend on pointer order,
hash-table iteration order, or unstable workqueue ties.

## Implementation Staging

The initial executable tests may use fixed, trivial `LocationAssignments` for
`Parameter`, `Const`, and `Return`. Those assignments should still be produced
through the same `AllocationConstraints` vocabulary so the early backend does
not introduce a second convention.

The first real allocator should support:

- target register classes;
- instruction input and output constraints;
- fixed registers;
- same-as-input reuse;
- temporaries;
- clobber masks;
- block parameters and edge arguments;
- snapshot-derived canonical-home spilling;
- precise SSA live ranges;
- bundle formation across moves, reused inputs, and block arguments;
- priority-queue assignment with eviction and live-range splitting;
- split moves and edge parallel moves;
- parallel-move resolution;
- a symbolic allocation checker and generated-input fuzz target.

More advanced policies such as detailed spill-cost tuning, profitable
rematerialization, caller-context-sensitive register pressure, alternate
allocation algorithms, and Machine IR scheduling are later backend and
allocator work.

## External Model

Cranelift and
[`regalloc2`](https://cfallin.org/blog/2022/06/09/cranelift-regalloc2/) are the
closest external model. Cranelift IR uses block parameters instead of phi
instructions. `regalloc2` consumes block parameters, branch arguments, operand
constraints, independent access kind and timing, register classes, clobbers,
and CFG structure through a target-independent allocator interface. Clover
keeps the same conceptual separation while using direct Core/CFG anchors
instead of presenting a fully opaque IR adapter or making integer program
points durable.
