# JIT Near-Term Implementation Roadmap

| Field | Value |
|---|---|
| Document type | Implementation roadmap |
| Status | Active |
| Scope | Prioritized work that turns the current executable AArch64 JIT slice into a broader and runtime-complete compiler |
| Design authority | [JIT Compiler and IR](jit-compiler-and-ir.md), [JIT Register Allocation](jit-register-allocation.md), [JIT Side-Exit Lowering](jit-side-exit-lowering.md), and [JIT Transition Programs](jit-transition-program.md) |
| Validated against | `b847eed0` (2026-08-02) |

This roadmap records implementation order rather than adding architecture. The
owning design documents remain authoritative for IR, allocation, recovery,
calling conventions, and publication. Exact opcode coverage remains an
implementation fact; the slices below identify semantic families and the
infrastructure they exercise rather than maintaining an opcode ledger.

## Ordering Principles

Near-term work follows four rules:

1. Keep safepoint and recovery correctness current as compiled execution
   becomes longer-lived.
2. Add pressure handling before substantially widening expression, object, and
   call coverage.
3. Extend allocator contracts when the desired code shape depends on location
   choice; do not repair poor allocation with emitter peepholes.
4. Widen through operations that reuse the existing Snapshot, guard, side-exit,
   tagged-fact, and overflow machinery before adding new semantic machinery.

## Implementation Sequence

### 1. Poll Safepoints on Loop Backedges

Add a cheap safepoint poll to compiled loop backedges. The unrequested path
must stay in generated code without publishing frame state or calling a helper.
When a poll is requested, take an ordinary side exit at the committed state for
the target bytecode block and let interpreter reentry perform safepoint handling
before dispatch.

Cover a loop with live loop-carried values and request a safepoint while it is
executing compiled code. The test must prove both that roots are published and
that execution resumes at the backedge target without replaying loop-body
effects.

### 2. Add Ordinary Spill Storage

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

### 3. Complete the Direct Operator Slice

Extend the direct Core path through the remaining common operators whose fast
paths fit the existing guard and side-exit model. Floor division and modulo are
the next arithmetic pair: both need explicit zero and signed-overflow handling,
and floor semantics must be preserved when operand signs differ. Shifts follow
once their negative-count, large-count, and result-growth cases have a precise
fallback contract. Add unary operators where they exercise the same machinery.

Keep boolean-as-integer behavior explicit. Python's bool/bool and mixed bool/int
operators can differ in result type, so broader guards must not silently change
the operation selected or its result type. Exponentiation remains later because
its common cases quickly cross into allocation and arbitrary-precision results.

This slice should extend the existing instruction families and common lowering
functions rather than introducing one-off instructions. Preserve comparison and
branch fusion, tagged-fact propagation, and redundant-guard elimination as the
operator set grows.

### 4. Add Shape-Guarded Known-Field Access

Introduce Machine side-exit forms and AArch64 emission for shape and validity
guards, followed by known-offset field loads. This is the first high-leverage
object-oriented slice and should reuse the established runtime exit path.

Guard commoning and motion may begin here, once repeated shape checks exist in
real generated programs. Keep shape and validity checks independently
optimizable as required by their separate Core instructions.

### 5. Establish the First Call Slice

Start with guarded calls to trusted native handlers certified `NoCallPython`,
`NoSafepoint`, and `NoRaise`, following the staged
[JIT Trusted Handler Call Plan](jit-trusted-handler-call-plan.md). This pulls
forward executable shape and validity guards, native call clobbers,
`x25`-to-`x0` thread adaptation, and callee-owned preservation of `x30` in the
reserved compiled-return frame slot without requiring canonical root
publication or exception handoff.

For JIT-to-JIT calls, move ownership of return-address and return-`CodeObject`
spilling from the caller to the callee. The caller should transport that return
context in the compiled calling convention; the callee materializes canonical
frame-header cells only when its own calls, safepoints, exits, or register
pressure require them. A leaf function can then keep the return context in
registers and return without frame-header stores.

General calls still require managed frame-header construction, stack-passed
arguments, interpreter-state synchronization, safepoint publication, exception
dispatch, and adaptation owned by the runtime call layer. Those boundaries
remain later work rather than being inferred from the restricted trusted-call
path.

### 6. Add F64 Recovery and General Sinking

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

Use iterative Fibonacci as the narrow loop-code baseline. It currently checks
the combined quality of loop-carried allocation, guard elimination, fused
comparison branches, and overflow side exits. Preserve its emitted-code shape
and compare it with both CPython and the barrier-protected C++ reference, but do
not optimize specifically for that one loop.

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
