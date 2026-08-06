# JIT Near-Term Implementation Roadmap

| Field | Value |
|---|---|
| Document type | Implementation roadmap |
| Status | Active |
| Scope | Prioritized work that turns the current executable AArch64 JIT slice into a broader and runtime-complete compiler |
| Design authority | [JIT Compiler and IR](jit-compiler-and-ir.md), [JIT Register Allocation](jit-register-allocation.md), [JIT Side-Exit Lowering](jit-side-exit-lowering.md), and [JIT Transition Programs](jit-transition-program.md) |
| Validated against | `f2fe20d8` (2026-08-07) |

This roadmap records implementation order rather than adding architecture. The
owning design documents remain authoritative for IR, allocation, recovery,
calling conventions, and publication. Exact opcode coverage remains an
implementation fact; the slices below identify semantic families and the
infrastructure they exercise rather than maintaining an opcode ledger.

## Ordering Principles

Near-term work follows four rules:

1. Keep safepoint and recovery correctness current as compiled execution
   becomes longer-lived.
2. Keep pressure handling current as expression, object, and call coverage
   widens.
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

### 2. Unbox Float Arithmetic

Use Mandelbrot as the first end-to-end unboxing target. Implement the float
pipeline in this order:

1. Recognize trusted-handler calls from their registered semantics and guarded
   operand shape keys.
2. Replace recognized calls with explicit `UnboxF64`, F64 operation, and
   `BoxF64` instructions. `BoxF64` produces an exact builtin-float shape fact.
3. Propagate exact shapes for immutable types such as float, then eliminate
   redundant shape guards. In particular, the shape guard immediately after a
   `BoxF64` must be eliminated using the shape fact produced by the box itself.
4. Fold `UnboxF64(BoxF64(value))` to `value` once the intervening guards are
   gone.
5. Carry unboxed F64 values through loop parameters so loop bodies do not box
   values merely to feed the next iteration.
6. Sink remaining boxes into side exits and interpreter-visible returns.

This requires F64 allocation constraints and AArch64 emission, SIMD stack
transfers, boxing allocation, and recovery computation. Boxing must remain
where object identity can become observable. Arithmetic temporaries that escape
only through recovery or return paths should stay unboxed on the main path.
Guards themselves remain on the executable path because a transition program
cannot recursively side exit.

The acceptance shape for Mandelbrot is an inner loop containing F64 arithmetic
and comparisons, integer loop control, and branches, without per-iteration
float allocation or trusted-handler calls.

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

### 4. Add Known-Field Access

Add known-offset field loads behind the existing executable shape and validity
guards. This is the first high-leverage object-oriented slice and should reuse
the established runtime exit path.

Guard commoning and motion may begin here, once repeated shape checks exist in
real generated programs. Keep shape and validity checks independently
optimizable as required by their separate Core instructions.

### 5. Broaden Compiled Calls

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
