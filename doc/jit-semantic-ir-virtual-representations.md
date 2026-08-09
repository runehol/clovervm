# Semantic IR Virtual Representations

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | Not started |
| Scope | Virtual program values, representation selection, materialization identity, cross-block representation flow, and lowering from the optimizing Semantic IR tier into Core IR |
| Owning layers | Semantic IR owns logical values and representation planning; Core IR owns concrete represented values, executable conversions, Snapshots, and recovery computations |
| Related design | [Semantic IR and Specialization](jit-semantic-ir-and-specialization.md) |

## Decision

General virtual representation belongs to the optimizing Semantic IR tier. It
does not belong in direct bytecode-to-Core translation, and it should not be
grown out of a sequence of type-specific Core rewrites.

The two compilation paths are:

```text
tier 1:
    decoded bytecode + IC snapshots
        -> Core IR
        -> Machine IR
        -> machine code

tier 2:
    decoded bytecode + IC snapshots
        -> Semantic IR
        -> inference, inlining, and virtual representation planning
        -> Core IR
        -> Machine IR
        -> machine code
```

“Tier 2” describes a higher-effort compilation tier. It still lowers through
the same correctness-critical Core IR, recovery machinery, register allocator,
and backend. It is not a second machine backend or a second executable IR.

Tier 1 remains useful and deliberately direct. It begins with ordinary tagged
Python values, emits explicit guards and conversions, and applies local Core
optimizations. Tier 2 may begin with logical Python values and choose concrete
representations only after it has inferred enough semantics to make a global
choice.

## Motivation

The direct Core compiler initially emits the heaviest generally correct form:

```text
guard boxed operands
    -> unbox
    -> primitive operation
    -> box result
```

Core optimization can eliminate redundant guards, forward through local
`BoxF64`/`UnboxF64` pairs, carry F64 values through selected block parameters,
and sink snapshot-only boxes into transition programs. This works well inside
a block. It becomes progressively more complicated across joins and especially
loops.

The complication is not peculiar to floats. A later optimizer would encounter
the same problem for tuples, ordinary objects, lists with specialized storage,
inlined frames, and other values that can remain virtual until an observing use.
Growing one cross-edge rewrite for every object kind would make Core
optimization responsible for reconstructing high-level intent after lowering
has already committed to concrete allocations and representations.

Semantic IR has that intent before lowering. It is therefore the appropriate
place to start with the lightest valid representation and materialize only when
a use requires a real Python object.

## Logical Values and Concrete Forms

A Semantic IR `ProgramValueRef` denotes one logical Python value. It has no
intrinsic machine representation. Analysis may determine that the value has one
or more available concrete forms:

```text
logical float value
    available F64 form
    optional existing tagged-object form
    optional recipe for creating a tagged-object form
```

Other future examples include:

```text
logical tuple
    virtual ordered fields
    optional materialized tuple object

logical instance
    known shape
    virtual or scalar-replaced fields
    optional materialized instance object

logical integer
    tagged SMI form
    possible machine-integer form under a proven range
```

These are forms of one semantic value, not several unrelated Python values.
The representation plan records which forms exist, where they are available,
and which uses demand a particular form.

Core IR remains concrete. Lowering creates separate represented Core SSA values
such as `TaggedValue`, `F64`, or `Pointer`, connected by explicit operations.
Core block parameters likewise have one fixed representation. Virtuality does
not weaken Core IR's representation, dominance, use, liveness, or allocation
contracts.

## Existing Identity and New Identity

Representation equivalence is not object-identity equivalence. The plan must
distinguish an existing Python object from a newly produced Python value whose
identity is still virtual rather than physically allocated.

### Existing boxed values

Unboxing an existing float produces an F64 form of the same numeric value, but
the original box retains its identity:

```text
%boxed_argument: existing Python object
%raw = unbox %boxed_argument
```

If `is`, an opaque call, or another normal-path observing operation needs that
original object, the compiler must use `%boxed_argument`. Recovery has the same
identity rule: if the recoverable state already has a boxed object, the Snapshot
must capture and reuse that box. It cannot create a replacement from `%raw`.

### Newly computed values

A Python float-producing semantic operation creates a new virtual identity even
when its selected normal-path form is only an F64 value:

```text
%sum = add_f64 %lhs, %rhs
```

A normal-path materialization realizes that identity as a physical Python
object. Once that box exists, later normal-path and recovery uses must reuse it.
A recovery-only demand is different: while the identity is still virtual, a
taken side exit may temporarily materialize it for reconstructed interpreter
state without introducing a mainline allocation. All aliases observed in the
same outcome must receive the same realized object. Two semantically distinct
virtual identities must not be commoned merely because their payloads compare
equal.

The representation plan therefore needs an explicit materialization identity,
conceptually:

```text
MaterializationIdentity
    kind: BoxF64
    source: %sum
```

This is a semantic identity token, not a physical instruction ID. A static token
identifies the producing operation; repeated dynamic executions, such as loop
iterations, still produce distinct dynamic identities. Lowering may
realize it as a normal-path `BoxF64`, as one shared transition-program result,
or as a later virtual-object materialization. Every use of the same identity in
one dynamic execution receives the same object.

Separate side exits may independently evaluate the recipe because only one exit
is taken in a particular execution. Within one exit, repeated frame-state uses
of the identity must share one transition-local result.

Float-literal identity needs a separate explicit policy. If the language and
bytecode contract require a code-object constant to retain shared identity, the
Semantic plan must retain or materialize that shared object rather than treating
each use as an independent numeric recipe. Numeric equality alone is never
enough to choose the identity policy.

## Demand-Driven Materialization

A representation is selected from its uses. Uses fall into three broad groups.

### Primitive consumers

A primitive consumer can use the light form directly:

```text
AddF64
MulF64
NegF64
an inferred float comparison
```

No box is introduced merely because bytecode conceptually stores the result in
a Python local.

### Normal-path observing consumers

Some operations require a concrete tagged object on the normal path:

- identity observation;
- an opaque Python or native call;
- storage into a generic container or object field;
- an operation whose specialization consumes only a tagged form;
- return through a calling convention that requires a tagged value;
- frame inspection or another operation that exposes interpreter-visible
  objects immediately.

Lowering materializes at or before the first such use. Dominance and identity
requirements determine whether later uses share that materialization.

### Recovery-only consumers

A Snapshot or logical frame state is a conditional demand. If its value is
still virtual, it requires a recipe that can temporarily reconstruct a tagged
object when that exit is taken; it does not require allocation on the normal
path. If a box has already been materialized on the normal path, the Snapshot
must instead capture that existing box so recovery preserves its identity.

Semantic-to-Core lowering can express this using an explicit materialization
instruction referenced only by Snapshots. Existing Core sinking marks eligible
instructions, Core-to-Machine lowering moves their computation into side-exit
regions, and transition programs evaluate the recipe only on exit.

This division preserves a useful narrow Core mechanism without making Core
responsible for discovering the entire virtual representation plan.

## Blocks, Edges, and Loops

Semantic block parameters join logical Python values and remain
representation-free. Representation planning considers all incoming values and
all uses of the joined value before Core parameters are chosen.

For a loop-carried float:

```text
entry:    initial float value --------+
                                      +--> loop(%value)
backedge: multiplied float value -----+
```

the optimizing tier can choose an F64 loop parameter. The entry path unboxes or
produces its F64 source once, the loop body produces the next F64 value, and the
backedge carries that value directly. A logical local in recovery state refers
to a materialization recipe for the current iteration's F64 value.

The loop therefore does not allocate merely to satisfy a tagged block parameter
and then depend on later Core rewrites to move that allocation out again.

Mixed incoming forms require an explicit lowering decision:

- choose a common light representation and convert available incoming values;
- choose a tagged representation and materialize incoming virtual values;
- split or specialize control flow when correlated type partitions justify it;
- decline the optimization and lower conservatively.

Semantic-to-Core lowering builds a fresh graph, so it may create predecessor or
edge conversions as part of construction. It is not constrained by the Core
graph rewriter's intentionally narrower rule that an atomic block-parameter
rewrite cannot insert arbitrary source-block instructions.

Representation analysis must reach a bounded fixed point across loops. It must
not oscillate between tagged and unboxed choices. The initial policy should use
monotone demand propagation and explicit widening or a conservative tagged
fallback rather than an unbounded profitability search.

## Interaction With Type Inference and Inlining

Virtual representation planning runs only after enough Semantic inference has
established the operation semantics that justify a representation:

```text
build Semantic IR
    -> infer guaranteed and likely facts
    -> inline profitable known targets
    -> repeat in bounded waves if necessary
    -> freeze the semantic graph and facts
    -> plan virtual representations and materializations
    -> lower once into a fresh Core graph
```

Inlining is particularly important. A value passed through several inlined
functions may stay virtual when all contextual uses are primitive. Without
inlining, the same call boundary may demand a tagged object.

Correlated type partitions may select different representations in different
cases. A float case may remain F64 while a generic case remains tagged. Case
realization and representation realization must be planned together so the
compiler does not eagerly create a Cartesian product of alternatives.

If planning exposes new inference or inlining opportunities, the compiler may
run another complete bounded pass sequence. It should not mutate a partially
lowered Core graph while attempting to rediscover a stable Semantic plan.

## Lowering Contract

Semantic-to-Core lowering consumes an immutable Semantic graph, frozen facts,
and a representation plan. It creates a fresh Core graph in one direction.

For each logical value, lowering records the Core definitions already available
for each selected concrete form. When a use requests a form:

1. reuse a dominating compatible definition if one exists;
2. emit the planned conversion or materialization at its selected point;
3. bind the resulting Core definition to that form and materialization identity;
4. use the same definition for later dominated aliases that require sharing.

For recovery-only materializations, lowering emits a Core computation whose
uses are confined to the appropriate Snapshots. The ordinary sinking and
transition machinery then proves whether it can leave the normal path.

Core lowering still owns executable details:

- shape, tag, validity, overflow, and divisor guards;
- Snapshots and exact interpreter resume state;
- represented Core instructions and block parameters;
- concrete boxing and object-materialization instructions;
- generic fallbacks and side exits.

Semantic planning never treats inferred facts as executable checks. Likely
facts create guard obligations; only guaranteed facts remove checks.

## Relationship to Current Float Optimizations

The current Float work remains useful infrastructure:

- explicit `UnboxF64`, F64 arithmetic, and `BoxF64` Core operations;
- AArch64 F64 allocation and emission;
- exact immutable-shape propagation;
- constant folding for F64 values;
- local `UnboxF64(BoxF64(value))` simplification;
- transition-capable `BoxF64` and snapshot sinking;
- block-parameter and fixed-point traversal infrastructure used by general Core
  passes.

Type-specific cross-edge boxing rewrites are not the long-term generalized
representation architecture. They may remain as bounded tier-1 optimizations
when their complexity is justified, but they should not be extended into a
virtual tuple/object framework. The optimizing tier should produce the desired
cross-edge representation directly.

Near-term work may consequently prioritize making real allocation and boxing
reasonably cheap. Eliminating allocation globally across joins, loops, and
inlined calls is deferred to Semantic IR rather than forcing the direct tier to
recover virtual-object intent from already-lowered Core IR.

## Garbage Collection and Safepoints

Virtual values with no materialized managed pointer are not ordinary GC roots.
Their primitive operands must remain available so recovery can construct the
required Python objects.

A boundary that can safepoint and then return to compiled code needs an explicit
policy. Depending on the final GC and call conventions, lowering may:

- keep primitive virtual values live while publishing only existing managed
  roots;
- materialize selected values before the safepoint;
- record a recoverable recipe in published JIT metadata;
- leave compiled execution before entering the safepoint.

This document does not select that policy before the GC redesign. It requires
the choice to be explicit and forbids treating an unmaterialized object as if a
collector could already discover it.

Materialization also depends on the runtime allocation contract. The Semantic
design should not encode the current slab bitmap or a particular future nursery
layout. Core materialization operations and the backend use the allocation ABI
established by the runtime.

## Verification

The optimizing tier must verify at least:

- every semantic use has a selected concrete form that lowering can produce;
- every materialization recipe's operands are available at its realization
  point or in its transition program;
- materialization recipes are acyclic;
- every existing object identity is preserved when an observing use requires
  it;
- aliases of one materialization identity share one realized object where they
  can be observed together;
- distinct required allocation identities are not accidentally commoned;
- every logical frame state is exactly reconstructible;
- every Core block-parameter edge supplies the parameter's concrete
  representation;
- representation planning has converged within its fixed budget or fallen back
  conservatively;
- sinking is restricted to transition-capable, effect-safe computations and
  never recursively introduces a side exit.

## Initial Implementation Boundary

The first Semantic virtual-representation slice should be deliberately narrow:

1. support exact builtin floats only;
2. represent existing boxed floats separately from newly computed virtual
   floats;
3. select F64 through straight-line code and loop parameters;
4. materialize for tagged normal uses;
5. emit snapshot-only `BoxF64` recipes for existing transition sinking;
6. lower into ordinary valid Core IR and use the existing backend unchanged.

This slice should prove that Semantic IR can generate the desired Mandelbrot
loop directly. It should not begin with generalized escape analysis, arbitrary
object scalar replacement, or a universal materialization bytecode.

Once the Float model is sound, tuples are a plausible next virtual object
because their fixed immutable fields make identity and materialization easier
to state than mutable containers. Ordinary instances require shape stability,
field ownership, mutation, and escape rules and therefore come later.

## Open Questions

- the concrete representation-plan and materialization-identity data types;
- whether representation planning is a separate frozen analysis or part of
  Semantic-to-Core lowering;
- profitability rules for retaining multiple forms simultaneously;
- where normal-path materializations should be placed when several branches
  demand them;
- how literal and code-object constant identity is represented;
- how virtual values cross safepoint-capable compiled calls;
- which virtual-object recipes transition programs should eventually support;
- whether any current tier-1 cross-edge Float rewrite remains worthwhile after
  the optimizing tier exists;
- compilation budgets and the trigger for tier-2 recompilation.

## Related Documents

- [Semantic IR and Specialization](jit-semantic-ir-and-specialization.md)
- [JIT Compiler and IR](jit-compiler-and-ir.md)
- [JIT IR Graph Rewrites](jit-ir-graph-rewrites.md)
- [JIT Side-Exit Lowering](jit-side-exit-lowering.md)
- [JIT Transition Programs](jit-transition-program.md)
- [Value Representation](value-representation.md)
