# JIT Compiler and IR

This document records the current assumptions, constraints, and design
guardrails for a future clovervm JIT compiler and its intermediate
representation. It is intentionally not an implementation plan or a complete
IR specification. Its purpose is to constrain later design work so that JIT
execution remains compatible with the existing bytecode, object model, inline
caches, calling convention, and reclamation machinery.

## Foundational Invariant: Bytecode Is Canonical

The existing bytecode is the canonical execution and recovery model. JIT code
is a speculative execution of that bytecode, not a replacement language with
independent state or semantics.

Compiled execution must be able to return to the interpreter at essentially
any speculative point, including:

- a failed shape or shape-key check;
- an invalid validity cell;
- SMI arithmetic overflow;
- an inline-cache miss;
- a safepoint request;
- any other failed assumption used by compiled code.

At such an exit, the JIT must recover the interpreter-visible bytecode state at
the appropriate bytecode position. The recovery model should use canonical
bytecode registers and frames rather than require an unrelated, arbitrary
interpreter-value map at every exit.

The exact deoptimization mechanism is not yet specified, but designs that make
bytecode state expensive or impossible to reconstruct are out of scope.

## Bytecode State and Frames

The bytecode uses an accumulator and stack-backed registers. Registers cover,
in order:

1. arguments to the current function;
2. local variables;
3. temporaries;
4. outgoing arguments for the function about to be called.

Moving the frame pointer changes the interpretation of these slots, in a form
similar to register windows. This layout is already the canonical format used
by interpreted calls and must remain easy to recover from compiled execution.

The JIT should use machine registers aggressively, but each bytecode register
retains a stable canonical frame home. Machine registers may cache or temporarily
hold newer values, subject to the synchronization and safepoint rules below.

The accumulator has existing special treatment during safepoint publication:
it is published through `ThreadState` separately from the scanned frame range.
The JIT should reuse that mechanism rather than add an accumulator frame slot.

## Safepoints, Reclamation, and Canonical Homes

The initial JIT should preserve the interpreter's reclamation model rather than
introduce compiled-frame stack maps.

The governing invariant is:

```text
Compiled code may keep dirty bytecode state in machine registers only while
reclamation is impossible. Before reclamation can occur, interpreter-visible
values are synchronized to their canonical frame homes and the accumulator is
published through the existing ThreadState safepoint state.
```

Consequences:

- A machine-register copy of a synchronized frame value is a cache; the
  canonical frame slot remains its managed stack root.
- Before entering Python code, a native function, a runtime helper, or any
  other operation that may reclaim, compiled code synchronizes the current
  bytecode frame.
- If a safepoint poll trips, compiled code synchronizes the frame, publishes
  the accumulator, and deoptimizes. Reclamation then proceeds through the
  existing interpreter-safe mechanism.
- An untripped safepoint poll need not synchronize state, provided reclamation
  cannot begin asynchronously while compiled state is dirty.
- Non-bytecode IR temporaries cannot remain live across a reclaiming operation
  unless they are first represented in recoverable managed state.

Treating a tripped safepoint as a deoptimization boundary is an intentional
initial simplification. Compiled-frame scanning can be considered later if
measurements justify its complexity.

## One Managed Calling Convention

Interpreted and compiled Python calls should initially use the existing managed
calling convention:

- arguments are passed through the stack-backed outgoing argument window;
- moving the frame pointer establishes the callee frame;
- the return value is passed through the accumulator in its canonical machine
  register;
- the caller's bytecode-visible state is synchronized before a call that may
  reclaim or enter arbitrary code.

This convention applies to interpreted-to-compiled, compiled-to-interpreted,
and compiled-to-compiled calls. Cross-mode stubs may select the execution
engine and continuation, but should not translate between separate argument
ABIs.

The frame already has slots for interpreted and compiled return PCs. These may
distinguish normal interpreted and compiled continuations without changing the
argument convention. Their exact use remains to be designed.

Compiled-to-compiled call performance should come primarily from inlining,
not from a second register-passing ABI. A separate fast-entry ABI is not part
of the initial design.

Native and C++ calls remain a distinct boundary. They use their required native
ABI, but compiled managed state must be synchronized before any such call that
may allocate, reclaim, call Python, or otherwise require published roots.

## Inline Caches Drive Specialization

The JIT should compile the successful path recorded by an inline cache and
return to the interpreter when the cache assumptions do not hold. It should not
reimplement the generic Python protocol at every compiled operation.

For example, a bytecode `Add` may decompose into:

```text
ShapeKeyCheck
ShapeKeyCheck
ValidityCellCheck       # when required by the cache
SmiAdd | TrustedFunctionCall | PythonFunctionCall
```

The checks establish facts and have no Python-visible side effects. Only the
final selected operation may perform the operation's effects. On a failed
pre-operation check, execution returns to the original bytecode so the
interpreter can run the generic path.

An arithmetic operation such as `SmiAdd` may itself have a speculative failure
edge. SMI overflow returns to the original bytecode, which can produce the
appropriate heap integer or run another applicable path.

The final action and its guards form one semantic unit. Recognizing or
specializing the final action never permits dropping guards required to prove
that the inline cache still describes the Python-visible operation.

## Commit Boundaries

Each lowered bytecode operation has a commit boundary.

Before the final effectful operation runs, failed speculation can reconstruct
the state before the bytecode and retry that bytecode in the interpreter. Once
an effect has occurred, the bytecode cannot safely be retried.

The eventual IR and exit machinery must therefore distinguish:

- pre-effect exits that resume at the current bytecode;
- exits after a committed result that resume at a later bytecode state;
- exceptional exits that preserve pending exception state without repeating
  an already-performed effect.

General compiled exception handling is deliberately parked. The current
direction is to leave compiled mode when an operation raises and use the
interpreter's existing exception tables and unwinding. The exact committed-state
handoff for such exits still needs design work.

## Ordered List-Based SSA

The canonical function representation should be a control-flow graph of basic
blocks, with an ordered instruction list in each block. Instructions have SSA
operands and results, and blocks begin with block parameters or phi nodes and
end with explicit normal-flow terminators. Instructions within a block may also
carry explicit non-returning deoptimization side exits.

The list is the current schedule. SSA edges expose value dependencies, proof
values expose guard dependencies, and effect/dependency annotations determine
which instructions may legally move. List order does not create a false semantic
dependency between otherwise independent pure operations; it records their
chosen placement until a transformation deliberately moves them.

This representation fits clovervm's execution model:

- guards have bytecode locations and bailout frame states;
- effectful operations create commit boundaries;
- shape and validity proofs have control- and effect-bounded lifetimes;
- calls and safepoints require canonical frame synchronization;
- moving an instruction can change the values live at a deoptimization point.

Python also makes source order a useful conservative default. A generic
arithmetic bytecode can invoke overloaded Python methods and must initially be
treated as call-like. Only after inline-cache-driven expansion may it become a
pure `SmiAdd`, a recognized operation with declared effects, or another safely
movable instruction.

The out-of-order AArch64 and x86-64 targets recover substantial instruction-
level parallelism dynamically. Compiler scheduling still matters for dependency
chains, register pressure, flags, loads, branches, and front-end behavior, but
the semantic optimizer does not need a VLIW-style globally unscheduled graph.

A temporary DAG or sea-of-nodes representation may still be useful inside a
narrow optimization or instruction-selection task. It should not be the
canonical whole-function representation.

## Compiler Phases

The current design has three principal IR levels.

```text
Semantic IR -> Guard IR -> Machine IR
```

### Semantic IR

Semantic IR does not operate directly on the encoded byte array in
`CodeObject::code`. It is a parsed, list-based SSA representation whose atomic
operations resemble the semantic bytecode operations emitted by the high-level
compiler. The encoded bytecode, operand bytes, and inline-cache arrays are
inputs to this parsing step, not the representation optimized by later passes.

Parsing decodes instructions, forms basic blocks and control-flow edges, records
the original bytecode PC for recovery, and snapshots the relevant inline-cache
semantics into compilation-local data. The parsed bytecode operations remain
atomic at this level while accumulator and register uses are renamed into SSA
values.

This representation owns:

- bytecode basic blocks and dominance;
- SSA construction for the accumulator and bytecode registers;
- compile-local inline-cache specialization plans;
- type and shape propagation;
- context-sensitive inlining;
- logical bytecode frame states for exits and inlined frames.

The snapshot should capture the semantic content needed for compilation, not
blindly copy the runtime cache structs and treat them as mutable compiler IR.
Caller facts may refine an inlined specialization plan, but they do not on their
own identify a trusted handler or Python target. Any selected action must remain
justified by cache feedback or by the runtime's trusted resolution mechanism.

Inlining and fact propagation may run iteratively: propagate facts, inline newly
eligible calls, rebuild affected control-flow information, and propagate again.
The iteration must stop at stability or a compilation budget.

### Guard IR

Bytecode operations are then expanded into smaller semantic operations. For
example, an `AddBytecode` becomes its required shape and validity guards plus an
`SmiAdd`, recognized trusted operation, generic trusted call, or Python call.

This level owns proof-producing guards, common proof elimination, effect-aware
code motion, and other high-level optimizations. Bytecode frame states remain
attached to deoptimizing operations.

### Machine IR

Guard IR is lowered into a machine-oriented SSA or virtual-register IR. This
level owns register classes, calls, overflow flags, addressing constraints,
spills, branches, register allocation, and final instruction encoding.

Machine copies, spills, and reloads are physical locations of semantic values;
they do not create new Python value identities. The machine IR should remain a
lowering and allocation representation unless later measurements justify
machine-level optimization passes.

Ordered instruction lists are used at all three levels. Semantic IR begins in
decoded bytecode order. Guard IR explicitly orders guards, operations, calls,
and commit points. Machine IR provides the final target-level schedule, with
room for limited local scheduling where measurements justify it.

## Mutable CFG and Control-Flow-Producing Lowering

The CFG parsed from bytecode is an initial scaffold, not a fixed graph. Every
lowering level must be able to introduce, remove, and restructure control flow.

This is required for:

- IC specialization with multiple cases;
- arithmetic overflow and other deoptimization exits;
- inlining a callee CFG;
- out-of-line trusted, Python-call, or materialization paths;
- future compiled exception handling;
- machine slow paths, stubs, and target-specific branches.

A lowering is therefore allowed to replace one high-level operation with an
arbitrary CFG region, not only a linear instruction sequence.

The CFG infrastructure needs first-class operations to:

- split a block at an instruction;
- insert branches and joins;
- add, remove, and redirect edges;
- update block parameters or phi inputs;
- clone and splice regions;
- attach bytecode origins and frame states to newly introduced exits.

Major representation boundaries should normally construct a fresh destination
CFG. Semantic IR lowering builds a new Guard IR CFG, and Guard IR lowering
builds a new Machine IR CFG. This keeps source nodes intact during translation
and makes one-to-region lowering natural. Optimizations within one IR level may
use an in-place CFG editor.

Deoptimization exits must be visible to frame-state and correctness analysis.
They need not be ordinary successors in the normal CFG used for dominance and
loop analysis. To avoid creating a tiny normal basic block after every guard, an
ordered guard instruction may own an explicit non-returning side exit and frame
state while normal execution falls through to the next instruction. The exit
must not be hidden until machine code generation.

### Lazy analysis invalidation

Dominance, loop structure, reverse postorder, and propagated facts are derived
from the current CFG and must never silently survive structural mutation.

The initial implementation should invalidate broadly and recompute lazily. A
function carries a CFG generation number, and cached analyses record the
generation from which they were computed. Any block or edge mutation through
the official CFG editor advances the generation. Requesting stale analysis
recomputes it on demand.

Transformations must not edit predecessor and successor structures directly.
Centralizing mutation in the CFG API makes invalidation difficult to forget.
A newly constructed destination graph starts without cached analyses.

If broad lazy invalidation becomes measurably expensive, the pass manager may
later support narrower preservation declarations or incremental maintenance.
Correct broad invalidation is the initial policy.

## SSA, Control Flow, and Guard-Derived Facts

The IR needs explicit basic blocks, SSA values, dominance, and an effect model.
Shape guards are not merely control checks: they establish type and layout facts
about SSA values.

A successful guard establishes a fact only in regions where:

- the guarded SSA value is still the value being used;
- the guard dominates the use;
- no intervening operation invalidates the kind of fact established.

At control-flow merges, incoming facts are intersected. A fact remains known
only if it is established on every incoming path and remains valid along each
path.

The IR must distinguish probable facts from proven facts. Profile or IC
information may motivate a guard, but it cannot be consumed as proof until the
guard succeeds. Once established, the proven fact is available to dominated
uses under the applicable invalidation rules.

### Preserve semantic value identity

SSA construction should avoid introducing new semantic value identities for
operations that only move an existing value:

- `Ldar`, `Star`, and `Mov` update the accumulator/register environment to
  reference an existing SSA value; they do not produce copy nodes.
- Guards establish facts about their input SSA values; they do not return
  narrowed replacement values.
- Expanding a bytecode preserves the bytecode result identity. The final
  semantic result node inherits the result identity of the atomic bytecode.
- Only genuine value producers and non-trivial control-flow merges introduce
  new semantic value identities.
- Trivial phi nodes or block arguments with identical incoming values are
  eliminated.

For example:

```text
LdaSmi 1       accumulator -> %v1
Star r2        r2          -> %v1
Ldar r2        accumulator -> %v1
Mov r2, r3     r3          -> %v1
```

All of these bytecode locations refer to the same semantic value `%v1`.

### Proof values

A guard produces an SSA proof value rather than a narrowed runtime value:

```text
%lhs_is_smi = ShapeKeyCheck %lhs, Smi
%rhs_is_smi = ShapeKeyCheck %rhs, Smi

%result = SmiAdd %lhs, %rhs
    requires %lhs_is_smi, %rhs_is_smi
```

Proof values have no runtime `Value` representation. They make the dependency
between a guard and a specialized operation explicit while preserving the
identity of the guarded Python value.

An IR verifier should reject a specialized operation when a required proof:

- refers to the wrong SSA value;
- proves the wrong property;
- does not dominate the operation;
- depends on an obsolete effect state.

Proofs may include the state on which their validity depends:

```text
InlineShapeProof(value, shape)
StableShapeProof(value, shape)
MutableShapeProof(value, shape, shape-effect state)
ValidityProof(cell, validity-effect state)
```

Operations that may change shapes or invalidate validity assumptions advance
the relevant effect state, preventing old proofs from being consumed afterward.
Pure operations leave those states unchanged.

Proofs can be commoned and moved under ordinary SSA dominance and effect rules.
Guards require more care because each guard also has a bytecode location and a
bailout frame state.

The optimizer should therefore think in terms of commoning proofs, not blindly
hash-consing guards. A later guard may be removed when an equivalent proof:

- is produced by an earlier guard that dominates it;
- proves the same predicate about the same value;
- remains valid under the current effect state.

The dominating guard keeps its original bytecode bailout location. If equivalent
guards occur on sibling branches, neither proof dominates the other and ordinary
common elimination does nothing. Hoisting them to a common dominator is a
separate transformation that requires a legal bailout state and proof that any
crossed work may safely be replayed.

Proof values are erased before machine lowering. They constrain transformations
but consume no machine registers and emit no code beyond the guards that remain.

### Logical and materialized frame state

Canonical bytecode slots are properties of logical frames, not properties of
SSA values. The same SSA value may inhabit several registers, the accumulator,
or several inlined frames, while one canonical slot contains different SSA
values at different program points.

Each bytecode boundary therefore has a logical frame state:

```text
FrameState:
    CodeObject
    bytecode pc
    parent FrameState       # for an inlined caller
    accumulator -> SSA value
    register 0  -> SSA value
    register 1  -> SSA value
    ...
```

Frame states should be immutable and structurally shared, recording sparse
changes rather than copying the whole register file at every bytecode.

The compiler should distinguish:

```text
logical frame state:
    the SSA value each interpreter-visible location currently denotes

materialized frame state:
    the value currently committed to each canonical memory slot
```

Exit and call synchronization writes the difference required to make the
logical state canonical. A canonical slot may be a register-allocation affinity
or lowering preference, but it does not define an SSA value's identity.

## Shape Facts

Shape-key guards provide much of the JIT's type information, but shape facts do
not all have the same lifetime.

### Inline values

For inline values such as SMIs, the shape key follows from the value bits. The
fact remains valid while the SSA value remains unchanged. An operation that
produces a new value, such as overflowing SMI arithmetic, does not transfer the
old fact to the result.

### Mutable-shape heap values

General heap objects can change shape when properties are added or deleted, or
through other operations such as supported `__class__` assignment. Aliases can
cause these changes non-locally.

A shape fact for such an object may be moved only across operations proven not
to change the object's shape, including through aliases. Dominance by itself is
not sufficient.

### Stable-shape heap values

Some heap values have lifetime-stable shapes. An exact tuple value is an
example: its instance cannot add or delete attributes or undergo the mutable
instance class-change path.

Once a guard proves such a stable shape for an unchanged SSA value, the shape
fact can survive arbitrary calls. Shape stability is stronger than merely
observing flags that disallow ordinary attribute transitions; it must be a
runtime invariant for that kind of value. The representation of this stability
classification remains to be designed.

Shape stability does not imply lookup stability. A tuple object's own shape may
remain unchanged while mutation of its class or MRO invalidates a cached method
lookup. Shape facts and validity-cell facts must be tracked independently.

## Validity-Cell Facts

Validity cells represent assumptions about non-local mutable runtime state.
They are harder to hoist than shape checks because calls and other memory-
accessing operations may indirectly trip them.

The initial optimizer should be conservative:

- validity checks may be reused across pure arithmetic and similarly constrained
  operations with no relevant memory access;
- calls, arbitrary runtime helpers, and other operations capable of non-local
  mutation are barriers unless proven otherwise;
- validity-check optimization is expected to be primarily local redundancy
  elimination rather than aggressive loop or dominator-tree hoisting.

The IR effect model must be precise enough to prevent unsound code motion, but
validity-check optimization should not dictate an unnecessarily elaborate
memory model before evidence shows it is useful.

## Operation Effects

Guard motion depends on knowing what intervening operations can do. IR
operations and recognized runtime semantics therefore need conservative effect
and dependency descriptions.

An effect summary states what an operation might change. A dependency summary
states what an operation observes or which assumptions it requires. Code motion
is legal only when the moved operation's dependencies do not intersect the
effects of crossed operations and commit/control ordering remains valid.

Relevant properties include whether an operation:

- reads or writes memory;
- may change an object's shape, including through aliases;
- may invalidate lookup assumptions or validity cells;
- may call Python;
- may allocate or reach a safepoint;
- may raise;
- may deopt;
- has an irreversible or Python-visible effect;
- is pure arithmetic.

Obvious IR operations should receive precise defaults from their operation
definitions. A `ShapeKeyCheck`, for example, has a standard dependency and
deoptimization shape. Recognized trusted calls receive their effects from their
semantic descriptors. Python calls and unknown operations begin maximally
conservative.

Effect implications should be encoded centrally. For example, `MayCallPython`
should imply broad heap access, possible shape mutation, validity invalidation,
raising, and safepoint behavior. This prevents internally inconsistent
annotations.

An omitted or incorrect effect is a correctness bug, not merely a missed
optimization.

## Trusted Handlers and Semantic Recognition

Operator inline caches may select trusted native handlers. For selected handler
pointers, the JIT should recognize the handler's semantic meaning and replace a
generic call with specialized IR.

Recognition must be explicit and conservative:

```text
trusted handler pointer + arity
    -> runtime-neutral trusted semantic descriptor
    -> JIT-specific lowering
```

The descriptor should identify the semantic operation, operand convention and
coercion case, result kind, and conservative effects. For example, float-float
addition and float-intlike addition are distinct semantic cases even if they
eventually lower to similar machine instructions.

The owning builtin type file should declare or register the meaning of its own
trusted handlers. Type-specific coercion, reflected ordering, and trusted
handler semantics remain in that owning layer. The JIT owns the mapping from a
semantic descriptor to concrete IR nodes.

Trusted handlers should not name concrete JIT opcodes. Keeping a semantic layer
between handlers and IR allows the IR, compilation tiers, and machine lowering
to evolve without coupling builtin implementations to a particular compiler
representation.

Only explicitly recognized handlers receive specialized lowering. An unknown
handler remains a generic `TrustedFunctionCall`. Specialization retains all
shape and validity guards required by the inline cache.

## Context-Sensitive Inlining and Type Propagation

Inlining must propagate caller facts into the callee. This is important when a
function is globally polymorphic but one call site supplies well-known types.

Bytecode-to-IR construction should therefore accept an incoming abstract state:

```text
bytecode register -> SSA value + proven facts
```

When a callee is inlined, its parameter registers bind directly to the caller's
argument SSA values. Proven caller facts become entry facts for the inlined
callee. Callee guards that are redundant under those facts may be removed,
provided their shape-stability and effect constraints are satisfied.

These facts belong to a compilation and inline context, not to the `CodeObject`
globally. The same bytecode may be compiled standalone or inlined under several
different argument specializations.

Caller facts can eliminate guards from an inline-cache specialization, but do
not by themselves identify the correct trusted handler or Python target. The
selected action must still come from the cache or from the same trusted
resolution mechanism used by the runtime.

### Deoptimizing inlined code

A bailout inside an inlined callee may need to materialize more than one
bytecode frame. Once the inlined callee has performed effects, the caller's call
opcode generally cannot be retried.

Deoptimization state is therefore logically a stack of bytecode frames. Each
inlined instance needs recoverable identities for its `CodeObject`, bytecode PC,
accumulator, and canonical registers. The design should prefer stable homes and
per-inline-instance layout over arbitrary per-exit interpreter maps, but the
exact materialization scheme remains open.

## Value Representation

The existing `Value` representation is the default JIT representation.

- Tagged heap pointers remain in `Value` form and can be dereferenced using the
  existing representation.
- SMIs remain shifted left by five bits.
- SMI addition and subtraction can usually operate directly on encoded values
  and use native overflow flags.
- Operations such as multiplication and address indexing perform shifts only
  where required.
- Tagged values move directly between machine registers and canonical frame
  slots, reducing deoptimization cost.

The initial JIT should use boxed `Value`s exclusively. It should not require
general unboxing merely to execute ordinary compiled code.

The IR should nevertheless keep semantic value identity separate from physical
representation so later representation selection is possible. It must not
assume that one SSA value corresponds to exactly one canonical bytecode slot.

## Future Unboxed Floats

Unboxed floats are an advanced optimization, not an initial requirement. The
design must leave room for two different cases:

1. An unboxed cache of an existing boxed float. The original box remains the
   canonical value and preserves identity; deoptimization discards the cache.
2. A new unboxed result produced by arithmetic. It represents a virtual Python
   float object and must be materialized into a new box when required by
   deoptimization, escape, or an identity-sensitive operation.

A virtual float result has a materialization identity as well as a numeric
value. If one virtual result is present in multiple bytecode slots or inlined
frames, materialization must allocate one box and place that same `Value` in
every location. Independently boxing each occurrence would break Python `is`
semantics.

For this design, materialization allocation may be treated as infallible. The
runtime is expected to request a reclamation safepoint before memory exhaustion.

No unboxed-float nodes, representation selection, or materialization machinery
need to be implemented in the first JIT. The initial IR must only avoid making
them impossible to add.

## Observability

Tracing, traceback construction, stack inspection, and similar facilities may
require canonical bytecode frames and PCs even without a failed speculative
guard.

A plausible initial policy is to treat activation of such facilities as a
deoptimization request, returning execution to fully materialized interpreter
frames. This policy has not yet been selected as a firm design requirement.

## Deliberately Open Questions

The following have not yet been designed or agreed:

- the concrete storage layout and APIs for the agreed list-based SSA nodes and
  blocks;
- the precise effect taxonomy and alias model;
- the optimizer and register allocator structure;
- the exact side-exit and canonical-slot synchronization mechanism;
- how machine locations are recovered without large arbitrary maps at every
  deoptimization point;
- the physical layout used to materialize nested inlined frames;
- the stable-shape classification mechanism;
- compilation, invalidation, and lifetime rules for code derived from changing
  inline-cache contents;
- normal compiled-return dispatch through the compiled-PC frame slot;
- general compiled exception handling and cross-frame compiled unwinding;
- the exact observability and tracing policy;
- backend selection, code memory management, tiering, and compilation triggers.

These questions should be answered within the constraints above rather than by
weakening bytecode compatibility or introducing a second canonical runtime
state model.
