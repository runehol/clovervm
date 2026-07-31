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
| D-0002 | Use Core IR as the mandatory compiler waist | Accepted |
| D-0003 | Use block parameters for SSA joins | Accepted |
| D-0004 | Start with canonical publication while preserving a path to precise maps | Accepted |
| D-0005 | Use tagged `Value` as the initial JIT representation | Accepted |
| D-0006 | Use ordered list-based SSA rather than a sea of nodes | Accepted |
| D-0007 | Separate stable embedded metadata from movable compiled constants | Superseded |
| D-0008 | Preserve separate managed and host stacks during JIT bring-up | Superseded |
| D-0009 | Pool managed pointer constants and legalize non-pointer constants late | Accepted |
| D-0010 | Use indexed compact JIT instruction storage | Accepted |
| D-0011 | Use compact transition programs across execution boundaries | Accepted |
| D-0012 | Retain the native stack and use interpreter-aligned JIT context registers | Accepted |

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

## D-0002: Use Core IR as the Mandatory Compiler Waist

**Date:** 2026-07-18
**Status:** Accepted
**Scope:** JIT compiler pipeline
**Commitment:** Initial architecture with an extension point

### Decision

Core IR is the only mandatory compiler IR. The initial compiler lowers decoded
bytecode and IC snapshots directly into Core IR. Semantic IR is an optional
optimization frontend for type inference, caller-context-sensitive inlining,
polymorphic reasoning, and other higher-effort work.

### Context

The original design required Semantic IR, Core IR, and Machine IR for every
compilation. Review of fast optimizing JITs showed that monomorphic IC feedback
already supplies enough predicates and successful actions to generate useful
Core IR without first implementing a general type system or inliner.

### Alternatives Considered

- require Semantic IR before every Core IR compilation;
- maintain separate low-effort and optimizing compiler pipelines;
- lower bytecode directly into a target-specific representation.

### Why Chosen

Core IR is the narrow correctness-critical waist. It can express checks,
proofs, effects, calls, control flow, SSA, bytecode recovery states, and
conservative generic actions. Both direct and inference-driven compilation can
converge on one optimizer, verifier, recovery model, and backend interface.

### Consequences

- the first JIT does not require function inlining or semantic type inference;
- Semantic IR must produce ordinary valid Core IR and remain invisible to
  later optimization and backend stages;
- Core IR must support unknown types and conservative Python calls;
- compilation effort can increase without creating a second executable tier or
  backend contract.

### Revisit When

- direct Core construction materially constrains optimization;
- inference-driven compilation requires incompatible Core operations or CFG
  policies;
- conditionals for the two construction paths begin spreading through the
  shared Core optimizer.

### References

- `doc/jit-compiler-and-ir.md`
- Commit `2df088e`

## D-0003: Use Block Parameters for SSA Joins

**Date:** 2026-07-18
**Status:** Accepted
**Scope:** SSA joins and backend edge transfers
**Commitment:** Core and optional Semantic IR representation

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
an interpreted transition program. It does not initially require the collector
or stack walker to understand optimized register state.

The compiler does not construct separate `SafepointState` or `DeoptState`
products. A Core Snapshot is the authoritative logical exit-state description.
The CFG's `BytecodeStateOrder` maps Snapshot positions to canonical homes.
After allocation, transition planning combines those inputs with
`LocationAssignments`. Future precise root maps may project the required subset
from the same inputs when measurements justify that work.

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
- introduce separate declarative `SafepointState` and `DeoptState` products;
- update the collector and stack walker before beginning JIT implementation.

### Why Chosen

The initial policy minimizes simultaneous subsystem changes while the permanent
contract avoids an architectural dead end. Snapshots preserve logical frame
state independently of physical allocation, `BytecodeStateOrder` maps state
positions to canonical homes, and `LocationAssignments` provide the physical
information needed by publication, transitions, and any future shadow-map
projection.

### Consequences

- safepoint-capable calls initially publish dirty homes in all active logical
  frames;
- initial side exits expand Snapshots into transition programs; identical
  physical programs may be interned later;
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

Semantic IR, when present, and Core IR use conventional SSA CFGs with an
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
post-allocation transitions. SSA and explicit dependency metadata still permit
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
  changing the common Core representation;
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

## D-0007: Separate Stable Embedded Metadata From Movable Compiled Constants

**Date:** 2026-07-19
**Status:** Superseded by D-0009
**Scope:** JIT code generation, object-model metadata, garbage collection, and
compiled-code lifetime
**Commitment:** Cross-subsystem runtime contract

### Decision

Shapes and validity cells are allocated from dedicated non-moving stable pools.
Machine code may embed their addresses directly, and every embedded pointer is
also recorded in the owning compiled code object's GC-visible stable-metadata
array.

Every Python `Value` constant resides in a separate array of stable-addressed,
GC-rewritten slots. Machine code must access those slots through PC-relative
loads and must never embed any `Value` as an immediate. This includes SMIs,
booleans, and other self-contained immediate `Value`s as well as references to
movable managed objects. Keeping every `Value` in the precisely identified pool
gives the moving collector one uniform tracing and rewriting contract.

### Context

The JIT frequently compares shape and validity-cell identity. Relocating these
small, shared metadata objects would require target-specific rewriting of
embedded pointers or an extra indirection on hot checks. Ordinary Python
constants, however, should retain the moving collector's placement and
compaction benefits. Rewriting instruction bytes for their movement would
interact with target encodings, W^X transitions, and instruction-cache
coherency.

### Alternatives Considered

- allocate shapes and validity cells in moving generations and relocate every
  embedded machine-code reference;
- access all metadata and constants through indirect tables;
- make every object referenced by compiled code stable;
- embed movable constants directly and maintain target-specific instruction
  relocation records.

### Why Chosen

Stable pools make the highest-frequency identity checks direct while retaining
explicit GC lifetime through compiled-code metadata. Stable constant slots let
the collector rewrite ordinary managed references without decoding or patching
machine instructions. The split confines non-moving allocation to metadata
whose stable identity materially simplifies both the JIT and collector.

### Consequences

- stable pool entries are never moved but are reclaimable after all runtime,
  IC, compilation-session, and compiled-code references disappear;
- compiled code owns a precise stable-metadata array and a distinct traced,
  rewritten managed-constant array;
- every `Value` constant, including self-contained immediates, occupies a
  naturally aligned slot in that array and is loaded PC-relatively;
- the constant array's slots and their PC-relative relationship to machine code
  remain stable while the referenced objects may move;
- backend verification rejects unlisted embedded metadata pointers and any
  `Value` embedded as an instruction immediate;
- compiled-code retirement and GC tracing jointly determine when metadata and
  constant references cease to be live.

### Revisit When

- a target cannot address the compiled constant array efficiently with the
  required PC-relative scheme;
- stable shape or validity-cell retention becomes a measured memory problem;
- code relocation or compaction introduces a broader relocation mechanism that
  safely and profitably subsumes this split.

### References

- `doc/jit-compiler-and-ir.md`
- `doc/jit-machine-code-emission.md`
- `doc/generational-copying-gc.md`
- `doc/generational-copying-gc-implementation-plan.md`

## D-0008: Preserve Separate Managed and Host Stacks During JIT Bring-up

**Date:** 2026-07-19
**Status:** Superseded by D-0012
**Scope:** JIT entry, native calls, interpreter transitions, and reclamation
**Commitment:** Initial cross-subsystem execution policy

### Decision

The first JIT keeps all Python frames in the existing Clover stack. Generated
Python code uses that storage as its architectural managed stack, while the
hand-written interpreter, runtime, extensions, and every C or C++ target execute
on the host stack. Reentrant transition thunks publish the managed frontier and
preserve both stack positions while crossing between them.

The eventual generated interpreter and runtime may instead use one exact-scanned
mixed platform stack. That is a later migration, not a JIT bring-up prerequisite.

### Context

Putting generated frames directly on the host stack would require either a
mixed-stack walker or disabling reclamation. It would also collide with the
current interpreter's native activations when that interpreter allocates Python
frames by managed-frame pointer arithmetic. The existing separate Clover stack
already provides canonical storage understood by the reclaimer and interpreter.

Native code may re-enter Python, so selectively leaving apparently small native
calls on the managed stack would make bring-up transitions non-uniform. Switching
all native calls to the host stack gives nested re-entry one consistent rule.

### Alternatives Considered

- place initial generated frames directly on a mixed host stack;
- disable reclamation while bringing up the JIT;
- copy or relocate canonical frames at every interpreted/compiled transition;
- require the generated interpreter or mixed-stack walker before the first JIT.

### Why Chosen

The dual-stack policy composes with the implemented managed frame layout,
native-boundary contract, and reclaimer. It isolates new assembly work in
transition thunks while preserving one canonical Python frame chain across
interpreted, compiled, native, and reentrant execution.

### Consequences

- the reclaimer remains enabled during JIT bring-up;
- every initial JIT-to-native call switches to the host stack;
- transition records nest and restore the immediately enclosing SP, FP,
  frontier, and continuation;
- generated call and return instructions may use the Clover stack, but native
  ABI frames never reside there;
- later mixed-stack execution requires an explicit migration and exact walker.

### Revisit When

- generated interpreter handlers replace the hand-written interpreter;
- the mixed managed/native stack walker is implemented and validated;
- measurements show stack-transition overhead justifies certified native leaf
  calls on managed stack storage.

### References

- `doc/jit-compiler-and-ir.md`
- `doc/function-calling-convention.md`
- `doc/native-managed-boundaries.md`

## D-0009: Pool Managed Pointer Constants and Legalize Non-Pointer Constants Late

**Date:** 2026-07-23
**Status:** Accepted
**Scope:** Core constants, target legalization, garbage collection, and
machine-code emission
**Commitment:** Cross-layer compiler and runtime contract

### Decision

Core represents every Python constant with one ordinary `Const` instruction
whose `ValueConstant` attribute contains the semantic `Value`. Core does not
choose immediate encodings, create constant-pool entries, or expose pool
indices.

Backend preparation decides how each surviving constant is materialized.
Managed pointer values must use GC-traced constant-pool slots. Non-pointer
tagged values may be synthesized as immediates or placed in the pool according
to target encodability and profitability. Final pool creation and deduplication
remain emitter work.

Stable object-model metadata such as shapes and validity cells retains the
separate direct-embedding and GC-visible metadata contract from D-0007.

### Context

Forcing SMIs, booleans, `None`, and other non-pointer bit patterns into the pool
adds loads and pool pressure even when a target can materialize them cheaply.
Choosing pool entries during Core optimization is also premature: rewrites may
remove, duplicate, or move constants, leaving pool identities to be repaired.
Managed pointers are different because a moving collector must update a traced
slot rather than target-specific instruction bytes.

### Alternatives Considered

- require every `Value` constant to use the traced pool, as D-0007 specified;
- split Core into immediate and pool-load constant instructions;
- assign pool slots during Core construction and preserve their identities
  through optimization;
- embed managed pointer values directly and relocate machine instructions.

### Why Chosen

One Core `Const` keeps constant folding and SSA rewriting independent of target
encoding. Late legalization preserves immediate opportunities, and emitter-time
pool construction sees only surviving constants. Pointer-only pooling retains
the required moving-GC boundary without imposing it on self-contained values.

### Consequences

- Snapshots and ordinary operands refer uniformly to the `ProgramValueRef`
  produced by `Const`;
- Core constant folding never manages pool slots or indices;
- target legalization may rematerialize a non-pointer constant or choose a pool
  load;
- every managed pointer constant remains session-retained during compilation
  and becomes reachable through a traced code-object pool slot before session
  teardown;
- the emitter deduplicates final pool values without exposing that identity to
  Core.

### Revisit When

- measured code size or instruction count favors a different target-specific
  materialization policy;
- a moving collector gains a safe and profitable way to patch embedded managed
  pointers;
- Machine IR needs explicit immediate pseudos for folding into single-use
  consumers.

### References

- `doc/jit-compiler-and-ir.md`
- `doc/jit-instruction-representation.md`
- `doc/jit-machine-code-emission.md`
- `src/jit/instruction.def`

## D-0010: Use Indexed Compact JIT Instruction Storage

**Date:** 2026-07-26
**Status:** Accepted
**Scope:** JIT instruction identity, physical layout, CFG references, and
instruction-indexed analyses
**Commitment:** Compiler-wide representation contract

### Decision

`CompilationStorage` owns an append-only vector of 16-byte
`InstructionEntry`s. `InstructionId` is the sole persistent instruction
identity and is a typed 32-bit vector index. Instruction operands, block
instruction sequences, block parameters, use lists, and instruction-indexed
analyses store IDs rather than instruction pointers.

`Instruction` and its concrete subclasses are lightweight views containing a
storage pointer and an ID. Blocks and CFG edges remain stable-address objects;
branch attributes use compact `BlockEdgeId`s resolved through compilation
storage.

Each entry contains three 32-bit inline slots followed by a 16-bit kind and a
16-bit operand-storage word. Attributes are always inline. Direct operands
precede attributes. When operands are indirect, attributes begin at slot zero,
slot two holds an offset into the compilation-owned `InstructionOperandTable`,
and all operands occupy one contiguous range in that table. Wide attributes
consume two slots and their schema-authored positions are checked for alignment.

### Context

The original representation used stable instruction pointers and 48-byte
records with five pointer-sized slots. Stable pointers simplified local access,
but forced a slab or segmented allocation model, made every persistent
instruction reference pointer-sized, and spent substantial inline space on
ordinary small instructions.

Side-exit transitions and other compiler metadata also benefit from compact
instruction references. Once references became IDs, retaining stable entry
addresses no longer justified the larger representation or indirect allocation
model.

### Alternatives Considered

- retain slab-backed stable instruction objects and pointer references;
- retain a deque so both IDs and stable entry pointers remain available;
- use variable-size instruction records in a relocation-aware operation buffer;
- put every payload, including attributes, in side storage;
- retain pointer-sized operands and shrink only the number of inline slots.

### Why Chosen

Dense vector entries provide constant-time indexed lookup and cheap sequential
traversal. Four-byte references reduce operands and analysis keys. Value views
preserve the natural typed instruction API while making vector reallocation
irrelevant to identity. Keeping attributes inline makes typed access
kind-constant, while the separate operand table handles variadic, oversized,
and alignment-constrained operand layouts without expanding every entry.

### Consequences

- code that dereferences an instruction ID must have its
  `CompilationStorage`;
- CFGs borrow their owning storage, and ordinary block iteration returns
  resolving instruction views while explicit ID access remains available;
- instruction and operand-table vector growth cannot invalidate IDs or views;
- detachment poisons the indexed entry in place, and IDs are never reused;
- construction checks the 32-bit instruction and operand-table domains;
- generated schema checks determine direct versus indirect operands and reject
  layouts whose inline attributes do not fit or align;
- representative storage use and lookup cost remain to be measured.

### Revisit When

- profiles show storage-assisted instruction lookup materially limiting compiler
  throughput;
- operand-table traffic or wasted inline capacity dominates representative IR
  memory;
- real compilation units approach the 32-bit identity or operand-table limits;
- a variable-size operation buffer demonstrates a measured advantage large
  enough to justify its relocation and access machinery.

### References

- `doc/jit-instruction-representation.md`
- Commits `646667e` through `80abb95`

## D-0011: Use Compact Transition Programs Across Execution Boundaries

**Date:** 2026-07-26
**Status:** Accepted
**Scope:** JIT side exits, frame synchronization, sunk computation, and future
calling-convention adapters
**Commitment:** Transition-program representation

### Decision

A `TransitionProgram` is restricted Core IR plus transition-specific header,
transfer, and terminal handoff instructions. It is one continuous straight-line
instruction stream that transforms values and machine state between execution
conventions. The first consumer reconstructs canonical interpreter state for a
JIT side exit. Future consumers may adapt between compiled function
conventions.

The program uses the 16-byte `InstructionEntry` layout and `InstructionKind`
values, but its physical references are 32-bit `TransitionLocation`s rather
than Core `InstructionId`s. Each location contains an area tag and an offset.
The initial areas are register-file state, stack data, and instruction-indexed
scratch storage. An eligible Core instruction's result is implicitly
`Scratch[instruction_index]`, where the body-instruction index excludes the
`BeginTransition` header. The resultless header carries the actual scratch-slot
requirement. `Transfer` has one source operand and one destination attribute,
both encoded as `TransitionLocation`; it updates that location and has
`ResultClass::None`. The resultless terminal `ResumeInterpreter` reads the
reconstructed accumulator from the fixed `Scratch[0]` handoff slot, carries a
borrowed stable `CodeObject *` and inline `BytecodePCOffset`, ends transition
execution, and identifies the interpreter continuation as an offset relative to
the code object's bytecode base. The source `CodeObject`
owns the published `JitCodeObject`, so the terminal back-reference does not add
a retain under the current refcounted collector. Pointer-shaped tagged
constants are loaded through the compiled code object's constant pool.

`instruction.def` explicitly declares transition-program eligibility. Generated
checks reject eligible kinds with snapshots, block edges, shape or validity-cell
attributes, variadic or indirect operands, absent results, side exits, branches,
Python calls, or unsupported fallibility. Resultless transition-only
instructions such as `BeginTransition`, `Transfer`, and `ResumeInterpreter` are
explicit exceptions to the Core result requirement. The transition executor
dispatches directly over raw entries through generated named layout accessors
rather than constructing compiler-facing typed instruction views. Execution
semantics remain explicit executor code rather than a generated interpreter.

Published programs are self-delimiting sequences in immutable code-object
metadata. A caller refers by offset to `BeginTransition`; the final handoff ends
execution. They do not own `std::vector`s. The only process-local pointer is the
terminal's borrowed source `CodeObject *`. Each thread owns a reusable
`std::vector<uint64_t>` scratch buffer and grows it to
`BeginTransition.scratch_slot_count` before dispatch.

### Context

Side-exit reconstruction contains computation and physical publication, but
they are phases of one dependency-ordered program rather than separate
products. `Transfer` directly copies between the three location areas and uses
scratch only when parallel-transfer ordering requires it. A program containing
only direct resultless transfers may request zero scratch slots; a cycle
requests the staging slots introduced by its lowering. The same mechanics also
apply to adapters that move and materialize values between two function
conventions, so recovery-specific terminology and types would make the
representation unnecessarily narrow.

The compact indexed Core instruction representation already supplies an
appropriate physical record and generated schema. Reusing that layout avoids a
second hand-maintained opcode structure while keeping the runtime interpreter
free of storage pointers and heavyweight typed views.

Transition execution is a no-safepoint region. Transition instructions cannot
invoke Python, trigger GC, or otherwise enter safepoint-capable VM code. Raw
scratch words therefore require no transition-local root map. Canonical state
or other required destination state is fully published before the terminal
handoff leaves this region.

### Consequences

- side-exit planning consumes Snapshots, `BytecodeStateOrder`, sinking metadata,
  and `LocationAssignments`, but the resulting program retains none of those
  compiler objects;
- semantic computation and canonical publication share one scratch namespace
  and have no stored phase boundary;
- `BeginTransition` carries the actual scratch capacity, including zero for
  programs that do not use scratch;
- the side-exit register-file image is read-only, while stack and scratch may
  be destinations;
- scratch sources can name only slots initialized by earlier instructions;
- a side-exit program ends with `ResumeInterpreter`, which reads the
  reconstructed accumulator and carries its bytecode continuation PC;
- transition programs are compact, relocatable data with no raw compiler or
  runtime pointers;
- operations that can themselves side exit cannot be transition-program
  eligible.

### Revisit When

- a required transition operation cannot fit the restricted instruction schema;
- measurements justify compiling transition programs instead of interpreting
  them;
- multiple consumers require an additional operand area or a different offset
  partition;
- object materialization requires a pointer-free metadata vocabulary beyond
  the constant pool.

### References

- `doc/jit-transition-program.md`
- `doc/jit-compiler-and-ir.md`
- `doc/jit-register-allocation.md`

## D-0012: Retain The Native Stack And Use Interpreter-Aligned JIT Context Registers

**Date:** 2026-07-31
**Status:** Accepted
**Scope:** AArch64 JIT entry, return, native calls, and managed frame access
**Commitment:** Initial runtime calling convention

### Decision

Generated AArch64 code retains native `sp` and `x29` throughout execution.
Fixed callee-saved `x25` contains the active `ThreadState *`, and fixed
callee-saved `x21` contains the current Clover managed frame pointer. Fixed
`x22` and `x24` contain the interpreter PC and owning `CodeObject *`. Managed
frame state, spills, and outgoing managed call windows remain in Clover storage
and are addressed through `x21`; managed values are not placed on the native
stack.

The `preserve_none` interpreter already carries the same state in
`x21`/`x22`/`x24`/`x25`. Its call handler repurposes `x23` from dispatch to the
compiled entry address and tail-calls an arity-selected thunk. The thunk loads
register parameters and calls the generated entry with `blr`. An ordinary
generated return restores the caller's `x21`/`x22`/`x24` state and uses `ret`,
balancing that call. Native helpers preserve the fixed context through the
platform ABI. Root
publication remains explicit because the native stack is not scanned for
managed values.

The standalone native execution harness also uses the six-argument
`preserve_none` entry state. Its C++ caller therefore preserves any required
host state, and the assembly thunk saves only x30 across the generated call.

### Context

This supersedes D-0008's proposed architectural stack switch. Keeping native
`sp` active avoids two stack switches around every interpreter or native call,
retains normal platform call and return behavior, and still preserves the
separate Clover storage already understood by the interpreter and reclaimer.
Generated Python entry is consequently not a public C ABI entry; native callers
must use the adapter.

### Consequences

- `x21`, `x22`, `x24`, and `x25` are unavailable to register allocation;
- all managed `StackLocation`s use the `x21` frame coordinate on AArch64;
- native `sp` and `x29` retain their platform ABI meanings;
- JIT-to-JIT and JIT-to-native calls can use ordinary AArch64 call mechanics;
- interpreter-to-JIT entry and host return require a target-specific adapter;
- safepoints still publish every live managed root explicitly.

### Revisit When

- a generated interpreter changes the cost or ownership of stack transitions;
- mixed native and managed stack walking is implemented and measured;
- another target cannot afford the fixed context-register set.

### References

- `doc/aarch64-jit-calling-convention.md`
- `doc/function-calling-convention.md`
- `doc/native-managed-boundaries.md`
