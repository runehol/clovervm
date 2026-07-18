# JIT Compiler and IR

This document records the current assumptions, constraints, and design
guardrails for a future clovervm JIT compiler. It is not an implementation plan
or a complete IR specification. Its purpose is to keep compiled execution
compatible with the existing bytecode, object model, inline caches, calling
convention, and reclamation machinery.

The design currently has three principal IR levels:

```text
Semantic IR -> Guard IR -> Machine IR
```

All three are ordered, list-based SSA control-flow graphs. Semantic IR preserves
atomic bytecode semantics and performs inference and inlining. Guard IR makes
speculative checks, effects, and proof dependencies explicit. Machine IR owns
target representation, register allocation, and encoding.

The following choices are design guardrails rather than tentative suggestions:

- bytecode is the canonical execution and recovery model;
- the generic runtime consumes canonical interpreter frame state, not optimized
  register state or compiled stack maps;
- interpreted and compiled Python calls initially share the existing managed
  calling convention;
- inline caches drive specialization, and misses return to the interpreter;
- Semantic IR and Guard IR share one semantic type system;
- the initial JIT operates on tagged `Value`s and does not require unboxing.

Type partitions, the detailed effect taxonomy, and many backend policies remain
plausible or open designs as noted below.

## Terminology

Use distinct terms for operations that are easily conflated:

- **load** or **cache**: move a canonical frame value into a machine register;
- **spill**: move a register value into a machine spill slot;
- **frame synchronization**: write current interpreter-visible values into
  canonical frame homes;
- **frame activation**: make a preallocated canonical frame region the current
  logical Python frame and initialize its structural metadata;
- **frame reconstruction**: create an interpreter frame that has no existing
  canonical backing region; active inlined frames do not require this;
- **boxing** or **reification**: create a heap object for a virtual semantic
  value;
- **root publication**: expose synchronized frames and the accumulator to
  reclamation machinery.

`Materialization` is too ambiguous to be the precise name for these operations.

## Runtime and Recovery Contract

### Runtime invariant: bytecode is canonical

The existing bytecode is the canonical execution and recovery model. JIT code
is a speculative execution of that bytecode, not a replacement language with
independent state or semantics.

Compiled execution must be able to return to the interpreter at essentially
any speculative point, including:

- a failed shape or shape-key check;
- an invalid validity cell;
- SMI arithmetic overflow;
- an inline-cache miss;
- a tripped safepoint poll;
- any other failed assumption used by compiled code.

At such an exit, the JIT must recover the interpreter-visible state at the
appropriate bytecode position. Designs that make bytecode state expensive or
impossible to reconstruct are out of scope.

### Runtime invariant: canonical frame publication

The interpreter, garbage collector, and runtime side of deoptimization consume
only canonical interpreter frame state. Compiled code may temporarily cache
newer interpreter-visible values in machine registers, but before any operation
that may reclaim memory, it synchronizes every dirty canonical frame home in
the complete active logical frame chain and publishes the innermost active
accumulator through `ThreadState`. Before transferring execution back to the
interpreter, the JIT exit machinery also finalizes bytecode PCs and other frame
metadata and reifies any virtual values required by those frames.

Once publication or interpreter handoff begins, every interpreter-visible root
is therefore available through canonical frames and `ThreadState`. The generic
runtime never scans optimized register state and does not require compiled-frame
stack maps. JIT-generated publication and exit machinery may know how optimized
locations correspond to logical frame state; that knowledge does not escape
into the collector or interpreter.

Immediate consequences include:

- an untripped safepoint poll need not publish state if reclamation cannot begin
  asynchronously;
- a tripped poll synchronizes, publishes, and deoptimizes before reclamation;
- calls that may reclaim require publication before entry;
- publication covers dirty homes in outer and inlined active frames, not only
  the frame containing the current operation;
- a register copy of a synchronized frame value is a cache, not a second root
  model;
- a value live across reclamation must have a managed canonical home or be
  placed into one by publication machinery;
- non-bytecode IR temporaries cannot remain live across reclamation merely
  because compiler metadata could reconstruct them later.

Treating a tripped safepoint as a deoptimization boundary is an intentional
initial simplification. Compiled-frame scanning can be considered later only if
measurements justify replacing this invariant.

### Interpreter-visible state

The bytecode uses an accumulator and stack-backed registers. Registers contain,
in order:

1. arguments to the current function;
2. local variables;
3. temporaries;
4. outgoing arguments for the function about to be called.

Moving the frame pointer changes the interpretation of these slots, similarly
to register windows. Each bytecode register retains a stable canonical frame
home even when its current value is cached in a machine register.

The accumulator already has special safepoint treatment: it is published
through `ThreadState` separately from the scanned frame range. The JIT reuses
that mechanism rather than adding an accumulator frame slot.

Inlining does not remove the corresponding Python frame activation. Every
active inlined function has a stable canonical backing region laid out
contiguously after its caller's region. Entering it advances the managed frame
pointer to that region and initializes its structural metadata. Returning from
it restores the caller's frame pointer. Canonical values in both regions may
remain stale until publication is required.

### Deoptimization and commit boundaries

Each semantic bytecode operation has a commit boundary. Before its final
effectful action runs, failed speculation can reconstruct the state before the
bytecode and retry it in the interpreter. Once an effect has occurred, retrying
the bytecode may duplicate Python-visible behavior.

Exit state must therefore distinguish:

- pre-effect exits that resume at the current bytecode;
- exits after a committed result that resume at a later bytecode state;
- exceptional exits that preserve pending exception state without repeating an
  already-performed effect.

Every deoptimizing check retains a bytecode origin and logical frame state.
Moving or commoning checks must preserve a legal bailout state, not merely the
same successful-path computation.

### One managed calling convention

Interpreted and compiled Python calls initially use the existing managed calling
convention:

- arguments pass through the stack-backed outgoing argument window;
- moving the frame pointer establishes the callee frame;
- the return value passes through the accumulator in its canonical machine
  register;
- the caller publishes state before a call that may reclaim or enter arbitrary
  code.

This convention applies to interpreted-to-compiled, compiled-to-interpreted,
and compiled-to-compiled calls. Cross-mode stubs may select the execution engine
and continuation, but do not translate between separate argument ABIs.

Frames already have interpreted and compiled return-PC slots. An inlined frame
initializes its return FP, return PC, and return `CodeObject` exactly as a real
activation would. The managed frame pointer always identifies the innermost
active logical Python frame, including an inlined one. A real callee therefore
returns normally to that inline frame; the lowered inline return later restores
the outer frame pointer and branches to its caller continuation.

Compiled-to-compiled performance should initially come from inlining rather
than a second fast-entry ABI.

Native and C++ calls remain a separate ABI boundary. State must be published
before any native call that may allocate, reclaim, call Python, or otherwise
require managed roots.

A real Python call can also be the successful action of any overloaded
operator IC. Publication before such a call is continuing fast-path code, not a
failed-speculation side exit: it synchronizes dirty canonical homes, invokes the
callee, and then resumes compiled execution on normal return. It cannot be
delegated to the non-returning deduplicated recovery tails used by guards.

If the call occurs inside one or more inlined functions, continuing publication
synchronizes dirty homes across the entire active outer-to-inner frame chain.
The frame regions already exist, so this is multi-frame synchronization and
metadata finalization rather than allocation of missing frames.

Native ABI preservation and runtime root discovery are separate concerns.
Callee-saved registers can preserve caller values across a tiny function, but
under canonical publication the collector still cannot discover a managed
value that exists only in one of those registers. Publication need not evict the
register cache or reload it after a non-moving reclamation; it still adds stores
that may dominate the cost of a very small callee.

Inlining removes this boundary and is expected to cover important tiny Python
functions when their targets and bytecode are visible. A certified compiled
no-safepoint entry could also use only native ABI preservation, but it would
need a transitive guarantee that the selected path cannot reclaim, enter
Python, poll, or deopt directly into the interpreter. A failed entry guard would
use the publishing generic path instead.

Whether inlining and such leaf entries cover enough real calls to make
unconditional publication practical is deliberately open. JIT compilation
makes small callees more visible than separate static compilation does in
principle, but the design should not depend on that theoretical advantage
without measurements.

Trusted native calls have a related but firmer distinction. A semantic
descriptor certified `NoSafepoint`, `NoReclaim`, and `NoCallPython` requires only
native ABI call-clobber handling; it does not publish canonical state. Unknown
or safepoint-capable trusted calls remain conservative publication boundaries.

### Exceptions and observability

General compiled exception handling is deliberately parked. The current
direction is to leave compiled mode when an operation raises and use the
interpreter's existing exception tables and unwinding. The exact handoff after
an effect has committed remains open.

Tracing, traceback construction, stack inspection, and similar facilities may
require canonical frames and bytecode PCs even without failed speculation. A
plausible initial policy is to treat their activation as a deoptimization
request. This is not yet a firm design decision.

## Compiler Pipeline and Phase Ownership

### Semantic IR

Semantic IR is a parsed SSA representation of bytecode semantics, not a wrapper
around the encoded bytes in `CodeObject::code`. Its atomic operations resemble
the semantic bytecode operations emitted by the high-level compiler.

Semantic IR owns:

- parsing encoded bytecode into basic blocks;
- SSA construction for the accumulator and bytecode registers;
- compilation-local inline-cache specialization plans;
- type and shape inference;
- context-sensitive inlining;
- logical bytecode frame states.

### Guard IR

Guard IR expands atomic semantic operations into checks and smaller actions.
It owns:

- proof-producing guards and deoptimization exits;
- realization of demanded type partitions as control flow;
- explicit operation effects and dependencies;
- common proof elimination and effect-aware code motion;
- ordered calls, operations, and commit points.

Guard IR carries the same semantic facts and partition identities as Semantic
IR. Lowering does not translate them into a second type system.

### Machine IR

Machine IR is a machine-oriented SSA or virtual-register representation. It
owns register classes, target operations, calls, flags, addressing constraints,
spills, branches, register allocation, scheduling, and final encoding.

Machine copies, spills, and reloads change physical location, not Python value
identity. Machine IR should remain primarily a lowering and allocation
representation unless measurements justify broader machine-level optimization.

## Shared IR Foundations

### Ordered list-based SSA

The canonical function representation is a CFG of basic blocks with an ordered
instruction list in each block. Instructions have SSA operands and results.
Blocks begin with parameters or phi nodes and end with explicit normal-flow
terminators. Instructions may also carry explicit non-returning deoptimization
side exits.

The list is the current schedule. SSA edges expose value dependencies, proof
values expose guard dependencies, and effect annotations constrain movement.
List order does not create false dependencies between independent pure
operations; it records their chosen placement until a pass deliberately moves
them.

This representation fits clovervm because:

- guards have bytecode locations and bailout frame states;
- effectful operations create commit boundaries;
- shape and validity facts have control- and effect-bounded lifetimes;
- calls and safepoints require canonical publication;
- moving an instruction changes what is live at a deoptimization point;
- generic Python arithmetic may call overloaded methods, making source order a
  useful conservative default.

Out-of-order AArch64 and x86-64 cores recover substantial instruction-level
parallelism dynamically. A temporary DAG or sea-of-nodes form may still help a
narrow optimization or instruction-selection task, but it is not the canonical
whole-function representation.

Ordered lists are used at every level. Semantic IR begins in decoded bytecode
order, Guard IR orders checks and effects, and Machine IR records the final
target schedule with room for measured local scheduling.

### Preserve semantic value identity

Operations that only move an existing Python value do not create new semantic
SSA identities:

- `Ldar`, `Star`, and `Mov` update the accumulator/register environment to
  reference an existing value;
- guards refine facts about an input rather than return a narrowed replacement;
- expanding a bytecode preserves the semantic identity of its result;
- only genuine producers and non-trivial merges introduce identities;
- trivial phis with identical incoming values are eliminated.

For example:

```text
LdaSmi 1       accumulator -> %v1
Star r2        r2          -> %v1
Ldar r2        accumulator -> %v1
Mov r2, r3     r3          -> %v1
```

All four interpreter locations refer to the same semantic value `%v1`.

Canonical slots belong to logical frame states, not SSA values. One value may
appear in several registers or in several inlined frames, while one canonical
slot denotes different SSA values at different program points.

### Typed identities and deterministic traversal

Compiler objects use strongly typed integer identities rather than pointer
identity:

```text
SemanticNodeId, SemanticValueId, SemanticBlockId
GuardNodeId,    GuardValueId,    GuardBlockId
MachineNodeId,  MachineValueId,  MachineBlockId

PartitionId
FrameStateId
InlineFrameId
ResumeStateId
RecoveryPlanId
```

Node, value, and block IDs are specific to their IR level and cannot be mixed
implicitly with one another or with raw integers. Partition IDs are
compilation-wide because Semantic and Guard IR refer to the same logical
partitions. IDs are allocated monotonically in a deterministic construction
order and are not reused during a compilation.

Node identity and semantic value identity remain distinct. Replacing an
instruction creates a new node ID. A replacement that preserves the same
semantic result may deliberately retain its result `ValueId`; an independently
produced value receives a new one.

Compilation behavior must not depend on pointer addresses or hash-table
iteration order. Pointer addresses are neither compiler identities nor analysis
keys. Passes traverse blocks, nodes, edges, and worklists in defined orders,
using typed IDs as stable tie-breakers. If a hash table is needed, any results
that affect compiler output are ordered by typed ID before use. Dense side
tables should use IDs directly as indexes. Dumps and diagnostics print typed IDs
rather than addresses.

IR nodes, partition anchors, frame states, and related compilation objects have
compilation-scoped lifetime. Their IDs remain valid for that lifetime. The
container and allocation strategy used to provide this lifetime is an
implementation detail rather than an IR design constraint.

### Immutable nodes, mutable graphs, and analysis side tables

IR nodes are immutable descriptions of operations. Their operation kind,
operands, results, bytecode origin, logical frame state, intrinsic effects,
semantic descriptor, guard obligations, and partition IDs are fixed when the
node is constructed. A transformation changes any of these properties by
constructing a replacement node.

Graph structure remains mutable. Block instruction lists, predecessor and
successor sets, placement, definition indexes, and use indexes are maintained by
the IR editor. Replacing a node updates these structures transactionally; it
does not mutate the old node in place.

Derived knowledge is not written into immutable nodes. Analyses own
generation-scoped side tables such as:

```text
ValueId     -> TypeEvidence
NodeId      -> RefinedEffects
PartitionId -> ConditionalFacts
```

Fixed-point inference may update these tables repeatedly without rebuilding
nodes. Intrinsic effects remain a conservative immutable operation contract;
contextual effect refinements belong to analysis. Selecting a genuinely more
specific semantic operation, such as replacing a generic call with recognized
float addition, creates a new node with the corresponding intrinsic contract.

### Mutable CFG and control-flow-producing lowering

The bytecode CFG is an initial scaffold, not a fixed graph. Every lowering level
may introduce, remove, clone, and restructure control flow. This is required for
polymorphic ICs, overflow exits, inlining, out-of-line calls and reification,
future exception handling, and machine slow paths.

A lowering may replace one operation with an arbitrary CFG region. The CFG API
therefore needs first-class operations to:

- split a block at an instruction;
- insert branches and joins;
- add, remove, and redirect edges;
- update block parameters and phi inputs;
- clone and splice regions;
- attach bytecode origins and frame states to new exits.

Major representation boundaries normally build fresh destination CFGs.
Semantic-to-Guard and Guard-to-Machine lowering leave their source graphs intact
and naturally support one-to-region translation. Optimizations within one IR
may use an in-place CFG editor.

Deoptimization exits must remain visible to correctness and frame-state
analysis, but need not be ordinary successors in the normal CFG used for loops
and dominance. An ordered guard may own an explicit non-returning side exit and
frame state while successful execution falls through.

### Analysis invalidation

Dominance, loop structure, reverse postorder, propagated facts, refined effects,
and partition state are derived from the current IR. The initial implementation
invalidates them broadly and recomputes lazily.

A function has an IR mutation generation, and cached analyses record their
source generation. Inserting, removing, or replacing a node; changing a
definition; associating a new partition anchor through node replacement; or
structurally editing the CFG advances the generation through the official IR
editor. Requesting stale analysis recomputes it. Passes must not mutate nodes or
graph structures directly.

Verification at pass boundaries should require:

- exactly one live definition for every reachable SSA value;
- every referenced partition to have a live defining object or realized region
  in that IR;
- no reachable conditional fact to depend on a disconnected partition anchor;
- partition definitions to dominate conditional uses where required;
- every child partition to name an existing parent case and be defined within
  that case's scope;
- partition parent links to be acyclic;
- child facts not to escape their inherited parent context without the required
  joins;
- every referenced case and realized case edge to remain valid;
- values named by conditional facts to remain valid in their scopes;
- every consumed analysis result to match the current IR generation.

Narrow preservation declarations or incremental maintenance may be added later
if broad invalidation is measurably expensive.

### One semantic type system

Semantic IR and Guard IR share `ValueFacts`, `TypeEvidence`, type-partition
identities, and their join and refinement rules. The physical split belongs to
the IR, not to a separate Guard-only type system.

Proof values record why Guard IR may rely on a fact at a particular use; they do
not introduce another notion of type. Machine IR has a different concern:
physical representation classes such as tagged `Value`, `Float64`, address, and
condition code. Python-level facts may remain as lowering and recovery metadata,
but Machine IR does not define another Python type lattice.

## Semantic IR

### Parsed bytecode and inline-cache snapshots

Parsing decodes instructions, forms blocks and edges, records bytecode PCs, and
snapshots relevant inline-cache semantics into compilation-local data. Encoded
instructions, operand bytes, and runtime cache arrays are inputs to parsing,
not the optimized representation.

The snapshot captures the semantic content needed for compilation rather than
blindly copying mutable runtime cache structs. A specialization plan records:

```text
required predicates
selected successful action
facts true on the successful continuation
evidence provenance
```

The JIT compiles successful IC paths and deoptimizes on misses. It does not
reimplement the generic Python protocol at every compiled operation. Caller
facts may refine a plan but cannot invent a trusted handler or Python target;
the selected action remains justified by cache feedback or the runtime's
trusted resolution mechanism.

### SSA construction and logical frame states

Function arguments are semantic definitions at function entry:

```text
%arg0 = Parameter 0
%arg1 = Parameter 1
```

Their physical values may remain in canonical argument slots until first use.
Semantic definition and machine residency are separate.

During construction, an environment maps the accumulator and bytecode registers
to SSA values. A live location with different incoming values receives a block
parameter or phi at a CFG join. Construction is pruned so dead interpreter
locations do not receive unnecessary phis.

Each recoverable bytecode boundary has an immutable, structurally shared logical
frame state:

```text
FrameState:
    CodeObject
    bytecode pc
    parent FrameState       # inlined caller
    accumulator -> SSA value
    register 0  -> SSA value
    register 1  -> SSA value
    ...
```

Sparse structural sharing avoids copying the complete register mapping at every
bytecode. A frame state describes interpreter meaning; it does not assert that
the corresponding canonical slots are currently synchronized.

### Trusted handlers and semantic recognition

Selected operator ICs name trusted native handlers. The JIT may recognize
specific handlers and replace a generic call with specialized IR, but
recognition is explicit and conservative:

```text
trusted handler pointer + arity
    -> runtime-neutral trusted semantic descriptor
    -> JIT-specific lowering
```

A descriptor identifies the semantic operation, operand convention, coercion
case, result kind, and conservative effects. Float-float addition and
float-intlike addition are distinct semantic cases even if their final machine
sequences overlap.

The owning builtin type declares or registers the meaning of its handlers.
Type-specific coercion, reflected ordering, and handler semantics remain in
that layer. The JIT maps descriptors to IR nodes. Trusted handlers do not name
JIT opcodes directly.

Unknown handlers remain generic `TrustedFunctionCall`s. Recognized handlers
retain every shape and validity guard required by the IC.

### Value facts and inference

Types and shapes describe SSA values, not bytecode slots. An operation declares
how operand facts produce inherent result facts:

```text
infer_result(operation, operand facts) -> ValueFacts
```

Examples include:

```text
ConstantSmi  -> exact SMI
ConstantNone -> exact None
CreateTuple  -> exact tuple shape
```

The current facts for a value live in type-analysis side tables keyed by its
typed `ValueId`; they are not mutable annotations on the defining node.

A minimal bounded fact lattice is:

```text
Bottom                  # unreachable or no possible value
ExactConstant(Value)
ShapeSet{ShapeKey, ...}
Unknown
```

`ExactShape(ShapeKey)` is a singleton shape set. Joining different exact shapes
produces their union; an excessively large set may widen to `Unknown`. Joining
an unreachable state with a reachable fact yields the reachable fact. Integer
ranges, truthiness, callable targets, and other domains may be added later.

Inherent facts are valid everywhere their defining value is available.
Flow-sensitive refinements live in block or edge environments. At a merge,
possible-value sets join by union, while predicate facts survive only when every
incoming path establishes them.

Type propagation is forward abstract interpretation over a block worklist.
Instruction transfer functions update facts, branches refine outgoing states,
and successor states join until a fixed point. The bounded initial lattice
should stabilize without special loop widening.

### Likely and guaranteed evidence

Likely and guaranteed evidence use the same fact lattice. They differ in
epistemic status:

```text
TypeEvidence {
    guaranteed: ValueFacts
    likely: ValueFacts
}
```

A guaranteed union is exhaustive: `SMI | Float` excludes every other shape on
that continuation. A likely union identifies profitable cases but does not
exclude other runtime values. Likely evidence can select a specialization and
create guard obligations; only guaranteed evidence justifies specialization
without a new guard. No numeric confidence is required.

Likely evidence retains provenance such as an IC or caller context. Propagation
must not amplify evidence by cycling it through phis, SSA uses, recursive
inlining, or repeated analysis. More-specific caller evidence may replace
less-specific aggregate callee feedback, but semantic actions still require
trusted runtime resolution.

### Type partitions

The following design is plausible and increasingly coherent, but its concrete
representation and complexity limits remain tentative.

Every partition has an immutable, compilation-wide anchor:

```text
PartitionAnchor {
    PartitionId
    optional parent PartitionCaseRef
    cases
    semantic provenance
    optional derived-from PartitionId
}
```

IR objects and type-analysis side tables refer to the anchor by `PartitionId`.
A polymorphic semantic operation defines the anchor for its latent cases. A
join or block-header control object defines the anchor for a predecessor
partition. Each IR maintains a deterministic index from `PartitionId` to its
local defining object or realized CFG region. The anchor does not contain a
mutable back-pointer to whichever node currently represents it.

`PartitionCaseRef` identifies one case of another partition. It scopes a child
anchor beneath that case without requiring the immutable parent anchor to be
mutated when children are discovered. A deterministic analysis index may
enumerate the child anchors of each case.

Semantic and Guard IR use the same partition ID for one logical choice. Lowering
projects the Semantic definition to a Guard-local node or region without
changing that identity. A semantics-preserving node replacement may explicitly
reuse the same partition ID. Cloning a discriminator into independently
executed choices creates fresh partition IDs, optionally retaining
`derived-from` provenance.

Bytecode PCs remain semantic origins and bailout locations, not partition
identities. Inlining can create several instances of one bytecode, a single
bytecode can supply several partitions, and synthetic joins may have no direct
bytecode operation at all.

Per-value unions lose correlations between alternatives. A polymorphic IC may
describe:

```text
case 0: (Float, Float) -> FloatAdd        -> result Float
case 1: (Float, SMI)   -> FloatAddWithSMI -> result Float
```

Flattening this to `lhs: Float` and `rhs: Float | SMI` loses the association
between operand combination, semantic action, and result. The specialization
plan therefore retains finite correlated cases:

```text
SpecializationCase {
    operand predicates
    semantic action
    successful-continuation facts
    evidence provenance
}
```

These cases form a **type partition**: an abstract branch recorded by shared
Semantic/Guard type analysis without adding speculative Semantic CFG edges.
The partition supplies both joined facts and facts conditional on a named case:

```text
partition P:
    case P.float:
        a: Float
        b: Float
        result: Float

    case P.smi:
        a: SMI
        b: SMI
        result: SMI

unconditional:
    a: Float | SMI
    b: Float | SMI
    result: Float | SMI
```

Existing program control flow creates the same abstraction. Multiple phis at
one join share a predecessor partition:

```text
then:
    a1: SMI
    b1: Float

else:
    a2: Float
    b2: SMI

join:
    a = phi(a1, a2)       # guaranteed SMI | Float
    b = phi(b1, b2)       # guaranteed Float | SMI

partition P:
    P.then: a is SMI,   b is Float
    P.else: a is Float, b is SMI
```

Independent unions appear to allow four combinations; the partition retains
the two environments that can actually occur. Conditional inference can then
preserve facts such as "if `a` is Float in this case, `b` is also Float."

Partitions are recursive. Nested control flow and specializations retain their
structure rather than flattening into the Cartesian product of every leaf. For
example:

```text
partition P_x:
    case P_x.true:
        facts:
            x is Truthy

        partition P_y [parent = P_x.true]:
            case P_y.true:
                facts:
                    y is Truthy

            case P_y.false:
                facts:
                    y is Falsy

    case P_x.false:
        facts:
            x is Falsy
```

Facts under a child case inherit the complete parent environment. The context
of `P_y.false` is therefore the partition path
`P_x.true / P_y.false`, under which both `x is Truthy` and `y is Falsy` hold.
Following immutable parent-case links reconstructs this path; it need not be
stored redundantly on every conditional fact.

A case may scope several independent child partitions. Each child remains a
separate anchor rather than forcing their alternatives into one flat case set.
Joins are performed at the appropriate nesting level: child alternatives join
within their parent case before that parent is joined with its siblings.

The initial design restricts partitions to finite cases originating in known
structures such as polymorphic ICs, existing CFG edges, and inlined call-site
specializations. Recursion means finite nesting, not cyclic anchors. A loop
backedge must join, widen, or discard conditional structure rather than create a
parent-case cycle. The system does not attempt arbitrary logical implication
solving.

When a type-sensitive consumer demands realization, that demand traces back to
the defining partition anchor. Guard lowering therefore knows the earliest
logical point at which to introduce a discriminator, or which existing
predecessor edges already embody the cases.

Demand may realize only part of a partition tree. A consumer needing the
`P_x` distinction can split the outer partition while leaving `P_y` latent.
Within `P_x.true`, a later consumer may realize `P_y` locally. This preserves
correlation without generating all leaf combinations eagerly.

IC observations alone do not make the joined result globally guaranteed. For a
speculative partition, result facts become guaranteed only after Guard IR has
checked that a supported case applies; unmatched cases deoptimize. A genuinely
exhaustive guaranteed partition may use elimination: disproving one of two
cases proves the other without another runtime test.

### Context-sensitive inlining

Inlining binds callee parameter uses directly to caller argument SSA values. IR
construction accepts an incoming abstract state:

```text
bytecode register -> SSA value + available facts + guard obligations
```

Caller evidence and provenance become entry information for the inlined callee.
Redundant callee requirements may be removed under inherent or already-proven
facts, subject to effect and stability rules. Obligations remain attached until
Guard IR makes their checks explicit.

Inlining removes machine call and dispatch overhead, not Python frame
structure. Each inline instance receives a fixed canonical backing region in
the compiled frame layout. Inline entry advances the managed FP, initializes
the callee and return metadata, and binds the argument SSA values to the new
logical frame. It need not immediately store those arguments or other slot
values. Inline return places the result in the accumulator, restores the return
FP, and branches to the caller continuation.

Outer and inner frame slots may both be stale while reclamation is impossible.
A reclaiming call or other publication boundary synchronizes every active
logical frame. If the inline body reaches no such boundary, many of its slots
may never be written before the frame is popped. Before publication, every slot
the runtime will scan must nevertheless contain a valid tagged value or the
interpreter's ordinary empty or unbound sentinel.

Facts belong to a compilation and inline context, not globally to a
`CodeObject`. The same function may be compiled standalone or under several
call-site specializations.

Inference and inlining may run iteratively: propagate, inline newly eligible
calls, rebuild affected analyses, and propagate again until stable or a budget
is exhausted.

## Guard IR

### Expanding semantic operations

Guard lowering breaks an atomic bytecode operation into checks and its selected
action. For example:

```text
Add
    -> ShapeKeyCheck
    -> ShapeKeyCheck
    -> ValidityCellCheck          # when required
    -> SmiAdd | recognized operation | TrustedFunctionCall | PythonFunctionCall
```

Pre-operation checks have no Python-visible side effects. Only the selected
action may perform the operation's effects. Failed checks return to the
original bytecode so the interpreter can run the generic protocol.

An action may have its own speculative exit. SMI overflow, for example, returns
to the original bytecode, which may create a heap integer or select another
Python path. The action and all guards required to justify it remain one
semantic unit.

### Realizing type partitions

Guard IR alone decides whether an abstract partition becomes actual control
flow. Realization is consumer-driven rather than an automatic response to a
union type:

- a union-transparent consumer, such as storing a tagged `Value` in a list,
  accepts the union without a split;
- a type-specialized consumer, such as integer-versus-float arithmetic, may
  demand different arms;
- an IC partition emits guards and bailout edges that select supported cases;
- a partition inherited from existing CFG edges may duplicate a consumer onto
  predecessor edges without new type checks;
- each realized arm converts its conditional facts into guaranteed facts and
  explicit proofs.

Recursive partitions lower recursively. Realizing a parent creates or reuses
its case arms and establishes the inherited fact environment in each arm. A
child can be realized only within its parent case, where it introduces its own
nested split. Unneeded child partitions remain latent even when their ancestors
have become CFG.

The arms may merge immediately after one operation or remain separate across a
larger cloned region when several consumers benefit. Code duplication is a
profitability decision with an explicit growth budget. A partition never
demanded by a type-sensitive consumer remains metadata and produces no runtime
branch.

When Guard IR realizes a partition, it continues propagation in the shared
fact lattice. CFG edits, cloning, and joins must invalidate or update the same
analysis used by Semantic IR.

### Operation effects and dependencies

An effect summary says what an operation may change. A dependency summary says
what it observes or assumes. Moving an operation is legal only when its
dependencies do not intersect crossed effects and when commit and control
ordering remain valid.

Relevant properties include whether an operation:

- reads or writes memory;
- may change object shapes, including through aliases;
- may invalidate lookup assumptions or validity cells;
- may call Python;
- may allocate, reclaim, or reach a safepoint;
- may raise or deoptimize;
- has an irreversible Python-visible effect;
- is pure arithmetic.

Operation definitions provide precise defaults where possible.
`ShapeKeyCheck`, for example, has a standard dependency and deoptimization
shape. Recognized operations inherit effects from semantic descriptors. Python
calls and unknown operations begin maximally conservative.

This conservative intrinsic summary is part of the immutable node. A
generation-scoped effect-analysis side table may derive a more precise summary
from current facts, but it does not erase effects from the node. When
specialization selects a different semantic operation with a genuinely narrower
contract, the pass constructs a replacement node of that operation kind.

Effect implications are centralized. `MayCallPython`, for example, implies
broad heap access, possible shape mutation, validity invalidation, raising, and
safepoint behavior. Unless the call has been eliminated by inlining or replaced
by a certified no-safepoint entry, it also requires continuing canonical
publication before the call. An omitted effect is a correctness bug, not merely
a missed optimization.

### Shape facts

Shape facts have different lifetimes.

**Inline values.** For an SMI or another inline value, the shape follows from
the bits. It remains valid while the SSA value is unchanged.

**Mutable-shape heap values.** General objects may change shape through property
mutation, supported `__class__` assignment, or aliases. Dominance alone is
insufficient: a shape fact can cross only operations proven not to change that
object's shape, including indirectly.

**Stable-shape heap values.** Some exact heap values, such as tuples, have
lifetime-stable instance shapes. Once proved, that fact survives calls for the
unchanged value. Stability must be a runtime invariant, not merely the absence
of an ordinary transition flag.

Shape stability does not imply lookup stability. A tuple instance can retain
its shape while mutation of its class or MRO invalidates a cached lookup. Shape
and validity facts remain independent.

### Validity-cell facts

Validity cells capture assumptions about non-local mutable runtime state. The
initial optimizer is conservative:

- validity checks may be reused across pure arithmetic and similarly constrained
  operations with no relevant memory access;
- Python calls, arbitrary helpers, and possible non-local mutation are barriers
  unless proved otherwise;
- optimization initially emphasizes local redundancy elimination rather than
  aggressive loop hoisting.

Validity optimization should not force an elaborate memory model before
evidence shows it is worthwhile.

### Proof values and guard optimization

Proof values appear when Semantic IR lowers to Guard IR. A guard produces an SSA
proof rather than a narrowed runtime value:

```text
%lhs_is_smi = ShapeKeyCheck %lhs, Smi
%rhs_is_smi = ShapeKeyCheck %rhs, Smi

%result = SmiAdd %lhs, %rhs
    requires %lhs_is_smi, %rhs_is_smi
```

Proofs have no runtime `Value` representation. They make the dependency between
a guard and specialized operation explicit without changing the guarded
value's identity.

Proof kinds may include relevant effect state:

```text
InlineShapeProof(value, shape)
StableShapeProof(value, shape)
MutableShapeProof(value, shape, shape-effect state)
ValidityProof(cell, validity-effect state)
```

Operations that may invalidate an assumption advance its effect state. An IR
verifier rejects a required proof that refers to the wrong value or property,
fails to dominate its use, or depends on obsolete state.

Optimization commons proofs rather than blindly hash-consing guards. A later
guard can be removed when an equivalent valid proof dominates it. The earlier
guard retains its original bytecode bailout location. Equivalent sibling guards
do not dominate one another; hoisting them is a separate transformation that
must establish a legal bailout state and safe replay of crossed work.

Proofs disappear before machine lowering. They constrain transformations but
consume no registers and emit no code beyond remaining guards.

## Machine IR and Value Representation

### Generated side exits and recovery plans

Machine IR retains an explicit non-returning `DeoptExit(FrameStateId)` until
register allocation determines the location of every value needed for recovery.
Post-allocation exit expansion combines three inputs:

```text
logical FrameState:
    active frame chain
    (frame instance, canonical slot) -> ValueId
    innermost accumulator            -> ValueId

machine location state at the exit:
    ValueId -> register | spill | canonical slot | constant | recipe

synchronized state:
    (frame instance, canonical slot) -> ValueId currently stored there
```

The resulting recovery plan is the parallel assignment needed to make logical
and synchronized state agree. It includes dirty canonical-slot writes, the
accumulator source, final bytecode and return metadata for every active frame,
and any future reification recipes. Active inline frames already have canonical
backing regions and do not require allocation or layout reconstruction. Machine
liveness at the exit includes the transitive closure of values required by the
plan.

These tables are compiler inputs to generated cold code, not runtime stack maps.
After the generated sequence publishes canonical state, the generic runtime
does not inspect optimized locations.

Side-exit code is factored into three levels:

```text
guard or speculative failure
    -> resume-state stub
    -> shared recovery-plan block
    -> common interpreter handoff
```

A resume state contains more than a numeric bytecode PC:

```text
ResumeState {
    CodeObject
    bytecode pc
    inline instance
    exit kind              # pre-effect, post-commit, exception, ...
}
```

All failures returning to the same logical bytecode state may share its small
stub. The stub installs the interpreter resume state and jumps to a recovery
block. Different resume states may share that block when their exact
post-allocation recovery operations are identical. If one resume state is
reached with different machine recovery plans, code generation emits a distinct
stub for each `(ResumeStateId, RecoveryPlanId)` pair.

A recovery plan is interned from a canonical, deterministic signature containing
its destination homes, physical or rematerialized sources, representations,
accumulator action, frame metadata finalization, and reification requirements.
Interning assigns a typed `RecoveryPlanId`; it never depends on pointer hashes
or hash-table iteration order. Identical plans share one emitted block, and all
ordinary recovery blocks tail into a common interpreter-dispatch handoff.

Canonical-slot writes are parallel assignments. A source home may be overwritten
before its old value has been copied elsewhere, so exit expansion uses ordinary
parallel-copy scheduling.

Each backend provides one dedicated exit scratch general-purpose register. It is
excluded from ordinary allocation, never appears as a live recovery source, and
may be clobbered by resume stubs and recovery blocks. It is available for
constructing bytecode PCs, breaking parallel-copy cycles, forming addresses and
constants, and reaching the final dispatcher. The initial AArch64 backend
reserves this register globally; avoiding that reservation is not worth adding
complexity to cold exits on a register-rich target.

### Continuing multi-frame call publication

Safepointing Python calls use the same logical-versus-synchronized frame
analysis as side exits, but not the same non-returning code shape. A
call-publication plan covers every active logical frame, including outer frames
whose slots remain dirty while execution is inside an inlined callee.

For example, if `B` is inlined into `A` and calls a non-inlined `C`, the active
chain before the call is `A -> B`. Publication synchronizes dirty homes in both
backing regions, publishes `B`'s accumulator, establishes `C`'s outgoing
arguments, and then performs the call. Normal return restores `FP = B` and
continues compiled execution in the inlined body. A later inline return restores
`FP = A`.

The planner may share frame-difference, location, and parallel-copy machinery
with side-exit recovery. Its generated instructions remain at the continuing
call site because they must preserve post-call liveness and return to compiled
code; they are not delegated to deduplicated cold exit tails.

Without inlining, `A` would publish before calling `B`, and `B` would publish
again before calling `C`. Inlining can replace those two boundaries with one
larger multi-frame publication. The eventual inlining cost model should account
for both eliminated boundaries and the dirty homes at reclaiming calls that
remain inside the inline region.

### Tagged `Value` baseline

The existing `Value` representation is the initial JIT representation:

- heap pointers stay tagged and can be dereferenced in their existing form;
- SMIs remain shifted left by five bits;
- SMI addition and subtraction usually operate directly on encoded values and
  use native overflow flags;
- multiplication and address indexing shift only where required;
- tagged values move directly between registers and canonical frame homes.

The initial JIT uses boxed `Value`s exclusively. It does not require general
unboxing to execute ordinary compiled code.

### Semantic identity and machine representation

Semantic identity is separate from physical representation. A value does not
own a canonical bytecode slot and need not map to exactly one virtual register.
Later lowering may keep several representations simultaneously:

```text
semantic %v
    -> %v.boxed : Value
    -> %v.f64   : Float64
```

Machine liveness is separate from semantic liveness. A function argument exists
semantically at entry while remaining in its canonical slot until a guard or use
makes loading it profitable.

### Future unboxed floats and reification

Unboxed floats are an advanced optimization, not an initial requirement. The
design distinguishes:

1. an unboxed cache of an existing boxed float, where deoptimization discards
   the cache and retains the original identity;
2. a new unboxed arithmetic result representing a virtual Python float, which
   must be reified before deoptimization, escape, or identity-sensitive use.

A virtual result has one semantic identity. If it appears in multiple bytecode
slots or inlined frames, reification allocates one box and places that same
`Value` everywhere. Boxing each occurrence independently would break `is`.

Reification allocation may initially be treated as infallible because the
runtime requests a reclamation safepoint before memory exhaustion. The first
JIT needs no unboxed-float nodes or reification implementation; its IR must only
avoid precluding them.

## Inlined Frame Backing and Deoptimization

Inlining preserves the Python frame stack structurally. The compiled outer
frame reserves stable, contiguous backing regions for its active inline depth:

```text
outer frame A
inline frame B
inline frame C
...
```

Each inline instance has a fixed base offset and the ordinary interpreter frame
layout for its arguments, locals, temporaries, outgoing arguments, return FP,
return PC, return `CodeObject`, and other required metadata. The managed frame
pointer always identifies the innermost active logical frame.

Inline entry advances FP to the preallocated region and initializes the
structural callee and return metadata. Slot values may remain represented only
by SSA values in machine registers until publication. Inline return places the
result in the accumulator, restores the recorded return FP, and branches to the
compiled caller continuation. A real call made from an inline frame uses the
same convention and returns with one ordinary frame pop; it does not require a
depth-specific or double-pop return thunk.

Deoptimization distinguishes:

```text
logical frame state:
    the SSA value each interpreter-visible location denotes

synchronized frame state:
    the Value currently committed to each canonical frame home
```

A bailout inside an inlined callee may expose several bytecode frames. Once the
callee has performed effects, the caller's call bytecode generally cannot be
retried. Exit machinery therefore synchronizes every dirty active frame region,
sets the appropriate interpreted PCs and return metadata, reifies virtual
objects once per semantic identity, publishes the innermost accumulator, and
hands the already-backed frame chain to the interpreter.

Outer and inner regions may both remain stale until that boundary. This does
not require frame allocation or arbitrary per-exit layouts: only value
synchronization and metadata finalization are exit-specific. Inactive sibling
inline sites may eventually share backing regions when their lifetimes cannot
overlap, but assigning fixed distinct regions is the initial policy and their
stack cost contributes to the inlining budget.

## End-to-End Example: Polymorphic Add

A polymorphic addition illustrates the complete flow:

1. Semantic IR parses one atomic `Add`, records its bytecode PC and logical
   frame state, and snapshots its IC cases.
2. Trusted handler descriptors identify the semantic actions for those cases.
3. Type inference records likely or guaranteed operand facts, result unions,
   guard obligations, and any correlated type partition.
4. Inlining may refine the same plan using caller-context evidence.
5. A union-transparent consumer leaves the partition latent. A type-sensitive
   consumer asks Guard lowering to realize the relevant alternatives.
6. Guard IR emits shape and validity checks, bailout states, proof values, and
   specialized actions. Existing predecessor partitions may instead permit code
   duplication without new checks.
7. Failed pre-effect checks return to the original `Add`; overflow or committed
   exits use the appropriate bytecode state.
8. Machine IR selects tagged or future unboxed representations and assigns
   locations while preserving canonical publication requirements.
9. Post-allocation exit expansion interns the required recovery plans, emits
   resume-state stubs and shared synchronization blocks, and tails them into the
   common interpreter handoff.

## Deliberately Open Questions

### IR representation and optimization

- concrete storage layouts and APIs for SSA nodes and blocks;
- optimizer and register allocator organization;
- whether any narrow pass benefits from a temporary graph representation;
- when analysis preservation or incremental CFG maintenance is worthwhile.

### Type evidence and specialization

- whether explicit Guard IR proof SSA values are necessary, or can be unified
  with the shared fact and partition system while still making dominance,
  guard obligations, and effect-state validity mechanically verifiable;
- concrete representation and propagation limits for type partitions;
- profitability and code-growth policy for partition realization;
- depth, leaf-count, and propagation budgets for recursive partitions;
- joins of intersecting partition trees and loop-carried facts without
  combinatorial growth or cyclic anchors;
- compilation, invalidation, and lifetime rules for changing IC contents.

### Effects and runtime assumptions

- the precise effect taxonomy and alias model;
- the runtime classification of stable-shape values;
- how aggressively validity checks should eventually be optimized.

### Deoptimization and execution boundaries

- whether publication at every potentially safepointing Python call is
  affordable in practice, or whether the JIT eventually needs runtime
  stack/register maps, a fixed root-register convention, or broader certified
  no-safepoint entries;
- how often inlining actually removes small hot Python call boundaries, and how
  many dirty managed values remain live at the calls it does not remove;
- exact encoding of post-allocation machine locations, rematerialization, and
  future reification recipes in recovery plans;
- recovery-plan interning and code-size policy beyond exact-plan deduplication;
- normal compiled returns through the compiled-PC frame slot;
- compiled exception handling and cross-frame unwinding;
- final observability and tracing policy.

### Backend and code lifecycle

- backend selection and target-specific lowering structure;
- code memory management;
- tiering and compilation triggers;
- invalidation and lifetime of generated code.

These questions must be answered without weakening bytecode compatibility or
the single semantic type system shared by Semantic and Guard IR. Any future
replacement for canonical frame publication must provide equally explicit and
verifiable root-discovery and interpreter-recovery guarantees.
