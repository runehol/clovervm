# JIT Near-Term Implementation Roadmap

| Field | Value |
|---|---|
| Document type | Implementation roadmap |
| Status | Active |
| Scope | Prioritized work that turns the current executable AArch64 JIT slice into a broader and runtime-complete compiler |
| Design authority | [JIT Compiler and IR](jit-compiler-and-ir.md), [JIT Register Allocation](jit-register-allocation.md), [JIT Side-Exit Lowering](jit-side-exit-lowering.md), and [JIT Transition Programs](jit-transition-program.md) |
| Validated against | `b2fe5ecb` (2026-08-01) |

This roadmap records implementation order rather than adding architecture. The
owning design documents remain authoritative for IR, allocation, recovery,
calling conventions, and publication. Exact opcode coverage remains an
implementation fact; the slices below identify semantic families and the
infrastructure they exercise rather than maintaining an opcode ledger.

## Ordering Principles

Near-term work follows four rules:

1. Remove structural and allocation-created waste that would otherwise become
   pressure, spills, or repeated work in every widened operation.
2. Extend allocator contracts when the desired code shape depends on location
   choice; do not repair poor allocation with emitter peepholes.
3. Keep safepoint and recovery correctness current as compiled execution
   becomes longer-lived.
4. Widen through operations that reuse the existing Snapshot, guard, side-exit,
   and overflow machinery before taking on calls or allocation.

## Implementation Sequence

### 1. Fuse Value Tests With Branches

Recognize the existing single-use pattern:

```text
Is or IsNot -> ConditionalBranch
```

and lower it to one Machine compare-and-branch terminator. AArch64 can then emit
`cmp` followed immediately by `b.cond` without materializing tagged `True` and
`False` or retesting the result. If the Python boolean has another use, retain
ordinary materialization and tagged truthiness testing.

Identity tests provide the first executable fixture and establish the fusion
shape before SMI comparisons are added. The optimization should remove the
intermediate boolean from Machine IR so allocation does not model a value that
emission will never materialize.

### 2. Poll Safepoints on Loop Backedges

Add a cheap safepoint poll to compiled loop backedges. The unrequested path
must stay in generated code without publishing frame state or calling a helper.
When a poll is requested, take an ordinary side exit at the committed state for
the target bytecode block and let interpreter reentry perform safepoint handling
before dispatch.

Cover a loop with live loop-carried values and request a safepoint while it is
executing compiled code. The test must prove both that roots are published and
that execution resumes at the backedge target without replaying loop-body
effects.

### 3. Widen SMI Operations and Comparisons

Extend the direct Core translator through operation families that reuse the
existing inline guards, side-exit state, and Machine emission:

- overflow-checked subtraction, including compact-immediate bytecode forms;
- direct translation and guarding for the existing `AndSMI`, `OrrSMI`, and
  `EorSMI` instructions;
- backend folding of encodable tagged SMI constants into immediate forms;
- ordinary SMI comparison results, connected to the established
  compare-and-branch fusion when their result has only the branch use.

Boolean-as-integer handling must remain explicit. Python's bool/bool and mixed
bool/int operators can differ in result type, so `SMIOrBoolean` guards must not
replace SMI guards until the lowering represents those semantics.

Multiplication, shifts, division, modulo, and exponentiation remain later
slices because their tagged arithmetic, failure cases, or result growth require
additional lowering.

### 4. Add Ordinary Spill Storage

Stage ordinary spilling rather than combining every spill mechanism into one
change:

1. Provide allocator-owned per-value spill slots, assign eligible
   register-free bundles to them, and materialize the resulting loads and
   stores.
2. Trim register-free regions around pressure splits into spill bundles and
   record the register-to-spill and spill-to-register connectors.

Preserve clean compilation failure for pressure that the completed stage cannot
yet handle. This precedes substantially wider expression, object, and call
coverage. The symbolic allocation checker and adversarial pressure tests in
[JIT Register Allocation Open Work](jit-register-allocation-progress.md)
should grow with this slice.

### 5. Add Shape-Guarded Known-Field Access

Introduce Machine side-exit forms and AArch64 emission for shape and validity
guards, followed by known-offset field loads. This is the first high-leverage
object-oriented slice and should reuse the established runtime exit path.

Guard commoning and motion may begin here, once repeated shape checks exist in
real generated programs. Keep shape and validity checks independently
optimizable as required by their separate Core instructions.

### 6. Approach Calls, F64, and General Sinking

Calls require managed frame-header construction, stack-passed argument support,
fixed argument and result locations, clobber validation, `x25`-to-`x0` native
helper adaptation, call-boundary interpreter-state synchronization, and
safepoint policy. Managed return must restore the caller frame state; calls
should not be introduced as emitter-only sequences.

For JIT-to-JIT calls, move ownership of return-address and return-`CodeObject`
spilling from the caller to the callee. The caller should transport that return
context in the compiled calling convention; the callee materializes canonical
frame-header cells only when its own calls, safepoints, exits, or register
pressure require them. A leaf function can then keep the return context in
registers and return without frame-header stores.

F64 support requires SIMD stack transfers, float guards, boxing allocation, and
recovery computation. General transition sinking becomes valuable with boxing
and other values that exist only for interpreter-visible exit state; guards
themselves remain on the executable path because a transition program cannot
recursively side exit.

Semantic IR, inference, inlining, and polymorphic partition realization remain
deferred until the direct Core path has broader executable coverage and
measurement shows that frontend inference would pay for its compilation cost.

## Measurement and Review

Before tuning allocator heuristics or adding broad optimization machinery,
record compact per-compilation statistics for:

- Core and Machine instruction counts;
- side-exit count, unique transition-program count, and transition bytes;
- hot and cold machine-code bytes;
- moves introduced by guards, block edges, splits, and spills;
- allocator evictions, splits, spills, and compilation failures;
- compilation time by major phase.

Use those counts to decide when to intern transition programs. If distinct
bindings emit byte-identical finalized `TransitionInstruction` sequences,
publish one untagged payload for the equivalent group and then merge any cold
stubs that become identical. Program comparison must occur after physical
input locations are bound; Snapshot or region identity alone is not
sufficient.

A small representative compilation corpus is preferable to a large set of
tests that merely restate instruction construction. Execution tests should
cover semantic branches and exits; structural tests should pin only valuable
contracts such as fused branches, eliminated transfers, shared cold exits, and
calling conventions.

Revisit this sequence when measurements show a different dominant cost, when a
slice exposes a missing architectural contract, or when the runtime transition
work changes the required ordering. Completed detail belongs in the owning
design or implementation ledger rather than accumulating indefinitely here.
