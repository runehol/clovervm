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
| D-0007 | Separate stable embedded metadata from movable compiled constants | Accepted |
| D-0008 | Preserve separate managed and host stacks during JIT bring-up | Accepted |
| D-0009 | Emit machine code through conservatively shortened code fragments | Accepted |
| D-0010 | Allocate code and GC-visible pools through a reachability-aware code cache | Accepted |

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
**Status:** Accepted
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
**Status:** Accepted
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

## D-0009: Emit Machine Code Through Conservatively Shortened Code Fragments

**Date:** 2026-07-20
**Status:** Accepted
**Scope:** JIT target encoding and machine-code layout
**Commitment:** Backend architecture

### Decision

Target backends emit directly encodable instructions in program order into
`CodeFragment`s. A fixed-size instruction whose fields depend on its final PC
is emitted as template bytes plus a machine-specific relocation. An operation
whose size may change instead terminates a fragment for layout purposes and
remains symbolic until final layout. `DeferredTransfer` is the linking or
non-linking control-transfer kind of trailing deferred operation. Fragments are
machine-code layout units rather than compiler basic blocks, so side-exit guards
may split one compiler block into several fragments. A linking transfer returns
to the beginning of the following fragment, while non-linking transfers include
jumps and tail calls.

Labels, code positions, fragments, and the three-pass finalizer are target-
independent. The finalizer is a template over the target's deferred-operation
and relocation types. The deferred-operation type supplies target-specific
operation kinds, size and form selection, final encoding, scratch registers,
PC-relative semantics, and the maximum pessimistic unit size. The relocation
type patches fixed-size instruction fields during final copying and cannot
change layout. A label itself identifies only a fragment boundary and is not
parameterized by either target type.

Every machine-code `Value` constant resides in a naturally aligned slot in a
separately identified GC-visible pool, including SMIs and booleans. The pool is
aligned to `sizeof(Value)` in a stable code-cache slice within target reach.
Fixed-size constant loads use machine-specific relocations rather than fragment
terminators: AArch64 patches a literal `LDR` or fixed far-pool `ADRP` plus `LDR`
template, while x86-64 patches one RIP-relative `MOV` after its final PC is
known.

Non-`Value` constants are excluded from the initial pool. Target macro
assemblers materialize them directly in instructions; on AArch64 an arbitrary
64-bit pattern requires at most one `MOVZ` or `MOVN` plus three `MOVK`
instructions. This sequence is known and encoded during program-order emission.
A separate non-GC literal pool is deferred until measurements justify its
additional layout and identification machinery.

Deferred operations use target-specific tagged kinds. Each stores its selected
form after the second pass and encodes that form during the third pass. An
operation that may expand receives one concrete scratch register from its
caller, defaulting to AArch64 `x16`; the emitter does not own a configurable
scratch pool. Initial macro operations may synthesize at most one address or
immediate at once. Additional simultaneous temporaries must be explicit
lowering constraints rather than hidden macro-assembler clobbers.

Finalization uses three fragment walks. The first computes fragment minimum and
maximum sizes and pessimistic start offsets, then rejects a pessimistic emission
unit larger than the target deferred-operation type's limit. The AArch64 limit
is 128 MiB. The emitter requests stable code and pool slices from the code cache.
The second walk assigns actual fragment starts and selects forms in program
order. Internal label targets use pessimistic source and target offsets,
including unresolved forward targets; shortening between them can only reduce
displacement magnitude. An outside-unit target uses its exact address and the
actual executable source PC already assigned by the second-pass cursor. The
third walk copies encoded templates, invokes relocations, and writes selected
trailing operations directly into the destination buffer, then populates the
pool. Relocations compute pool displacements from the final executable PC. Form
selection is not iterated. Each target exposes a direct assembler for exact
instructions and a macro assembler for operations that may expand.

Both target hooks distinguish the writable destination from the executable PC.
The writable destination is a `void *`; executable PCs, pool-slot addresses,
and resolved machine targets are opaque `MachineAddress` values. The latter
supports only checked offset advancement, checked signed byte or aligned
displacement, offset within an alignment, and raw-bit extraction for
materializing an indirect transfer target. The aligned queries provide the
architectural-page operations needed by AArch64 `ADRP` plus `LDR` without
exposing general address arithmetic. Target hooks store bytes through the
writable pointer and perform every displacement, page-relative calculation,
and reachability check through `MachineAddress`. This permits a Linux code
cache to use distinct RW and RX aliases without changing the emitter or target
encoders.

On AArch64, the macro assembler emits linking and non-linking absolute-target
transfers. Reachable targets use `BL` and `B`, respectively. Far targets are
materialized in the transfer's caller-supplied scratch register and use `BLR`
or `BR`; macro-assembler entry points default the scratch to `IP0` (`x16`). The
non-linking form supports tail calls. The 128 MiB pessimistic unit-size limit
guarantees that the unconditional `B` in an expanded intra-unit conditional
branch can reach its target.

### Context

AArch64 `TBZ` and `TBNZ` are attractive single-instruction type guards but
their limited range is not known for unresolved forward side exits. Reserving
two instructions at every guard would penalize dense speculative code. Basic
block fragments alone do not solve this because side exits are deliberately
absent from the compiler CFG. Retaining all machine instructions until layout
would instead create an unnecessary mandatory Machine IR.

### Alternatives Considered

- reserve the maximum instruction sequence at every conditional branch site;
- create side-exit stub islands within every narrow branch's reach;
- retain complete machine instructions or general assembler fragments;
- iteratively relax branch sizes to a fixed point;
- impose a compiled-region size below the narrowest target branch range.

### Why Chosen

Code fragments retain only the information whose encoding depends on final
machine-code positions while ordinary instructions are encoded once.
Pessimistic selection is correct without iteration because later shortening
only reduces internal branch distances, while outside-unit targets are tested
from source PCs already fixed by the second-pass cursor. The same mechanism
naturally supports x86-64 near-to-short jump selection, while accepting that a
few additional branches could shorten only after iteration.

### Consequences

- binding a label after emitted bytes begins a new fragment, and a label
  resolves to its fragment's start boundary; empty fragments are valid and may
  share an address with an adjacent boundary;
- labels and pre-finalization metadata positions are fragment-relative, and the
  continuation after any deferred operation is offset zero in the following
  fragment;
- the backend above the emitter owns block ordering, fall-through selection,
  block-condition inversion, edge moves, and removal of redundant
  unconditional branches;
- one compiler block may produce many code fragments without changing SSA or
  CFG structure;
- fixed-size PC-dependent instructions use fragment-relative, machine-specific
  relocations;
  only variable-size operations must terminate fragments;
- final encoding asserts that every selected short or direct form still fits;
- deferred operations store a caller-supplied scratch register, with AArch64
  `x16` as the macro-assembler default and a one-scratch maximum for implicit
  expansion;
- malformed label use and oversized units assert; allocation failure abandons
  compilation and retains interpreted execution, while final-encoding failure
  remains a hard compiler-invariant failure;
- outside-unit transfers require resolved absolute addresses by construction;
- the code unit exposes stable code and pool slices with final code size,
  pessimistic capacity, pool base, and slot count; the collector may rewrite
  pool slots without decoding instruction bytes;
- non-`Value` constants use target instruction materialization rather than a
  second literal pool;
- code allocation, W^X, reachability, and publication follow D-0010;
- the capped initial design does not require intra-unit branch veneers.

### Revisit When

- missed shortening opportunities become a measured code-size problem;
- veneer placement requires more general fragment scheduling;
- a target backend independently justifies a mandatory Machine IR;
- direct final copying becomes a material compilation-latency cost.

### References

- `doc/jit-machine-code-emission.md`
- `doc/jit-code-cache.md`
- `doc/jit-compiler-and-ir.md`
- `doc/jit-control-flow-graph.md`

## D-0010: Allocate Code and GC-Visible Pools Through a Reachability-Aware Code Cache

**Date:** 2026-07-20
**Status:** Accepted
**Scope:** JIT storage, target reachability, garbage collection, W^X, and code
publication
**Commitment:** Cross-subsystem runtime contract

### Decision

Compiled code and its GC-visible `Value` pool occupy distinct non-moving slices
owned by one compiled code object. Code slices expose separate writable and
executable addresses; pool slices remain writable and non-executable. The code
cache places both slices within target PC-relative reach. A machine-emission
attempt fixes its pool-load width before allocation; successful allocation
fixes the addresses before address-dependent form selection and final
PC-relative encoding.

The first executable implementation gives each code unit a private page-rounded
code mapping, emits while writable, transitions it once to read-only executable
memory, and never reopens it. Pool pages are shared by many functions because
their permissions remain RW/NX. The later macOS implementation packs functions
within `MAP_JIT` pages and uses thread-local JIT write protection without
changing emitter APIs or executable addresses.

The interface also supports the intended Linux implementation in which one
physical code allocation has distinct RW and RX virtual aliases. Deferred
operations and relocations receive both addresses, write only through the RW
view, and calculate all encoded PCs from the RX view.

The cache prefers placement within AArch64's approximately 1 MiB literal-load
range. A far-pool path using `ADRP` plus `LDR` remains required for units that
cannot use near placement. Fixed-size pool loads use per-fragment relocations;
only operations whose size remains undecided terminate fragments.

An AArch64 attempt first emits in near-literal mode. A typed near-placement
rejection discards that machine-emission attempt and retries once in forced
far-page-relative mode before final encoding or publication. Actual code or
pool allocation failure abandons JIT compilation, publishes nothing, sets no
Python exception, and continues execution in the interpreter.

### Context

Placing a moving-GC-rewritten pool in the same page as executable code would
require making code pages writable during collection. Giving every function a
dedicated pool page would instead waste substantial memory. Reopening a shared
published code page with process-wide permission changes can also remove execute
permission while another thread is running code from that page.

Separating page-protection domains while retaining bounded address placement
lets the collector update packed pool slots safely. A stable writable view and
executable address also permit a simple page-per-unit bring-up allocator to be
replaced later by macOS JIT mappings without changing encoding or branch
calculation.

### Alternatives Considered

- place code and pool in one contiguous mapping and transition permissions
  during garbage collection;
- allocate dedicated code and pool pages for every function;
- pack functions into ordinary code pages and reopen published pages with
  process-wide permission changes;
- require a writable alias implementation before executable bring-up;
- permanently decline JIT compilation for every unit outside near-pool reach.

### Why Chosen

Private page-rounded Tier-1 code mappings provide the simplest safe one-way W^X
transition. Shared RW/NX pool pages avoid per-function data-page overhead.
Reachability-aware placement preserves efficient target loads, while the
writable-view/executable-address split contains platform publication policy
inside the code cache. `MAP_JIT` later recovers dense code-page packing on the
initial macOS target without exposing page mechanics to the compiler.

### Consequences

- code and pool slices never move while compiled code may execute;
- the moving collector rewrites pool slots without changing code permissions;
- Tier-1 code capacity is rounded to private pages and may waste tail space;
- a process-wide transition never reopens a page containing published code;
- direct outside-unit targets must remain stable for the source unit's lifetime;
- the allocator, not the emitter, owns page size, virtual placement, writable
  scopes, instruction-cache synchronization, and publication;
- target encoders never derive an executable PC from a writable pointer, so
  distinct Linux RW and RX aliases preserve correct relocation arithmetic;
- unusually large units need a far-pool allocation/emission path rather than an
  unconditional permanent refusal to compile;
- near-placement rejection causes one deterministic far-mode re-emission;
- actual code-cache allocation failure is a recoverable compilation outcome,
  leaving the interpreter as the executable implementation;
- initial code is immortal until retirement, dependency tracking, and slice
  reuse are designed.

### Revisit When

- measurements show Tier-1 page rounding materially limits bring-up workloads;
- the macOS `MAP_JIT` tier is implemented and validated under concurrency;
- another platform requires writable aliases or a different JIT publication
  mechanism;
- code-cache fragmentation or region exhaustion requires compaction or
  reclamation.

### References

- `doc/jit-code-cache.md`
- `doc/jit-machine-code-emission.md`
- `doc/jit-compiler-and-ir.md`
- `doc/jit-compiler-bring-up-plan.md`
