# JIT Near-Term Implementation Roadmap

| Field | Value |
|---|---|
| Document type | Implementation roadmap |
| Status | Active |
| Scope | Prioritized work that turns the current executable AArch64 JIT slice into a broader and runtime-complete compiler |
| Design authority | [JIT Compiler and IR](jit-compiler-and-ir.md), [JIT Register Allocation](jit-register-allocation.md), [JIT Side-Exit Lowering](jit-side-exit-lowering.md), and [JIT Transition Programs](jit-transition-program.md) |
| Validated against | `6ce461e` (2026-07-29) |

This roadmap records implementation order rather than adding architecture. The
owning design documents remain authoritative for IR, allocation, recovery,
calling conventions, and publication. Exact opcode coverage remains an
implementation fact; the slices below identify semantic families and the
infrastructure they exercise rather than maintaining an opcode ledger.

## Current Vertical Boundary

The direct bytecode-to-Core path now produces executable AArch64 code for a
small but meaningful language subset:

- constants, value movement, identity tests, branches, joins, and returns;
- guarded SMI addition with overflow side exits;
- Core optimization through global dead-code elimination;
- Core-to-Machine side-exit lowering with late executable bindings;
- whole-CFG register allocation with bundle merging, eviction, splitting, and
  allocation-created edge-transfer blocks;
- multi-block AArch64 layout, fallthrough removal, branch relaxation, cold
  side-exit blocks, constant-pool publication, and transition-program
  references.

This is no longer a single-block or fixed-register bring-up backend. The
allocator and emitter can support broader lowering without being replaced.

The runtime path is not complete. Successful paths can be executed directly in
tests, but guard failure cannot safely return to the interpreter until the
entry/exit and side-exit thunks are implemented. Ordinary allocation pressure
also cannot spill into compiler-owned slots, and managed calls do not yet model
fixed arguments, results, or clobbers.

## Ordering Principles

Near-term work follows four rules:

1. Remove obvious multiplicative waste before adding many more exit sites.
2. Extend allocator contracts when the desired code shape depends on location
   choice; do not repair poor allocation with emitter peepholes.
3. Widen through operations that reuse the existing Snapshot, guard, side-exit,
   and overflow machinery before taking on calls or allocation.
4. Do not widen indefinitely while failed speculation remains non-executable.
   A short happy-path expansion phase is useful, but runtime transition closure
   remains a required milestone.

## Implementation Sequence

### 1. Intern Transition Programs and Merge Equivalent Cold Stubs

Generate each pending side exit far enough to obtain its finalized
`TransitionInstruction` sequence, then group byte-identical programs. Program
identity must include physical input locations; a shared Snapshot or
`SideExitId` is not sufficient because register allocation may give two exits
different executable bindings.

For each equivalent group:

- publish one untagged transition-program payload;
- resolve every owning side-exit label at one cold-block boundary;
- emit one address-materialization and thunk branch sequence.

The current guarded binary-add example produces three identical 144-byte
snapshot programs and three equivalent cold stubs. Interning therefore removes
two complete programs and two stubs without changing hot code or IR identity.

Raw untagged constant-pool interning may live in the generic machine-code
emitter, while cold-stub grouping remains backend CFG-emission policy.

### 2. Add Same-As-Input Allocation Constraints

Implement the accepted allocator relation between an instruction result and
one input location, including conflict handling and required fixup connectors.
Apply it first to value-refining guards.

A successful guard produces the same runtime value as its input. Its refined
SSA result should normally remain in that input register instead of generating
a post-check move. This belongs in allocation because the input may remain live,
the result may have other constraints, and later targets need the same
contract.

Explicit-copy affinities and broader copy coalescing may follow, guided by
measurements. They are not a substitute for the required same-as-input
relation.

### 3. Widen SMI Arithmetic and Bitwise Lowering

Extend the direct Core translator and Machine lowering through operation
families that reuse existing inline guards and side-exit state:

- overflow-checked subtraction, including compact-immediate bytecode forms;
- SMI-only bitwise operations whose successful action is total;
- backend folding of encodable tagged SMI constants into immediate forms.

Boolean-as-integer handling must remain explicit. Python's bool/bool and
mixed bool/int operators can differ in result type, so `SMIOrBoolean` guards
must not replace SMI guards until the lowering represents those semantics.

Multiplication, shifts, division, modulo, and exponentiation remain later
slices because their tagged arithmetic, failure cases, or result growth require
additional lowering.

### 4. Add Ordinary Spill Storage

Provide allocator-owned spill slots, split pressured bundles through those
slots, trim register-free regions, and materialize the resulting transfers.
Preserve the existing clean compilation failure for pressure that cannot yet be
handled while the spill implementation is staged.

This precedes substantially wider expression and call coverage. Without it,
adding otherwise-correct lowering merely moves realistic functions toward
avoidable allocation failure.

The symbolic allocation checker and adversarial pressure tests in
[JIT Register Allocation Open Work](jit-register-allocation-progress.md)
should grow with this slice.

### 5. Add SMI Comparisons and Comparison-Branch Fusion

First add ordinary comparison results so Python booleans remain available when
the program consumes them as values. Then recognize the single-use pattern:

```text
comparison -> ConditionalBranch
```

and lower it to one Machine compare-and-branch instruction. AArch64 can then
emit `cmp` followed immediately by `b.cond` without introducing flags as an
allocated IR value. If the Python boolean has another use, retain ordinary
materialization and tagged truthiness testing.

Identity tests use the same fusion shape and provide an existing executable
test case.

### 6. Close the Runtime Transition Loop

Implement the target-specific entry/exit thunk and side-exit register-save
thunk described by the
[AArch64 JIT Calling Convention](aarch64-jit-calling-convention.md):

- install and restore fixed `x19` thread state;
- switch between host/interpreter and managed Clover stack disciplines;
- save the compiled register image and transition-program address;
- provide thread-owned transition execution storage;
- publish recovered canonical interpreter state and resume dispatch.

This milestone makes guard failure, arithmetic overflow, and unsupported
bytecode exits executable rather than merely encoded. Broader speculative
lowering should not outrun it by more than the preceding focused slices.

### 7. Add Shape-Guarded Known-Field Access

Introduce Machine side-exit forms and AArch64 emission for shape and validity
guards, followed by known-offset field loads. This is the first high-leverage
object-oriented slice and should reuse the completed runtime exit path.

Guard commoning and motion may begin here, once repeated shape checks exist in
real generated programs. Keep shape and validity checks independently
optimizable as required by their separate Core instructions.

### 8. Approach Calls, F64, and General Sinking

Calls require fixed argument and result locations, clobber validation, x19-to-C
ABI adaptation, call-boundary interpreter-state synchronization, and safepoint
policy. They should not be introduced as emitter-only sequences.

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

A small representative compilation corpus is preferable to a large set of
tests that merely restate instruction construction. Execution tests should
cover semantic branches and exits; structural tests should pin only valuable
contracts such as fused branches, eliminated transfers, shared cold exits, and
calling conventions.

Revisit this sequence when measurements show a different dominant cost, when a
slice exposes a missing architectural contract, or when the runtime transition
work changes the required ordering. Completed detail belongs in the owning
design or implementation ledger rather than accumulating indefinitely here.
