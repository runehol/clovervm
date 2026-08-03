# JIT Trusted Handler Call Plan

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Active |
| Scope | Guarded calls from compiled AArch64 code to non-raising trusted native handlers |
| Design authority | [JIT Compiler and IR](jit-compiler-and-ir.md), [AArch64 JIT Calling Convention](aarch64-jit-calling-convention.md), [Fast Operator Dispatch](fast-operator-dispatch.md), and [Function Specialization](function-specialization.md) |

## Objective

Compile a monomorphic operator or special-method inline-cache entry to its
already-selected trusted native handler. This is the first native call from
generated code, not the first general Python call.

The initial JIT-callable contract is deliberately narrow:

```text
NoCallPython
NoSafepoint
NoRaise
```

The handler may still mutate objects and have Python-visible effects. The call
therefore retains the conservative trusted-handler `MayEffects` envelope and
empty `MustEffects`. The restricted contract controls boundary mechanics; it
does not authorize effect-based reordering.

Applicability failures occur only in guards before the call and use the
ordinary replayable side-exit path. Once the call begins, it must return a
normal tagged `Value`. It cannot return `exception_marker`, enter managed code,
or require canonical frame or root publication.

## End State

A compiled cached binary operation has this shape:

```text
Snapshot at the operator bytecode
guard operand shape keys
guard method-lookup validity cells
%result = TrustedHandlerCall [%guarded_lhs, %guarded_rhs] {
    handler = <registered native target>
}
continue with %result
```

`TrustedHandlerCall` represents the selected semantic target. It does not
perform method lookup, reflected-operator selection, argument adaptation, or
inline-cache miss handling.

On AArch64, a binary call lowers to:

```text
x1 = operand 0
x2 = operand 1
x0 = x25                         // ThreadState *
bl handler
                                  // tagged result in x0
```

Unary and ternary handlers use `x1`, or `x1` through `x3`, respectively.

## Slice 1: Executable Cache Guards

Complete the existing Core guard path before introducing calls:

1. Add Machine side-exit forms for the `ShapeGuard` family and
   `ValidityCellGuard`.
2. Lower them through the existing Snapshot-to-side-exit-region machinery.
3. Emit an AArch64 pointer check followed by the object-shape comparison for
   `PointerAndShapeGuard`; a non-pointer must exit before any shape load.
   Tagged-value fact simplification may weaken it to `ShapeOnlyGuard` when the
   input is already proven to be a pointer.
4. Emit AArch64 validity-state comparison for `ValidityCellGuard`.
5. Preserve their forwarding definitions so successful guards continue to
   narrow and replace the guarded value in bytecode state.

All guards consume the same pre-operation Snapshot. Any failed guard resumes
at the original bytecode without having performed the operation.

Verification should cover one object shape, one invalidated validity cell, and
successful forwarding into a later use. Tests should verify observable
lowering and execution rather than duplicating instruction accessors.

## Slice 2: Trusted Handler Call Representation

Add one Core-and-Machine instruction:

```text
TrustedHandlerCall {
    arguments: ProgramValueRef[]
    handler: TrustedHandlerTarget
}
    -> ProgramValue(TaggedValue)
```

The argument count is initially one through three and determines the trusted
handler ABI. The handler is a non-GC native code address stored in the
compilation-owned function-pointer attribute pool; the instruction carries its
32-bit pool index while its typed accessor exposes `TrustedHandlerTarget`.
There is no callable operand, Snapshot operand, interpreter return PC, or
exceptional successor.

The CFG verifier checks that the argument count is supported and that the
target is non-null. JIT eligibility is established before constructing the
instruction; the IR does not copy runtime registration metadata into every
call.

Add explicit VM-owned registration for the initial eligible handler set. The
registry is populated during builtin setup and queried by handler pointer plus
arity during compilation. Unregistered handlers continue to lower to
`ResumeInInterpreter`. Do not enlarge `OperatorInlineCache` for JIT-only
metadata.

This slice may initially construct the instruction only in focused CFG tests;
frontend selection lands after the backend path is executable.

## Slice 3: AArch64 Call Constraints And Return Preservation

Describe the platform ABI directly in allocation constraints. For a binary
handler:

```text
operand 0 -> early fixed x1
operand 1 -> early fixed x2
result    -> late fixed x0
```

The emitter copies fixed JIT context register `x25` to native argument register
`x0` after allocator-created argument transfers. The instruction reserves the
native caller-saved allocation registers as clobbers. The fixed result owns
`x0`; it is not also represented as an undefined clobber. Existing allocation
machinery moves live-across values into callee-saved registers. Until ordinary
spilling is available, unsatisfied pressure remains a clean compilation
failure.

A graph containing `TrustedHandlerCall` is non-leaf. Before emitting blocks,
detect that property once and store its incoming hardware return continuation:

```text
str x30, [x21, FrameHeaderCompiledReturnPcOffset * sizeof(Value)]
```

Every normal `Return` and `BareReturn` in that graph reloads `x30` from the same
managed-frame header slot before `ret`. Leaf graphs remain unchanged. Do not
shrink-wrap this store or its reloads in the initial implementation.

Snapshots already carry the compiled-return header position. Its entry program
value is assigned the same canonical `fp[1]` location, so the prologue store
makes that location the authoritative source subsequently observed by recovery.
Allocation and transition verification must confirm that no stale alternate
value is materialized over the stored continuation on a side exit after a call.

Side-exit blocks require no matching epilogue. They do not modify the native
stack, and the current side-exit thunk resumes interpreter dispatch rather than
returning through the compiled function's `x30`. The durable continuation
remains in the frame header for later cross-engine return handling.

Emit the call with the existing AArch64 call relaxation so both near and far
native targets are supported. Use backend scratch registers for far-call
materialization.

Verification should include:

- a leaf graph retaining its current prologue-free code shape;
- a call graph storing and reloading `x30` through `fp[1]`;
- a value live across the call being assigned away from caller-saved clobbers;
- unary, binary, and ternary argument placement;
- a far handler address using the existing call relaxation.

## Slice 4: Operator Frontend Integration

Extend `CoreBytecodeTranslator::lower_non_fastpathed_operator()` for one
ordinary cached trusted-handler case:

1. Read the `OperatorInlineCache` and determine the opcode's trusted-handler
   arity and semantic operand order.
2. Query VM registration for the cached handler and arity.
3. If it is not eligible, emit the existing unsupported interpreter exit.
4. Emit one pre-operation Snapshot.
5. Emit the cached shape and validity guards in cache-match order through one
   frontend helper for an IC `ShapeKey`: an inline key becomes an exact
   inline-tag guard, an object key becomes an exact
   `PointerAndShapeGuard`, and a corresponding non-null lookup validity cell
   adds a `ValidityCellGuard`.
6. Emit `TrustedHandlerCall` with the guarded semantic arguments.
7. Write its normal tagged result into bytecode state.

Start with a non-immediate binary opcode whose handler is non-raising and does
not allocate through a safepoint. A float comparison is a useful end-to-end
candidate: it exercises two object-shape guards, native argument shuffling, a
tagged boolean result, and the non-leaf return path without requiring F64 IR.

After that fixture works, reuse the same path for eligible unary and ternary
operator caches and for the interpreter's cached trusted special-method call.
Immediate bytecode forms and generalized function adaptation are separate
frontend extensions.

## Completion Criteria

The initial plan is complete when:

- an actual warmed inline cache selects a registered trusted handler;
- matching inputs execute the handler and return the correct tagged result;
- a shape or validity mismatch exits before the handler and replays the
  original bytecode correctly;
- native caller-saved clobbers do not corrupt live compiled values;
- `x30` is preserved through `fp[1]` without changing native `sp`;
- leaf generated functions retain their current code shape;
- unsupported, raising, allocating-at-safepoint, and Python-calling handlers
  remain interpreter paths.

## Deferred Work

- exception-marker results and compiled exception-table dispatch;
- safepoint-capable handlers and canonical root publication;
- handlers that re-enter Python;
- precise per-handler effect analysis;
- general Python call adaptation and JIT-to-JIT calls;
- generalized function-specialization caches and semantic identities;
- shrink-wrapped return-continuation stores;
- recovery after a committed native call.
