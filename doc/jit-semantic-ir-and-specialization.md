# Semantic IR and Specialization

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | Not started |
| Scope | Optional Semantic IR, type evidence, correlated partitions, contextual inlining, and lowering into Core IR |
| Owning layers | Semantic IR owns high-level inference and specialization planning; Core IR owns executable checks, effects, Snapshots, and recovery |
| Validated against | N/A |
| Supersedes | Optional Semantic IR material formerly embedded in [JIT Compiler and IR](jit-compiler-and-ir.md) |

Semantic IR is an optional optimization frontend for the clovervm JIT. It
preserves atomic bytecode semantics long enough to perform type inference,
caller-context-sensitive inlining, and correlated polymorphic reasoning before
lowering into explicit Core IR checks and actions.

It is not required for useful compiled execution:

```text
initial path:
    decoded bytecode + IC snapshots -> Core IR -> backend

higher-effort path:
    decoded bytecode + IC snapshots -> Semantic IR -> Core IR -> backend
```

Both paths produce the same Core IR and use the same Snapshot, effect,
representation, and recovery contracts. This document records a plausible
future design so that the direct compiler does not preclude it. Complexity
budgets and some representations remain deliberately unsettled.

## Semantic Operations and Feedback

### Parsed semantic bytecode

The shared frontend decodes bytecode, forms basic blocks and edges, records
bytecode PCs, and captures compilation-local snapshots of relevant inline-cache
semantics. “Bytecode” at this level does not mean the encoded memory format. It
means a parsed representation close to the high-level compiler's semantic
operations.

Semantic IR initially preserves one atomic instruction for an operation such
as `Add`, `StoreAttr`, or a call. It does not yet expand that operation into
guards, overflow checks, helper calls, or side exits. This makes it possible to
change an operation's specialization plan or inline its target before choosing
the executable control flow.

### Inline-cache snapshots

Compilation copies semantic feedback rather than retaining mutable pointers to
runtime IC storage. A specialization plan records:

```text
required predicates
selected successful action
facts true on the successful continuation
evidence provenance
```

Caller evidence may select or refine a plan, but it cannot invent a trusted
handler or Python target. Every executable action remains justified by recorded
feedback or fresh trusted runtime resolution.

An eventual IC may represent:

```text
Uninitialized
Monomorphic(case)
Polymorphic(case...)
Megamorphic
```

Each case retains operand shapes, validity requirements, the resolved semantic
action, and successful-continuation facts. Selection follows these rules:

- a compatible monomorphic case wins over conflicting merely-likely context;
- guaranteed context incompatible with the only recorded case makes that case
  unreachable and requires fresh resolution or fallback;
- guaranteed context filters a polymorphic IC before likely context chooses a
  preferred remaining case;
- a megamorphic IC has no trustworthy small recorded subset, so context may
  propose cases only through fresh trusted resolution;
- absent usable feedback or context, compilation remains generic or exits to
  the interpreter.

Caller and callee evidence have different roles. Caller evidence describes one
inline context and may replace aggregate callee entry predictions. The IC at an
operation still owns the actions observed or resolved for that operation.

Under the initial no-safepoint compilation policy, “fresh trusted resolution”
means a certified read-only, non-allocating resolver that cannot request a
safepoint or execute Python. If no such resolver applies, compilation retains a
generic operation or falls back rather than entering an arbitrary runtime path.

### Provisional feedback stability

The runtime need not begin with polymorphic case arrays. A one-case IC can carry
a small saturating case-install count:

```text
0             uninitialized
1             monomorphic observation
2..threshold  replacement churn; polymorphism suspected
saturated     highly unstable or megamorphic-like
```

The count advances when the cached operand-shape tuple changes, not merely when
the same case is reinstalled after validity invalidation. A high count proves
instability, not true megamorphism: a one-case cache cannot distinguish `A/B`
alternation from many unrelated shapes. That is sufficient to trust a stable
case, treat a low-churn case cautiously, and prefer contextual evidence plus
fresh resolution under high churn.

This counter is a provisional feedback mechanism, not a committed cache layout.
It may later govern promotion from one case to a small case array and then to an
explicit megamorphic state.

## SSA and Logical State

Function arguments are SSA definitions at entry even when their physical values
remain in canonical argument slots until first use:

```text
%arg0 = Parameter 0
%arg1 = Parameter 1
```

Construction maps the accumulator and bytecode registers to SSA values. A live
location with different incoming values receives a block parameter, and each
predecessor supplies its argument with parallel-copy semantics. Pruned
construction avoids parameters for dead interpreter locations.

Semantic `ProgramValueRef`s are representation-free. They describe Python
program values and participate in the same generic SSA machinery without
choosing a Core encoding. Semantic-to-Core lowering builds fresh defs with
intrinsic `ValueRepresentation`s and selects the corresponding concrete Core
block-parameter kinds.

Semantic operations and recoverable bytecode boundaries retain structurally
shared logical `FrameStateId`s. The mutable construction environment also
tracks the current global accumulator and active inline frame chain. This state
describes interpreter meaning, not machine locations or synchronization.

Semantic IR does not need Snapshot instructions. Core lowering creates a
Snapshot only for a recovery state actually consumed by an emitted exit. The
Snapshot captures every program value required to reconstruct that state; each
guard or side exit consuming its `SnapshotRef` makes those captured values
transitive point uses at the consuming position.

## Value Facts

Types and shapes describe SSA values rather than bytecode slots. Each SSA value
has one guaranteed static fact in one analysis result. Narrowing through visible
control flow or a guard creates a new block parameter or refinement result; an
existing value does not acquire a different guaranteed type at another program
position.

Semantic value facts live in a concrete `SemanticValueAnalysis` attachment
indexed by typed Semantic `ProgramValueRef`s; they are not fields on the
physical instruction. Mutable inference updates private storage and publishes a
generation-checked frozen view. A structural edit makes old views stale; broad
recomputation is the baseline before a later pass consumes facts again.
Inference may later preserve unaffected entries, apply local transfer functions
to transparent edits, or revisit only affected dependents when measurements
justify that complexity. Conditional facts and partition state remain in their
own concrete Semantic analysis attachments. An operation supplies a transfer
function:

```text
infer_result(operation, operand facts) -> ValueFacts
```

A bounded initial lattice is:

```text
Bottom                  # unreachable or no possible value
ExactConstant(Value)
ShapeSet{ShapeKey, ...}
Unknown
```

`ExactShape` is a singleton shape set. Joins form unions and widen excessively
large sets to `Unknown`. Integer ranges, truthiness, callable targets, and other
domains should be added only when an optimization demonstrates their value.

Forward abstract interpretation processes a block worklist. Instructions
transfer facts, branches refine outgoing states, and successor states determine
block-parameter facts until a fixed point. The bounded initial lattice should
stabilize without a separate numeric probability system.

### Guaranteed and likely evidence

Guaranteed and likely evidence share the same `ValueFacts` lattice:

```text
TypeEvidence {
    guaranteed: ValueFacts
    likely: ValueFacts
}
```

A guaranteed union such as `SMI | Float` is exhaustive on that continuation. A
likely union names profitable cases but excludes nothing. Likely evidence may
choose a specialization and create guard obligations; only guaranteed evidence
removes the need for a guard.

Likely evidence is a preferred case key with provenance, not a numeric
probability. Propagation must not amplify it by cycling through block
parameters, loops, SSA uses, or recursive inlining. More-specific caller
evidence may replace aggregate entry feedback, while operation-level IC rules
still authorize the semantic action.

## Correlated Type Partitions

Per-value unions lose relationships between simultaneous alternatives. A
polymorphic operation might have:

```text
case 0: (Float, Float) -> FloatAdd        -> result Float
case 1: (Float, SMI)   -> FloatAddWithSMI -> result Float
```

Flattening this to `lhs: Float` and `rhs: Float | SMI` loses which semantic
action belongs to each case. A specialization therefore retains finite
correlated cases:

```text
SpecializationCase {
    operand predicates
    semantic action
    successful-continuation facts
    evidence provenance
}
```

A type partition is an abstract branch shared by Semantic and Core analysis. It
records unconditional joined facts plus facts conditional on named cases
without eagerly adding speculative Semantic CFG edges:

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

Conditional facts do not give one `ProgramValueRef` position-dependent types.
Realizing a case creates narrowed guard results or block parameters with their
own SSA identities and guaranteed facts.

### Stable partition anchors

Every partition has an immutable compilation-wide anchor:

```text
PartitionAnchor {
    PartitionId
    optional parent PartitionCaseRef
    cases
    semantic provenance
    optional derived-from PartitionId
}
```

Type side tables and both IRs refer to `PartitionId`. Each IR maintains a
deterministic index from that ID to its local defining operation or realized CFG
region. The anchor never contains a mutable back-pointer into one IR.

Semantic-to-Core lowering preserves a logical partition ID. Replacing an
operation may reuse it when the choice remains the same; cloning a discriminator
into independently executed choices creates new IDs with optional provenance.
Bytecode PCs remain origins and recovery locations rather than partition
identities.

The shared anchor prevents a rewrite from leaving the type system pointing at a
detached instruction. Verification requires every referenced partition to have
a reachable definition or realization in the current IR.

### Partitions from ordinary control flow

Existing branches produce the same abstraction. Several block parameters at a
join share a predecessor partition:

```text
then -> join(a_smi,   b_float)
else -> join(a_float, b_smi)

join(a, b):
    a: SMI | Float
    b: Float | SMI

partition P:
    P.then: a is SMI,   b is Float
    P.else: a is Float, b is SMI
```

The independent unions appear to permit four combinations; the partition
retains the two environments that can occur.

### Recursive partitions

Nested control flow and specialization produce recursive partitions:

```text
partition P_x:
    case P_x.true:
        x is Truthy

        partition P_y [parent = P_x.true]:
            case P_y.true:  y is Truthy
            case P_y.false: y is Falsy

    case P_x.false:
        x is Falsy
```

A child case inherits its complete parent environment. Parent-case links
reconstruct that context without copying it into every conditional fact. A case
may contain several independent child partitions; they remain separate rather
than becoming one Cartesian product.

Partitions are finite and acyclic. A loop backedge joins, widens, or discards
conditional structure rather than creating a parent cycle. The analysis is not
a general logical implication solver.

### Demand-driven realization

A guaranteed union alone does not justify code duplication. A union-transparent
use site, such as storing a tagged value into a list, can consume it directly.
Only a use requiring different machine operations for different cases
demands realization.

That demand traces to the partition anchor. Core lowering either uses existing
predecessor edges or introduces the discriminator at the logical definition.
It may realize only part of a recursive tree: an outer case can become control
flow while its child partition remains latent until a later use needs it.

For a speculative partition, joined result facts become guaranteed only after
Core checks that one supported case applies; unmatched inputs exit. An
exhaustive guaranteed two-case partition may use elimination: disproving one
case proves the other.

## Context-sensitive Inlining

Inlining binds callee parameters directly to caller SSA values and starts
callee construction with caller facts and guard obligations. Caller context may
remove redundant callee checks subject to shape stability, effects, and IC
authorization.

Inlining removes dispatch and machine-call overhead but preserves Python frame
structure. Each inline instance receives a canonical backing region, structural
caller metadata, and a logical `FrameState`. Slot values may remain only in SSA
locations until a publication or recovery boundary synchronizes every active
frame.

Facts belong to a compilation and inline context, not globally to a
`CodeObject`. A polymorphic function may therefore be compiled generically and
also inlined under a caller that guarantees SMI arguments.

Inference and inlining may run in bounded waves:

```text
propagate facts
    -> inline newly profitable calls
    -> rebuild affected analyses
    -> propagate again
```

The compiler imposes explicit iteration and graph-growth budgets. If the last
permitted wave still inlines code, it runs one final propagation,
canonicalization, CFG cleanup, and dead-code pass with inlining disabled.

## Lowering Into Core IR

Semantic lowering converts each atomic operation into an arbitrary Core CFG
region. It may introduce checks, branches, calls, overflow exits, and joins.
The source Semantic graph remains immutable while a fresh Core graph is built.

A monomorphic operation normally lowers to:

```text
Snapshot pre-bytecode state
    -> required shape checks returning narrowed values
    -> optional validity checks
    -> selected terminal action
    -> successful continuation
```

Only the terminal action performs Python-visible effects. Pre-effect failures
consume the original Snapshot and retry the semantic bytecode in the
interpreter.

A polymorphic operation may emit a discriminator and one Core region per
selected case. Core block parameters join their results. Existing control-flow
partitions may instead allow lowering to duplicate a use into predecessor
regions without adding a new runtime test.

Trusted handler recognition is semantic rather than opcode-based:

```text
trusted handler pointer + arity
    -> runtime-neutral semantic descriptor
    -> Core lowering
```

Descriptors declare operand conventions, coercion cases, result kinds, and
conservative effects. Unknown handlers remain generic trusted calls. Every
recognized handler retains the IC predicates and validity requirements that
justify it.

## Example: Polymorphic Add

An optimized polymorphic addition follows this path:

1. The frontend parses `Add`, records its bytecode origin, and captures its IC
   cases.
2. Semantic IR preserves one atomic operation and its logical frame state.
3. Type inference records guaranteed and likely facts plus any correlated
   partition.
4. Inlining may replace aggregate callee evidence with caller-context facts.
5. Union-transparent uses leave the partition latent; a type-sensitive use
   requests realization.
6. Core lowering creates the required Snapshot, narrowed case values, validity
   checks, branches, and terminal arithmetic actions.
7. Unmatched or failed pre-effect cases return to the original `Add` state.
8. The ordinary Core backend assigns representations and locations and emits
   recovery and resume machinery.

## Verification

When this frontend is instantiated, pass-boundary verification additionally
requires:

- every referenced partition to have a reachable defining object or realized
  region in the current IR;
- every conditional fact to refer to live values and a connected partition
  anchor;
- partition definitions to dominate conditional uses where required;
- every child partition to name an existing parent case and be defined within
  that case's scope;
- parent links to be acyclic;
- child facts not to escape their inherited parent context without the required
  joins;
- every referenced case and realized case edge to remain valid;
- each SSA value to have one guaranteed fact compatible with its def;
- every consumed inference result to match the current IR generation.

## Open Questions

- concrete storage for `ValueFacts`, evidence provenance, and guard obligations;
- the first useful shape-set width and widening rules;
- how to prevent likely evidence from cycling or being counted repeatedly;
- concrete partition anchor and conditional-fact APIs;
- realization profitability and code-growth policy;
- depth, leaf-count, and propagation budgets for recursive partitions;
- joins of partition trees at loops and intersecting control flow;
- feedback layout and promotion from one-case to polymorphic ICs;
- inlining budgets and the evidence required to make call targets eligible;
- whether implementation experience demonstrates enough value to build
  Semantic IR at all.

## Related Documents

- [JIT Compiler and IR](jit-compiler-and-ir.md)
- [JIT Compiler Bring-up Plan](jit-compiler-bring-up-plan.md)
- [Decision Log](decision-log.md)
