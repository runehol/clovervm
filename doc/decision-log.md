# Decision Log

This document records consequential clovervm engineering decisions, their
context, the alternatives considered, and the evidence that would justify
revisiting them. Design documents describe the current coherent system; Git
records what changed; this log preserves why major choices were made.

The admission threshold is deliberately high:

> Log a decision when it shapes the overall design of a subsystem or establishes
> a contract spanning multiple subsystems.

Choices below that threshold should normally remain in code, tests, or the
relevant design document. They are feasible to revisit and refactor with a few
focused implementation prompts, so promoting them into project-level decisions
would make the design appear more fixed than it is. The decision log exists to
preserve expensive architectural reasoning, not to freeze ordinary engineering
choices.

The initial entries were reconstructed on 2026-07-18 from recent JIT design
work, its design document, and the corresponding commits. They record only
rationale that remains clear from those sources.

## Operating Rules

- Record choices that determine a subsystem's overall architecture or establish
  contracts and invariants across subsystem boundaries.
- Record Python-semantic, ownership, runtime, and major tooling decisions only
  when they meet that architectural or cross-subsystem threshold.
- Do not record routine local implementation choices.
- Distinguish permanent invariants, initial policies, experiments, and
  provisional choices.
- Keep entries concise enough to scan and link their detailed design sources.
- Do not rewrite historical reasoning merely because the current design has
  changed.
- Correct factual errors explicitly. Supersede a changed decision with a new
  entry and link both entries.
- State what evidence or conditions would justify revisiting each decision.
- Keep unresolved alternatives in design-document open questions until an
  actual decision is made.

## Status Values

- **Accepted:** the active project decision;
- **Experimental:** deliberately being tested before broader commitment;
- **Superseded:** replaced by a later decision;
- **Rejected:** considered and deliberately not selected.

## Index

| ID | Decision | Status |
|---|---|---|
| D-0001 | Compile whole functions rather than hot traces | Accepted |
| D-0002 | Use Guard IR as the mandatory compiler waist | Accepted |
| D-0003 | Use block parameters for SSA joins | Accepted |
| D-0004 | Start with canonical publication while preserving a path to precise maps | Accepted |
| D-0005 | Use tagged `Value` as the initial JIT representation | Accepted |
| D-0006 | Use ordered list-based SSA rather than a sea of nodes | Accepted |

## D-0001: Compile Whole Functions Rather Than Hot Traces

**Date:** 2026-07-18
**Status:** Accepted
**Scope:** JIT compilation unit and control-flow model
**Commitment:** Architectural direction

### Decision

The clovervm JIT compiles function CFGs rather than recording and compiling
linear hot execution traces. Side exits represent failed speculative
assumptions, not ordinary termination of a trace at unrecorded control flow.

### Context

Clovervm relies heavily on dominance for shape-check elimination and needs to
reason about ordinary branches, loops, polymorphic IC expansion, inlining,
effect ordering, and exact bytecode recovery. Python operators may call
arbitrary user code, making ignored alternatives and complete control-flow
structure important.

LuaJIT demonstrates valuable compact-IR and backward-allocation techniques, but
some of their simplicity follows specifically from making a mostly linear trace
the compilation unit. That tradeoff does not match clovervm's intended control
and recovery model.

### Alternatives Considered

- a trace-recording JIT with exits at divergence;
- a trace tree or side-trace system;
- a function JIT initially, followed by an independent trace tier.

### Why Chosen

A function CFG makes dominance, joins, loop structure, block-local facts, and
control-flow-producing lowering first-class. It keeps bytecode recovery tied to
one complete function representation and avoids building a separate trace
recording, stitching, and side-trace infrastructure.

### Consequences

- SSA and CFG construction are core compiler machinery.
- The compiler sees cold and untaken function paths unless it deliberately
  omits or outlines them.
- Register allocation and scheduling must handle general joins and loops.
- Useful trace-compiler techniques may be borrowed only where they do not rely
  on linear-trace semantics.

### Revisit When

- full-function compilation latency or code size prevents useful tier-up;
- representative workloads spend most time in paths poorly represented by
  function-level specialization;
- a trace tier provides a clearly independent benefit large enough to justify
  its recording and recovery machinery.

### References

- `doc/jit-compiler-and-ir.md`
- Commit `ad0988a`

## D-0002: Use Guard IR as the Mandatory Compiler Waist

**Date:** 2026-07-18
**Status:** Accepted
**Scope:** JIT compiler pipeline
**Commitment:** Initial architecture with an extension point

### Decision

Guard IR is the only mandatory compiler IR. The initial compiler lowers decoded
bytecode and IC snapshots directly into Guard IR. Semantic IR is an optional
optimization frontend for type inference, caller-context-sensitive inlining,
polymorphic reasoning, and other higher-effort work.

### Context

The original design required Semantic IR, Guard IR, and Machine IR for every
compilation. Review of fast optimizing JITs showed that monomorphic IC feedback
already supplies enough predicates and successful actions to generate useful
Guard IR without first implementing a general type system or inliner.

### Alternatives Considered

- require Semantic IR before every Guard IR compilation;
- maintain separate low-effort and optimizing compiler pipelines;
- lower bytecode directly into a target-specific representation.

### Why Chosen

Guard IR is the narrow correctness-critical waist. It can express checks,
proofs, effects, calls, control flow, SSA, bytecode recovery states, and
conservative generic actions. Both direct and inference-driven compilation can
converge on one optimizer, verifier, recovery model, and backend interface.

### Consequences

- the first JIT does not require function inlining or semantic type inference;
- Semantic IR must produce ordinary valid Guard IR and remain invisible to
  later optimization and backend stages;
- Guard IR must support unknown types and conservative Python calls;
- compilation effort can increase without creating a second executable tier or
  backend contract.

### Revisit When

- direct Guard construction materially constrains optimization;
- inference-driven compilation requires incompatible Guard operations or CFG
  policies;
- conditionals for the two construction paths begin spreading through the
  shared Guard optimizer.

### References

- `doc/jit-compiler-and-ir.md`
- Commit `2df088e`

## D-0003: Use Block Parameters for SSA Joins

**Date:** 2026-07-18
**Status:** Accepted
**Scope:** SSA joins and backend edge transfers
**Commitment:** Guard and optional Semantic IR representation

### Decision

SSA joins use ordered block parameters with explicit argument vectors on
incoming edges rather than phi instructions. Block-edge transfer has atomic
parallel-copy semantics.

### Context

Phi nodes and block parameters are semantically equivalent, but explicit edge
arguments expose the exact location where a direct backend must reconcile
register assignments. Treating the join transfer as one parallel operation is
necessary for swaps and cycles where sequential assignments would trample
still-needed sources.

### Alternatives Considered

- conventional phi instructions;
- lowering phi nodes into implicit edge moves only during code generation.

### Why Chosen

Block parameters make CFG interfaces and edge moves explicit, fit mutable CFG
rewrites, and simplify direct machine-code generation.

### Consequences

- every normal edge supplies one argument per destination parameter;
- CFG verification checks arity, ownership, kinds, and dominance;
- register allocation produces parallel-move bundles associated with edges;
- critical edges may require splitting or small emission-only edge blocks.

### Revisit When

- block-signature maintenance proves materially more complex than phi editing;
- an alternative representation preserves explicit edge moves while
  simplifying SSA construction or CFG mutation.

### References

- `doc/jit-compiler-and-ir.md`
- Commit `2df088e`

## D-0004: Start With Canonical Publication While Preserving Precise Maps

**Date:** 2026-07-18
**Status:** Accepted
**Scope:** JIT safepoints, garbage collection, and deoptimization
**Commitment:** Permanent runtime contract with an initial policy

### Decision

The permanent contract requires precise root discovery at every safepoint and
exact canonical interpreter reconstruction before interpreter resumption. The
initial implementation satisfies it through canonical frame publication and
generated recovery code. It does not initially require the collector or stack
walker to understand optimized register state.

The compiler nevertheless constructs declarative post-allocation
`SafepointState` and `DeoptState`, preserving a staged path to precise compiled
stack maps and generic metadata-driven deoptimization.

### Context

Canonical publication keeps the initial JIT compatible with the current
deferred-reference-counting runtime and avoids putting a collector rewrite in
the middle of JIT implementation. Precise maps can remove hot-path publication
stores, but require compiled-frame walking, PC-to-code lookup, callee-saved
register recovery, GC-critical metadata, and eventually writable root locations
for a moving collector.

Clovervm also plans a separate migration to a generational copying collector.
Building the precise-map interface against the current collector risks doing
that integration twice or choosing contracts that do not support pointer
relocation.

### Alternatives Considered

- require precise safepoint and deoptimization maps in the first JIT;
- make canonical publication a permanent runtime invariant;
- keep only generated recovery state with no declarative map-shaped input;
- update the collector and stack walker before beginning JIT implementation.

### Why Chosen

The initial policy minimizes simultaneous subsystem changes while the permanent
contract avoids an architectural dead end. Declarative location state lets
publication code, recovery blocks, shadow maps, and future generic translation
share allocation and logical-frame machinery.

### Consequences

- safepoint-capable calls initially publish dirty homes in all active logical
  frames;
- initial side exits use generated, deduplicated recovery code;
- compiled frames and code objects must remain identifiable and walkable enough
  not to obstruct later maps;
- future migration can validate shadow maps while publication remains
  authoritative;
- a moving collection must not allow compiled execution to reuse stale cached
  pointers; an initial integration may deopt when a collection epoch changes.

### Revisit When

- measurements show publication materially affecting representative workloads;
- the generational copying collector has stable root-update and stack-walking
  contracts;
- generated recovery metadata or code size becomes burdensome;
- shadow-map validation provides enough confidence for opt-in precise scanning.

### References

- `doc/jit-compiler-and-ir.md`
- `doc/generational-copying-gc.md`
- Commit `518630e`

## D-0005: Use Tagged Value as the Initial JIT Representation

**Date:** 2026-07-18
**Status:** Accepted
**Scope:** JIT semantic and machine value representation
**Commitment:** Initial implementation policy

### Decision

The initial JIT keeps ordinary Python values in the VM's existing tagged
`Value` representation. SMIs remain shifted, pointers remain directly usable,
and canonical homes contain the same representation expected by the
interpreter. Unboxed floats and other alternate representations are optional
later optimizations.

### Context

The existing representation already supports direct pointer use and native
overflow detection for most SMI addition and subtraction. Keeping it through
the first JIT makes interpreter exits, frame publication, calls, and recovery
substantially simpler. Boxed Python floats remain a known performance cost.

### Alternatives Considered

- unbox all values according to inferred types;
- require unboxed floats in the first backend;
- introduce a separate JIT-only tagged representation.

### Why Chosen

The tagged representation generates useful integer and object code without
requiring the initial type system, reification machinery, or representation
selection. It also minimizes conversion at JIT/interpreter boundaries.

### Consequences

- SMI arithmetic generally operates directly on encoded values;
- float-heavy code initially retains boxing overhead;
- backend locations denote semantic `Value`s unless an explicitly optional
  representation optimization says otherwise;
- the design preserves semantic identity independently of future simultaneous
  boxed and unboxed machine representations.

### Revisit When

- profiles show boxed float traffic materially limiting important workloads;
- semantic inference and recovery recipes can support alternate
  representations safely;
- representation selection has a measured benefit large enough to justify
  reification and additional backend complexity.

### References

- `doc/jit-compiler-and-ir.md`
- `src/object_model/value.h`
- Commit `ad0988a`

## D-0006: Use Ordered List-Based SSA Rather Than a Sea of Nodes

**Date:** 2026-07-18
**Status:** Accepted
**Scope:** Canonical JIT IR representation and optimization model
**Commitment:** Architectural direction

### Decision

Semantic IR, when present, and Guard IR use conventional SSA CFGs with an
ordered instruction list in each basic block. The list records the current
schedule. SSA operands, proof dependencies, explicit effects, and control edges
state the constraints under which a pass may deliberately change that schedule.

A sea-of-nodes graph is not the canonical whole-function representation.
Temporary DAGs, e-graphs, or backend-local graph forms remain available for a
narrow optimization, instruction-selection, or scheduling problem when they
demonstrably pay for themselves.

### Context

Clovervm operations frequently have ordering constraints. Python arithmetic can
call overloaded methods; arbitrary Python calls can mutate non-local state;
shape and validity facts have effect-bounded lifetimes; operations have commit
boundaries; and speculative checks may exit to precise bytecode states. Moving
an operation can therefore change Python-visible order, proof validity,
liveness, and the optimized locations that a recovery description must capture.

A sea of nodes can represent control and effect dependencies, but its greater
reorderability provides less benefit when much of the graph must remain tied to
an effect order. It also makes the compiler reconstruct or maintain a schedule
before emission and can encourage gratuitous movement whose deoptimization and
safepoint consequences must then be represented precisely.

The initial targets are wide out-of-order AArch64 and potentially x86-64
processors. They dynamically recover substantial instruction-level parallelism
without requiring the compiler to globally expose every legal reordering.

### Alternatives Considered

- a canonical whole-function sea-of-nodes IR with explicit control and effect
  chains;
- an unordered SSA graph followed by mandatory global scheduling;
- ordered CFG IR for semantic operations followed by a mandatory graph-based
  machine optimizer;
- ordered lists with optional temporary graph representations for narrow tasks.

### Why Chosen

An ordered list makes conservative semantic and effect order visible by default
and makes each intentional movement reviewable. It fits bytecode origins,
side-exit frame states, commit boundaries, mutable CFG lowering, and precise
post-allocation recovery. SSA and explicit dependency metadata still permit
code motion where it has clear value, while the target processors handle much
of the remaining dynamic scheduling.

### Consequences

- every block always has a concrete current schedule;
- effectful operations and side exits retain an obvious position relative to
  bytecode recovery states;
- optimization passes move operations explicitly and must revalidate effects,
  dominance, liveness, and recovery information;
- the compiler may miss profitable global reorderings that a more aggressive
  graph optimizer could discover;
- backend-local scheduling or graph construction remains possible without
  changing the common Guard representation;
- measurements, rather than representational fashion, must justify adding a
  less ordered optimization form.

### Revisit When

- profiles show that compiler-visible scheduling or global code motion, rather
  than checks, calls, memory behavior, or register allocation, limits important
  workloads;
- a future in-order, VLIW, accelerator, or unusually constrained target cannot
  obtain acceptable schedules from the ordered representation;
- a narrow graph-based pass repeatedly demonstrates enough benefit to justify
  becoming a broader representation boundary;
- maintaining explicit order becomes more expensive than maintaining precise
  control, effect, and recovery dependencies in an unordered graph.

### References

- `doc/jit-compiler-and-ir.md`
- Commit `ad0988a`
