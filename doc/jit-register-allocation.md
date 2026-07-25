# JIT Register Allocation

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Prepared allocation problem and initial conflict-free register assignment implemented; post-allocation materialization remains open |
| Scope | Allocation constraints, allocator-local numbering, liveness, bundles, backtracking allocation, live-range splitting, block-edge transfers, clobbers, spills, and post-allocation materialization |
| Owning layers | Target preparation owns occurrence constraints and physical-transfer capabilities; the generic register allocator owns numbering, liveness, bundles, splitting, allocation, spill decisions, and parallel transfers; generic allocation materialization resolves transfers and physicalizes instruction occurrences; publication and recovery planners own canonical-state synchronization; machine-code emission only encodes the materialized program |
| Validated against | `tests/test_jit_allocation_constraints.cpp`, `tests/test_aarch64_allocation_constraints.cpp`, `tests/test_jit_register_allocator.cpp` |
| Supersedes | The open register-allocation direction in [JIT Compiler and IR](jit-compiler-and-ir.md) and [JIT Compiler Bring-up Plan](jit-compiler-bring-up-plan.md) |

This document defines the register-allocation contract for the clovervm JIT. It
fits the Core IR and CFG contracts in [JIT Compiler and IR](jit-compiler-and-ir.md)
and [JIT Control-Flow Graph](jit-control-flow-graph.md). Core IR remains a
target-independent SSA CFG with block parameters and edge arguments. Target
backends describe physical requirements through `AllocationConstraints`; the
generic allocator computes liveness, splits ranges, assigns locations, and
produces a `RegisterAllocationResult`. Generic allocation materialization then
turns that result into an `AllocatedProgram` whose executable instruction
occurrences use physical registers and whose allocator-induced transfers are
explicit.

Finite implementation work is tracked separately in
[JIT Register Allocation Implementation Progress](jit-register-allocation-progress.md).

The allocator is target-independent. It may know about register classes,
physical registers, stack slots, clobber masks, operand access timing, and
operand constraints, but it does not know the semantics of AArch64, x86-64, or
any concrete Core instruction kind. Unlike a fully IR-independent allocator,
it may consume clovervm's generic Core and CFG interfaces directly: blocks,
SSA defs and uses, block parameters, edge arguments, Snapshots, and structural
occurrence anchors are part of the common compiler contract.

## Phase Products

The allocation and emission boundary has four products:

```text
BackendPreparation
    selected lowerings
    AllocationConstraints
    physical-transfer capabilities

RegisterAllocationResult
    LocationAssignments
    TransferSchedule

AllocatedProgram
    physical registers for executable instruction occurrences
    resolved sequential physical transfers

machine code
```

`AllocationConstraints` describe legal locations and timing at structurally
anchored occurrences. ABI registers and stack locations use the same fixed
constraint mechanism as every other fixed occurrence; there is no separate ABI
constraint class.

`LocationAssignments` contain post-allocation facts. They map value
occurrences that require physical existence to registers, allocator spill
slots, canonical frame slots, or other explicitly supported locations at the
relevant points. They do not contain moves, loads, stores, split actions, or
control-flow insertion policy.

`TransferSchedule` contains actions introduced by allocation:

- connectors between split children;
- fixed-location and reused-input fixups;
- entry, return, and call ABI transfers;
- block-edge parallel transfers;
- spills and reloads.

Transfers at one insertion point retain parallel semantics until generic
materialization resolves them.

The generic materializer combines the original Core CFG, location facts,
parallel transfers, and target-provided physical-transfer capabilities. Its
result is an `AllocatedProgram`: a compiler-lifetime overlay whose semantic
instructions still refer to immutable Core instructions, but whose executable
operands, results, and temporaries name physical registers. It also contains
the resolved sequential transfers to emit at block boundaries, instruction
Early and Late points, and first-class block edges.

The materializer introduces physical transfers, not target machine
instructions. A transfer from register to register later encodes as a move;
stack to register encodes as a load; register to stack encodes as a store.
Memory-to-memory transfers are resolved through a target-legal scratch location
before emission. The target emitter chooses the actual instruction encoding
and addressing mode but makes no allocation, splitting, or move-order decision.

The intended type boundary is:

```cpp
struct RegisterAllocationResult
{
    LocationAssignments locations;
    TransferSchedule transfers;
};

AllocatedProgram materialize_allocation(
    const ControlFlowGraph &graph,
    const RegisterAllocationResult &allocation,
    const PhysicalTransferConstraints &target);
```

`AllocatedProgram` and `RegisterAllocationResult` own their compiler-lifetime
tables and borrow immutable Core instructions and edges from the still-live
compilation session. Recovery planning consumes `LocationAssignments`;
machine-code emission consumes `AllocatedProgram`.

Canonical VM homes and whether they currently contain an up-to-date value
remain separate state. A canonical frame home is not silently converted into
an allocator-owned spill slot.

A Core def marked sunk has no `LocationAssignment`. Its recovery-only operation
remains available to recovery planning, while allocation liveness reaches
through it to the first non-sunk operands that must physically exist at an
exit.

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

## Allocation Locations

An allocation location is a place that can contain one machine value. The
initial vocabulary is:

```cpp
enum class StackLocationKind : uint8_t
{
    CanonicalFrameSlot,
    SpillSlot,
};

class StackLocation
{
public:
    StackLocation(StackLocationKind kind, int32_t frame_offset);

    StackLocationKind kind() const;
    int32_t frame_offset() const;
};

class AllocationLocation
{
public:
    static AllocationLocation reg(PhysicalRegister reg);
    static AllocationLocation stack(StackLocation stack);

    bool is_register() const;
    bool is_stack() const;
    PhysicalRegister reg() const;
    StackLocation stack() const;
};
```

The signed frame offset uses the same stack-slot coordinate convention as
`BytecodeValueLocation`. Frame layout can therefore slide offsets directly into
`StackLocation` without an unsigned remapping layer.

`CanonicalFrameSlot` covers parameters, locals, temporaries, and managed call
argument windows. Moving the managed frame pointer may reinterpret one physical
cell from a caller's argument window as a callee parameter; it does not create
a distinct outgoing-argument location kind. Canonical slots are
interpreter-visible homes and participate in publication, recovery, and stack
scanning. `SpillSlot` is compiler-owned temporary storage and is not
automatically an interpreter home. The value representation attached to the
occurrence or transfer determines the width, register class, and stack access
required for either kind.

A fixed location requirement applies only at its exact occurrence point. It
does not pin the original live range to that location. The allocator may split
immediately before or after the occurrence, subject to its use/def and
Early/Late timing, and connect the adjacent fragments with a transfer.
Fragments constrained to a fixed stack location are assigned that location
directly rather than entering a register probe queue. Their adjacent
register-required children remain ordinary register bundles.

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

The common layer derives ordinary location requirements directly from Core
`ValueRepresentation`:

```text
TaggedValue input  -> Use Early, AnyRegister(GPR)
F64 input          -> Use Early, AnyRegister(SIMD)
TaggedValue result -> Def Late, AnyRegister(GPR)
F64 result         -> Def Late, AnyRegister(SIMD)
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

class LocationRequirement
{
public:
    enum class Kind : uint8_t
    {
        AnyRegister,
        FixedLocation,
        SameAsInput,
    };

    static LocationRequirement any_register(RegisterClass register_class);
    static LocationRequirement fixed(AllocationLocation location);
    static LocationRequirement same_as_input(uint32_t operand_index);

    Kind kind() const;
    RegisterClass register_class() const;
    AllocationLocation fixed_location() const;
    uint32_t input_index() const;

private:
    LocationRequirement(Kind kind, size_t payload);

    Kind kind_;
    size_t payload_;
};

struct ProgramValueUseConstraint
{
    ProgramValueUseConstraint(uint32_t operand_index, AccessTiming timing,
                              LocationRequirement requirement);

    uint32_t operand_index;
    AccessTiming timing;
    LocationRequirement requirement;
};

struct ResultConstraint
{
    AccessTiming timing;
    LocationRequirement requirement;
};

struct TemporaryConstraint
{
    explicit TemporaryConstraint(LocationRequirement requirement);

    LocationRequirement requirement;
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

`LocationRequirement::AnyRegister` names a register class;
`LocationRequirement::FixedLocation` names one register or stack location; and
`LocationRequirement::SameAsInput` names the ProgramValue input whose assigned
location the result must reuse. Contextual constructors reject
`SameAsInput` for inputs and temporaries, so it remains a result-only
requirement without a second variant-based representation. The compact
allocator representation may encode these alternatives differently.

Most executable Core occurrences use `AnyRegister`. A stack-assigned fragment
therefore cannot cover such an occurrence. A fixed stack occurrence followed
by a register-only occurrence creates statically incompatible requirements and
forces splitting:

```text
entry def  FixedLocation(CanonicalFrameSlot(parameter_offset))
later use  AnyRegister(GPR)

stack fragment -> register fragment
               ^ load immediately before the register-only use
```

Keeping the stack fragment until the latest legal split point delays the load
and shortens register pressure. A fixed register and `AnyRegister` of the same
class remain compatible; the unsplit bundle may simply use that register.
Different fixed registers, or a fixed stack location and a register-only
occurrence, require splitting or an explicit fixup.

The allocator does not allocate a `SnapshotRef`. At each executable instruction
that consumes one, allocator preparation expands the captured
`ProgramValueRef`s into unconstrained late point uses. Repeated consumers of one
virtual Snapshot expand independently.

The heterogeneous ProgramValue references stored by the virtual `Snapshot`
instruction itself have no direct input constraints at the Snapshot's
definition position. They become allocation uses only through this expansion at
each Snapshot-consuming instruction.

A temporary takes either an `AnyRegister` or fixed-register requirement and
reserves the chosen register across the selected target sequence. `clobbers`
instead describes registers destroyed implicitly by the operation. Structural
preparation retains the compact register set. Allocator preparation expands
each member into an immovable half-open reservation covering the instruction's
Late point in that physical register's allocation map. A clobber is not a
bundle: it has no semantic value, allocation choice, spill weight, or legal
eviction.

Every physical register written by an instruction must be represented either
by an explicit def, including an allocated temporary, or by a clobber, but not
both. A clobber may coincide with a fixed early use, but it may not collide with
a fixed def or fixed late use. This is the allocator's clobber contract.

A potentially long branch therefore requests a GPR temporary; it does not
needlessly clobber a predetermined register. The emitter may ignore the
assigned temporary when the short branch form fits.

Register requirements and spill compatibility must agree with the Core
`ValueRepresentation` of the value. A constraint may narrow that representation
to a target class or compatible fixed location, but it must not change
representation semantics. Representation changes remain explicit Core
instructions.

## Initial AArch64 Bring-up Contract

The first AArch64 constraint producer temporarily follows the platform ABI.
This is a bring-up choice, not the final CloverVM calling convention:

- the enabled GPR class contains `x0` through `x15`, in that allocation order;
- the enabled SIMD class contains caller-saved `v0` through `v7` followed by
  `v16` through `v31`;
- `x16` and `x17` remain unavailable until allocation assignments are consumed
  by every branch and call lowering that may need scratch registers;
- platform-reserved `x18` is unavailable;
- callee-saved GPRs and `v8` through `v15` remain unavailable until prologue
  and epilogue generation preserves them;
- tagged entry-block parameters zero through seven have fixed-location result
  constraints `x0` through `x7`;
- tagged internal block parameters use the ordinary `AnyRegister(GPR)`
  default;
- F64 internal block parameters use the ordinary `AnyRegister(SIMD)` default;
- a `Return` input has a fixed `x0` constraint;
- conditional and unconditional branches request one `AnyRegister(GPR)`
  temporary for a possible long form;
- `Const`, SMI bitwise instructions, and the virtual `Snapshot` instruction
  need no target override.

Stack-passed entry parameters, F64 entry parameters, calls, and instruction
kinds without a bring-up lowering currently hard-fail instead of silently
receiving an incomplete contract. Managed stack arguments use fixed
`CanonicalFrameSlot` locations; they do not create a separate ABI constraint
mechanism. ABI registers likewise remain ordinary fixed locations at exact
occurrences.

Constraint validation enforces:

- each input override names one allocatable ProgramValue operand, and no
  occurrence has two overrides;
- a result override occurs only on a ProgramValue-producing instruction;
- `SameAsInput` names a valid ProgramValue input and occurs only on a result;
- fixed locations and `AnyRegister` classes are compatible with the
  occurrence's `ValueRepresentation`;
- every `RegisterClassDefinition::allocation_order` is a permutation of its
  members;
- register clobbers do not collide with explicit fixed-register defs, fixed
  late uses, or fixed temporaries, including the fixed register obtained after
  resolving `SameAsInput`.

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

Allocator ranges use one uniform half-open representation:

```cpp
struct ProgramRange
{
    ProgramPoint start;  // inclusive
    ProgramPoint end;    // exclusive
};
```

Half-open ranges make adjacency and splitting exact. Splitting `[a, c)` at
`b` produces `[a, b)` and `[b, c)` without overlap, a gap, or predecessor
arithmetic. `ProgramRange` contains no block, definition, occurrence, or
allocation metadata. It may represent the empty intermediate range `[p, p)`,
but every live range and bundle fragment is nonempty. Whether a point is a
legal split boundary is enforced by the allocator operation performing the
split rather than by `ProgramRange`.

## Live Ranges and Bundles

A live range is an immutable allocator-local record of one contiguous region
where a Core SSA value or anonymous target temporary needs storage. Ordinary
SSA values do not flow directly between blocks. An edge argument is a use at
the predecessor exit and its corresponding block parameter is a distinct
definition at the successor entry. Consequently, each live range is local to
one block even though preparation handles the complete multi-block CFG.

A dead definition still receives the minimal half-open range from its
definition point to the next program point. Preparation does not special-case
dead `Uninitialized` or other definitions out of the allocation model.

The live range retains its stable ID, original `ProgramRange`, origin, and
ordered occurrence IDs. A ProgramValue origin records its `ProgramValueRef`; an
anonymous temporary origin records its instruction and temporary index. Its
register class is derived once from the ProgramValue representation or
temporary declaration. Observing incompatible register classes for one live
range is a compiler invariant failure.

The initial allocator-local shape is conceptually:

```cpp
struct Occurrence
{
    ProgramPoint point;
    LiveRangeId live_range;
    OccurrenceKind kind;
    OccurrenceAnchor anchor;
    uint64_t spill_weight;
};

struct FixedLocationConstraint
{
    ProgramPoint point;
    AllocationLocation location;
    LiveRangeId live_range;
    OccurrenceId occurrence;
};

struct LiveRange
{
    ProgramRange range;
    LiveRangeOrigin origin;
    RegisterClass register_class;
    std::vector<OccurrenceId> occurrences;
    std::vector<FixedConstraintId> fixed_constraints;
};
```

`OccurrenceAnchor` retains the instruction operand or result, block parameter,
edge argument, or temporary identity needed by diagnostics and final lowering.
It does not make ephemeral integer positions durable.

A bundle is the allocator's unit of assignment: a set of non-overlapping
fragments that should receive one location. A bundle does not merely contain
live range IDs. It owns an ordered list of copied allocation fragments:

```cpp
struct BundleFragment
{
    ProgramRange range;
    LiveRangeId source;
};
```

The `range` is the fragment's current allocation geometry. `source` preserves
the immutable live-range identity needed to recover the `ProgramValueRef`,
temporary identity, occurrences, and original extent. Initial singleton
construction copies the complete source range. Later splitting may place
proper subsets of the same source range in different bundles:

```text
source L0: [10, 30)

bundle B1: { [10, 18), L0 }
bundle B2: { [18, 30), L0 }
```

The source live range remains `[10, 30)`. Bundle fragments, rather than source
live ranges, are the mutable allocation geometry. Fragments within one bundle
are sorted and non-overlapping. Every fragment is contained by its source
range, and the active fragments derived from one source neither overlap nor
silently lose occurrence-bearing portions of that source.

Occurrences remain stored on the source live range. Bundle property
calculation walks the ordered occurrences covered by each fragment. This
indirection is the initial representation; a fragment may later cache a
contiguous occurrence-index window if measurement shows repeated lookup to be
material.

Bundles recover physical continuity between distinct SSA values without
changing their semantic identities. The allocator later attempts to merge
bundles related by:

- block-argument transfers;
- explicit machine-value moves;
- reused-input constraints;
- other backend-declared allocation equalities.

Bundle merging combines the sorted fragment lists subject to the invariant that
two fragments in one bundle never overlap. Stable source live-range IDs support
merge idempotency while the pre-splitting merge phase still has one bundle
owner per live range. Affinity merging completes before allocation splitting
begins. After splitting, one source ID may occur in several bundles and a
singular `LiveRangeId -> BundleId` ownership map is no longer valid.

The initial prepared problem creates one singleton bundle containing one exact
fragment for every live range. Its representation is fully multi-block, but
the first conflict-free assignment stage accepts executable graphs only when
no cross-block bundle merging, edge moves, splitting, eviction, or spilling is
required.

The selected allocation is not stored in the prepared bundle. A separate
bundle-assignment table records the physical register or later spill location
chosen for each active bundle.

The initial register-only assignment stage uses:

```cpp
class BundleRegisterAssignments
{
public:
    PhysicalRegister register_for(BundleId bundle) const;

private:
    std::vector<PhysicalRegister> register_by_bundle_;
};
```

This is the allocator-local forward result. The reverse
`PhysicalRegister -> assigned fragments` index and the corresponding clobber
range index are assignment scratch state and are discarded after placement.
Each register's active fragments are stored in an `absl::btree_map` keyed only
by fragment start, with fragment end and `BundleId` in the value. Active
fragments are non-empty and conflict-free, so duplicate starts are an allocator
error. Candidate lookup checks the predecessor of `lower_bound(start)` and then
the entries beginning before the candidate end. Insertion and later eviction
therefore cost `O(F log A)` for `F` bundle fragments and `A` active fragments
on the register. A bundle must be removed from this index before splitting or
otherwise mutating its fragments.

Clobber ranges remain separate because they are immutable reservations rather
than allocatable bundles. They are sorted and coalesced once, then queried by
the same predecessor-and-successor rule.
Later location materialization converts the bundle result into durable
occurrence-oriented `LocationAssignments`.

The corresponding prepared bundle is conceptually:

```cpp
struct LiveBundle
{
    RegisterClass register_class;
    std::vector<BundleFragment> fragments;
    std::vector<FixedConstraintId> fixed_constraints;
    size_t allocation_priority;
    uint64_t spill_weight;
};
```

The fixed-constraint list and heuristic fields are derived caches recomputed
after bulk construction, merge, or split. They are not replacements for source
occurrences. Chosen physical locations live in the separate assignment state.

Allocation priority and spill weight are distinct. Priority determines which
bundle is processed next. Spill weight determines whether an unallocated bundle
may evict conflicting allocated bundles. Both are recomputed for children
created by splitting.

A bundle's allocation priority is the sum of the lengths of its fragments in
allocator program-point space. The priority queue therefore considers bundles
covering more code first, while the physical register maps are relatively
empty. Fixed-location constraints do not receive a separate priority boost.

Ordinary `AnyRegister(register_class)` requirements are implicit in each live
range's register class and are not copied into allocator occurrences. The
allocator retains every ordinary use and def occurrence for liveness,
constraint-driven splitting, spill weight, diagnostics, and later rewriting,
but only nondefault fixed-location constraints need sparse requirement records.
A fixed constraint records its program point, allocation location, source live
range, and occurrence ID. Incompatible register classes, a fixed register from
the wrong class, or a stack location incompatible with the value
representation are compiler invariant failures.

Each bundle keeps the fixed-constraint IDs covered by its current fragments,
ordered by program point. Merging combines those sparse lists; splitting
partitions them by point. Several fixed constraints naming the same location
restrict the whole unsplit bundle to that location when the ordinary
occurrences it covers are compatible. Different fixed locations at different
positions are valid pressure requiring splitting or fixups, not a compiler
invariant failure. A fixed stack location and an ordinary register-only
occurrence are likewise incompatible within one fragment. The first
non-splitting allocator reports a recoverable unsupported-allocation result for
such a bundle.

Each constrained occurrence contributes an initial spill weight:

```text
hot contribution          = 1000 * 4^min(loop_depth, 10)
definition contribution   = 2000 for a def, otherwise 0
requirement contribution  = 1000 for AnyRegister, 2000 for FixedLocation
```

The ordinary `AnyRegister` contribution is implied by an unconstrained
occurrence; it does not require a stored allocator constraint. A sparse fixed
constraint replaces that occurrence's requirement contribution.

Spill weights use 64-bit unsigned arithmetic because one bundle may accumulate
contributions from many occurrences. Saturating addition prevents heuristic
overflow from changing allocation ordering.

Constraints normalized into fixup moves are weighted at the resulting
occurrences. A non-minimal bundle's spill weight is:

```text
sum(occurrence spill weights) / allocation priority
```

This makes spill weight a density rather than a total cost. Splitting a bundle
usually raises the weight of the pieces containing important occurrences
because the same use weight is divided by a shorter live range. Snapshot and
recovery demand, rematerialization cost, and measured block frequency may
eventually refine the occurrence weights, but they are not part of the initial
formula.

A minimal bundle contains one fragment covering at most the irreducible range
required by one occurrence. Minimal bundles use reserved spill-weight values
above every non-minimal bundle: a minimal fixed-location bundle has the maximum
weight, and an ordinary minimal bundle has the next lower tier. Clobber
reservations are not bundles and cannot be evicted.

## Constraint Normalization and Fixups

The allocator normalizes awkward occurrence constraints into compatible
fragments plus deferred fixup transfers before its core assignment loop.

Requirements covered by one fragment must admit one common location.
`AnyRegister(GPR)` and `FixedLocation(x0)` are compatible. Two different fixed
registers are incompatible, as are a fixed stack location and an ordinary
register-only occurrence.

The normalizer splits immediately before the first incompatible use whenever
legal. This keeps an earlier stack or spill fragment live until the last
possible point, places its reload immediately before the register-only use, and
minimizes register pressure. A constrained definition starts its fixed
fragment at the definition point; a connector to a later fragment occurs after
the def. Early and Late timing determine the exact legal side of the
instruction.

If one SSA value is required in two locations at the same instruction, one
constrained occurrence remains on the range and the other becomes a fixup
transfer at that occurrence. A reused-input result is treated as a new range
starting at the input phase, with a fixup transfer from the input and a
high-priority merge opportunity between the two ranges. If they can share a
location the transfer disappears; otherwise it remains in the schedule.

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
collect split, fixup, explicit, and block-edge transfers
produce RegisterAllocationResult
resolve parallel transfers and physicalize occurrences
produce AllocatedProgram
```

Backtracking alone does not guarantee forward progress. The allocator relies on
the following ordering rules:

- a bundle may evict conflicts only when its spill weight is strictly greater
  than the maximum spill weight among those conflicts;
- equal-weight bundles split rather than repeatedly evicting each other;
- every split makes the affected bundles smaller;
- minimal bundles occupy reserved spill-weight tiers above non-minimal bundles,
  with minimal fixed-location bundles at the maximum weight.

These rules prevent two bundles from evicting each other indefinitely and
ensure that repeated splitting eventually reaches irreducible allocation
problems. A useful first split point is the first conflicting constrained use or
the first point at which a candidate register ceases to fit. If no legal
assignment remains for an unspillable minimal bundle, compilation fails rather
than emitting incorrect code.

Loop-aware split placement is later code-quality tuning. Once the basic
allocator is producing inspectable code, measure whether connectors, canonical
stores, or reloads are being placed inside hot loops. If that is material,
prefer split points with cheaper connecting moves and hoist boundaries outside
loops when legal. The initial allocator does not need this heuristic.

Fixed-register constraints and clobbers are ordinary pressure at their anchored
positions. An unsplit bundle whose fixed occurrences all name one register must
use that register. Fixed occurrences naming different registers require
splitting or fixups. A value live across a clobber must be split, spilled, or
assigned to a non-clobbered location.

## Liveness and Splitting

The register allocator owns precise liveness for allocation. It walks generic
Core def/use information, block parameters, edge arguments, and CFG edges. It
also consumes target `AllocationConstraints` to attach constrained occurrences
to allocator-local live ranges.

`Snapshot` instructions themselves produce no allocatable location, and their
captured operands are not allocation uses at the Snapshot definition. Whenever
an executable instruction consumes a `SnapshotRef`, liveness expands that
reference at the consumer's declared timing. An ordinary captured
`ProgramValueRef` becomes a point use. A captured sunk def is recursively
expanded through the sunk closure until only non-sunk physical inputs remain.
After this scan, neither Snapshot nor sunk-instruction uses require special
treatment by the bundle allocator.

The allocator may internally build:

```text
ProgramValueRef -> live-range ID
live-range ID -> stable origin, original range, and ordered occurrences
bundle -> sorted non-overlapping {range, source live-range ID} fragments
bundle -> sparse fixed constraints covered by those fragments
bundle or split child -> assigned register or spill location
```

Splitting partitions bundle fragments; it does not mutate the original live
range, the Core SSA graph, or durable structural constraints. The same source
live-range ID may therefore appear in adjacent children. The child covering a
fixed occurrence must satisfy that constraint. Moves reconnect adjacent
children after assignment, and the shared source ID identifies the semantic
value being transferred.

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
missing obvious coalescing would create large transfer sets at ordinary joins
and loop backedges.

Edge coalescing is still a preference, not a correctness requirement. Allocation
may coalesce an edge argument and its target parameter when their live ranges do
not interfere and their constraints permit a shared location. If the assigned
locations differ, the allocator records a parallel transfer set on the
corresponding `BlockEdge`.

First-class `BlockEdge` objects make these transfers directly addressable.
Each ordered edge argument already pairs with the block parameter at the same
index, and distinct edges remain distinguishable even when they have the same
source and target. Allocation may tag the two boundary occurrences with the
edge and argument index, or expose their assigned entry and exit locations
through structural occurrence IDs. Final transfer generation can then walk
each edge once and fill its scheduled parallel set. It does not need a
collect-sort-join scheme for matching source and destination "half-moves",
because Clover's edge arguments are represented by durable edge objects rather
than embedded anonymously in branch instructions.

Merge ordering and the weights of bundles created across edges should account
for:

- edge execution weight when available;
- loop depth;
- number of values transferred on the edge;
- whether the value participates in Snapshot or recovery state;
- whether either side has a fixed-location, clobber, or same-as-input pressure
  that makes coalescing unlikely.

Fixed constraints, clobbers, and real overlap override merging. A failed merge
does not constrain later location assignment; it merely leaves an edge
transfer if the separately allocated locations differ.

Several canonical interpreter homes may name the same machine value. In that
case the edge argument list references the same `ProgramValueRef` at each
corresponding logical position, and the successor interpreter-location map
associates those homes with one block parameter. The allocator therefore sees
one predecessor range and one successor range, not overlapping duplicate SSA
definitions. Repeating the same edge transfer or proposed bundle merge is
idempotent.

Critical-edge splitting is an implementation choice made when a transfer set
has no legal insertion point on the original edge. The semantic CFG retains
first-class `BlockEdge` objects and ordered edge arguments.

## Calls, Clobbers, and Temporaries

Calls are represented by ordinary allocation constraints:

```text
managed Python arguments -> early or late uses in fixed canonical frame slots
native arguments         -> early or late uses in fixed platform ABI registers
result definitions       -> early or late defs in fixed result locations
temporaries              -> target register classes
clobbers                 -> caller-saved register masks
```

A managed Python call prepares the existing Clover argument window. Its
arguments therefore use `FixedLocation(CanonicalFrameSlot(...))`; moving the
managed frame pointer reinterprets those same cells as the callee's parameters.
A native call instead uses fixed platform calling-convention registers. Both
are ordinary fixed-location constraints, and both generate transfers when the
surrounding value fragment occupies a different location. Native stack
arguments can extend the platform-call lowering later if Clover actually
supports them; they are not part of the managed stack-location vocabulary.

A value live across a call must be assigned to a non-clobbered location or split
around the call. The target describes clobbers; the allocator decides whether to
keep the value in a callee-preserved register, spill it, or insert split moves.

An explicit call result owns its fixed return register at the late point, so
that register is omitted from the call's clobber set:

```text
argument 0  -> Use Early, FixedLocation(x0)
argument 1  -> Use Early, FixedLocation(x1)
result      -> Def Late, FixedLocation(x0)
clobbers    -> caller-saved registers except x0
```

The immovable Late reservation for the `x1` clobber may follow the early
argument use. The explicit late result def supplies the required interference
for `x0` instead. A resultless call includes `x0` in its clobber set when the
ABI permits the call to destroy it.

Hidden scratch registers are not allowed. If a selected lowering needs a
temporary, its `AllocationConstraints` must expose that temporary. A reserved
global scratch during bring-up is still modeled as unavailable or clobbered in
the allocator problem rather than being consumed invisibly by emission.

## Unified Parallel Transfers

After assigning locations, the allocator collects every physical transfer it
introduced:

- connectors between split bundle children;
- normalized fixed-location and reused-input fixups;
- explicit machine-value moves;
- block-edge argument transfers;
- spill and reload transfers;
- ABI argument shuffles.

Transfers at one program point form one parallel transfer set. Resolving them
together avoids move chains created when block arguments, spills, and
instruction fixups are lowered independently. The resolver handles cycles with
an available temporary register or stack location and handles memory-to-memory
transfers through a legal target temporary.

Canonical-state synchronization may contribute additional transfers at the same
point, but the allocator does not decide which VM homes require publication.
`HomeState`, safepoint planning, or recovery planning owns that semantic
decision. A shared physical-transfer resolver may combine its transfers with the
allocator-produced set once both are known.

Generic allocation materialization should expose reusable physical-transfer
machinery rather than hide it inside one emitter. Canonical synchronization may
reuse the same location representation, parallel-transfer set, cycle
detection, scratch selection, register moves, spill loads and stores, and
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

Parallel-transfer resolution must account for a cycle at a point where every
suitable register is occupied. Memory-to-memory transfers introduce the same
requirement even without a cycle. The initial design has no ordinary compiler
spill area, so it cannot yet use the complete fallback of a temporary stack
slot and, when necessary, briefly spilling a victim register.

The implementation must choose a complete policy for every register class
before parallel transfers are emitted. Plausible choices are to reserve a scratch
register, introduce an allocator-visible emergency stack area, add ordinary
spill slots, or make scratch exhaustion abort this compilation and return to
the interpreter. Probing the allocation map for a free register should remain
the cheap first choice, but it is not a correctness guarantee. This question is
separate from instruction-declared temporaries: a parallel-transfer cycle is known
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
Snapshot
    + sinking attachment
    + LocationAssignments
    + HomeState
    -> RecoveryPlan
```

`LocationAssignments` identify where each non-sunk physical frontier value
lives at the exit position. `HomeState` identifies canonical frame homes that
already contain required values. Recovery reads those locations, evaluates
sunk instructions, performs constant materialization, F64 boxing, and future
reification, and publishes canonical state without adding a second semantic
state model. Whether this becomes generated code or an interpreted restricted
IR remains open.

Safepoint maps and RecoveryPlans may share physical location encodings, but
they remain separate consumers. Safepoint maps need only managed roots;
RecoveryPlans need complete interpreter-visible state.

## Verification

The verifier should be able to check the allocation boundary at the following
levels:

- every prepared executable instruction has matching allocation constraints for
  its allocatable inputs, outputs, temporaries, and clobbers;
- every constraint anchor resolves to a live prepared Core occurrence with a
  compatible `ValueRepresentation`, access kind, and early/late timing;
- every post-allocation assignment satisfies the relevant constraint at its
  occurrence position, including fixed locations, clobbers, reuse constraints,
  split transfers, and block-edge parallel-transfer sets;
- no sunk def receives a physical assignment, and every non-sunk input on the
  recovery frontier is live and assigned at each consuming exit;
- symbolic execution of the resolved transfers and assigned instruction operands
  preserves the original SSA def/use connectivity on every CFG path.

Diagnostics should report the durable anchor: instruction serial and operand
index, block parameter, block entry or exit, or block-edge argument. Ephemeral
numeric positions remain optional diagnostic detail.

The allocator should be developed with a generated SSA-CFG test input and a
symbolic allocation checker from its first non-trivial stages. The generator,
SSA validator, liveness implementation, allocator, and checker should be
cross-checked rather than relying only on hand-written examples. Fuzzing must
cover duplicate uses, fixed-location conflicts, early and late accesses,
reused inputs, loops, irreducible control flow, duplicate edge arguments,
spills, clobbers, split connectors, and cyclic parallel transfers.

## Implementation Discipline

Register allocation is compilation-time-sensitive. Allocator-local objects
should use dense integer IDs and compact contiguous storage. Live ranges within
bundles remain sorted so overlap checks and merges use linear scans rather than
pointer-linked insertion and random lookup. Building precise liveness may cost
more upfront but can reduce allocation work substantially by avoiding false
interference and unnecessary splits.

Heuristic state such as priority and spill weight should be compact and
observable in allocator dumps. Correctness must not depend on pointer order,
hash-table iteration order, or unstable workqueue ties.

## Persistent Bring-up Constraints

The first allocator has no ordinary compiler spill area. It may use registers
or a derived canonical home when one is proven legal; otherwise excessive
pressure aborts compilation and execution remains interpreted.

The hardcoded `x0` emitter path is test scaffolding, not a second allocation
strategy. The first assignment stage may accept only one-block executable
graphs, but preparation, live-range storage, bundle fragments, and assignment
results must use the general allocator model. Bring-up work must not introduce
a one-block data model, target-specific allocator, or temporary fixed-first
queue policy that will be discarded by the accepted allocator.

Target code defines location vocabulary, register availability, allocation
order, instruction constraints, and legal physical-transfer capabilities. The
generic allocator owns liveness, bundle policy, priority, spill weight,
probing, eviction, splitting, assignment, and parallel transfer scheduling.
Generic materialization resolves those transfers and physicalizes instruction
occurrences before target emission.

Allocator-local positions, ranges, bundles, allocation maps, and heuristic
state remain ephemeral. `RegisterAllocationResult` is the durable allocator
result; recovery consumes its `LocationAssignments`, while emission consumes
the derived `AllocatedProgram`.

## External Model

Cranelift is the closest external IR model because it uses block parameters
instead of phi instructions. General SSA bundle allocators consume block
parameters, branch arguments, operand constraints, independent access kind and
timing, register classes, clobbers, and CFG structure through a
target-independent interface. Clover keeps the same conceptual separation
while using direct Core/CFG anchors instead of presenting a fully opaque IR
adapter or making integer program points durable.

The allocation-priority, spill-weight, splitting, and progress rules above
should be changed only with an equivalent termination argument.
