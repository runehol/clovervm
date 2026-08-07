# JIT Trusted Handler Call Plan

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Active |
| Implementation | Cache guards, call IR, handler metadata and registry, typed resolver installation, float handler declarations, unary/binary/ternary operator frontend integration, AArch64 call allocation constraints and emission, fixed operand-copy materialization, concrete call-local managed-frame spills, and explicit link-register preservation are implemented |
| Scope | Guarded calls from compiled AArch64 code to non-raising trusted native handlers |
| Design authority | [JIT Compiler and IR](jit-compiler-and-ir.md), [AArch64 JIT Calling Convention](aarch64-jit-calling-convention.md), [Trusted Handler Declarations](trusted-handler-declarations.md), [Fast Operator Dispatch](fast-operator-dispatch.md), and [Function Specialization](function-specialization.md) |

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
therefore retains the conservative trusted-handler `MayEffects` envelope. Its
`MustEffects` includes `MachineState`, because every native call necessarily
changes architectural call state. The restricted contract controls boundary
mechanics; it does not authorize effect-based reordering.

Applicability failures occur only in guards before the call and use the
ordinary replayable side-exit path. Once the call begins, it must return a
normal tagged `Value`. It cannot return `exception_marker`, enter managed code,
or require canonical frame or root publication.

Allocation is not itself disqualifying. Registered effects deliberately treat
`Allocate`, `Safepoint`, `Raise`, and `CallPython` as independent facts. A
handler that allocates without safepointing, raising, or calling Python remains
eligible for this initial boundary.

## Implemented Foundation

The following pieces are already present and are no longer staging work:

- `PointerAndShapeGuard`, `ShapeOnlyGuard`, and `ValidityCellGuard` lower to
  executable Machine side-exit forms on AArch64;
- tagged-value facts weaken pointer-and-shape guards when pointerness is known;
- `TrustedHandlerCall` is a Core-and-Machine instruction with one through three
  tagged arguments and a pooled `TrustedHandlerTarget` attribute;
- CFG verification rejects unsupported call arities;
- the VM registry is keyed by erased native target and returns typed arity,
  effects, and semantic identity as metadata;
- typed handler definitions and resolver installation register every handler
  before publishing the resolver function pointer;
- every float unary and binary trusted handler is registered through this path.

The float conversion also establishes the intended division of authority. An
inline cache stores the concrete handler target selected by runtime resolution
and the shape and validity facts that made it applicable. Registry metadata
describes that exact target. The JIT does not rerun the resolver, infer the
target from the bytecode opcode, or copy effects and semantics into the IR.

Type-adapted dunder methods may generate several concrete trusted targets from
one semantic C++ operation. Their shape keys determine the operand adaptation;
their registered semantic identity determines the operation. The initial
opaque native call needs only the target and effects. A later semantic expansion
uses both sources of information.

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

`TrustedHandlerCall` represents the selected concrete target. It does not
perform method lookup, reflected-operator selection, operand reordering,
selection among type adaptations, or inline-cache miss handling. A generated
type-adapted handler may itself convert its already-guarded `Value` arguments to
the natural C++ types consumed by its shared semantic operation.

On AArch64, a binary call lowers to:

```text
x1 = operand 0
x2 = operand 1
x0 = x25                         // ThreadState *
bl handler
                                  // tagged result in x0
```

Unary and ternary handlers use `x1`, or `x1` through `x3`, respectively.

## Implemented Slice 1: AArch64 Calls And Return Preservation

Describe the platform ABI directly in allocation constraints. For a binary
handler:

```text
operand 0 -> early FixedOperandCopy(x1)
operand 1 -> early FixedOperandCopy(x2)
result    -> late FixedLocation(x0)
```

The emitter copies fixed JIT context register `x25` to native argument register
`x0` after allocator-created argument transfers. The instruction reserves the
native caller-saved allocation registers as clobbers. The fixed result owns
`x0`; it is not also represented as an undefined clobber.

`FixedOperandCopy` differs from the existing `FixedLocation`: it does not force the
source bundle into `x1` through `x3`. It records an operand-local copy from the
source's authoritative location to the ABI register and rewrites only that call
operand. Later uses continue to refer to the source bundle.

Ordinary pressure splitting cannot preserve an argument that is both needed in
`x1` through `x3` at the instruction's Early point and live after the call's
Late clobber. The bootstrap implementation uses allocator-owned managed-frame
spill slots instead. It trims a maximal spill-safe carrier interval around the
call with two ordinary bundle splits. The resulting connectors store into the
carrier and reload from it. The `FixedOperandCopy` copies the argument from the carrier
to its ABI register without changing the carrier's authoritative value.

Trimming respects every occurrence's `minimum_coverage`; it never splits
through an instruction-local lifetime. Each interval initially becomes a
dedicated spill-candidate bundle rather than joining a shared per-value spill
bundle. The allocator gives that candidate one final register probe and then,
if necessary, assigns an abstract `SpillSlotId`. A deterministic first-fit pass
reuses slots among non-overlapping candidates without requiring optimal slot
coloring.

These initial spills are deliberately call-local. Their ranges begin before
the argument shuffle and end immediately after the non-raising,
non-safepointing call. No safepoint, side exit, exception path, or Python
reentry may observe the spill interval. The slots may therefore hold any
machine representation without being interpreter-visible roots. Calls with
non-overlapping spill intervals may reuse slots. The finalized
`JitCodeObject` records the additional managed-frame extent so the storage
cannot overlap another managed frame.

`AnyLocation` does not grant this temporary-spill permission. In particular,
side-exit arguments remain observable and cannot be moved into an untracked
spill slot. A bootstrap carrier may contain no def, forwarding def, Snapshot
use, side-exit argument, safepoint, Python call, or Python reentry boundary; its
only value use may be one trusted-handler `FixedOperandCopy`. A value that is merely
live through the call may use the same carrier shape without a use at the call.

Materialization first executes ordinary authoritative transfers, including the
spill store or reload, and then resolves all fixed operand copies at the instruction as
one parallel assignment. The generated fixed-operand-copy values are assigned to the
ABI registers and replace only their corresponding operands. They do not
update the allocator's current-value mapping for the source bundle.

This slice does not search Snapshot uses for compatible canonical frame homes
and does not implement general spilling. Values outside this call-local shape
remain subject to the existing register-only allocator and clean compilation
failure.

A graph containing `TrustedHandlerCall` is non-leaf. After side-exit lowering,
an AArch64 Machine-IR pass makes return preservation explicit by inserting one
`SaveLinkRegisterToFrame` at function entry and one
`RestoreLinkRegisterFromFrame` immediately before every normal `Return` or
`BareReturn`. Both instructions have exact `MachineState` effects, as does the
must-effect bound of `TrustedHandlerCall`, so later instruction movement cannot
separate their ordering accidentally.

The save stores the incoming hardware return continuation:

```text
str x30, [x21, FrameHeaderCompiledReturnPcOffset * sizeof(Value)]
```

The restore reloads `x30` from the same managed-frame header slot before `ret`.
Side exits do not restore it because they do not execute a native return. Leaf
graphs remain unchanged. The initial pass does not shrink-wrap the save or its
restores.

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
- a fixed argument live after the call surviving through a call-local managed
  spill slot and fixed operand copy;
- an unrelated caller-saved value live across the call surviving through the
  same call-local spill mechanism without an operand copy;
- non-overlapping calls reusing compatible spill slots;
- unary, binary, and ternary argument placement;
- a far handler address using the existing call relaxation.

This slice is covered by manually constructed Machine graphs. The backend path
is executable before the bytecode frontend selects it.

## Implemented Slice 2: Operator Frontend Integration

`CoreBytecodeTranslator::lower_non_fastpathed_operator()` supports eligible
cached unary, binary, and ternary trusted-handler cases:

1. Derive unary, binary, or ternary arity from the bytecode operator role.
   Require that many explicit semantic source values; immediate binary forms
   remain unsupported until their embedded constant is materialized as a call
   argument.
2. Reject caches with no trusted target. Erase the cached target using that
   expected arity and query the VM registry by target alone.
3. Require the returned metadata arity to equal the expected arity. Reject
   unregistered targets and targets declaring `Safepoint`, `Raise`, or
   `CallPython`; `Allocate` alone remains eligible.
4. If the cache is not eligible, emit the existing unsupported interpreter
   exit.
5. Emit one pre-operation Snapshot.
6. Emit cached shape and validity guards in cache-match order: unary calls
   guard operand zero, while binary and ternary calls guard operands zero and
   one. Ternary operand two is runtime call state rather than a cache key. An
   inline key becomes an exact inline-tag guard, an object key becomes an exact
   `PointerAndShapeGuard`, and a corresponding non-null lookup validity cell
   adds a `ValidityCellGuard`.
7. Emit `TrustedHandlerCall` with the guarded arguments in the same canonical
   operand order used by interpreter cache replay. Reflected behavior is already
   encoded by the concrete cached target; the frontend must not swap operands.
   The post-guard emission helper receives the complete inline cache, including
   its shape keys, and the resolved registry metadata so later semantic
   expansion has both sources of specialization information.
8. Write its normal tagged result into bytecode state.

Float comparison and unary negation provide end-to-end executable cases. They
exercise object shape guards, native argument shuffling, tagged results, the
non-leaf return path, and interpreter replay after a shape-guard miss without
requiring F64 emission. Ternary frontend lowering is structurally covered;
immediate bytecode forms, cached trusted special-method calls, and generalized
function adaptation are separate frontend extensions.

## Remaining Slice 3: Semantic Expansion

The opaque call is the first consumer of registered metadata. The next stage
uses recognized float semantics to replace eligible `TrustedHandlerCall`
instructions with explicit guards, conversions, unboxed operations, and result
boxing. The cache shape keys select the exact type adaptation; registered
semantics select the operation. Neither source is sufficient by itself.

This expansion is not required for the native call boundary. It adds the
required F64 operations as each recognized semantic family becomes executable,
then reuses exact immutable-shape facts, box/unbox simplification, snapshot
recovery, and dead-code elimination to sink unnecessary boxes into side exits.

Broader builtin migration is also independent. Float already supplies
registered non-raising candidates for the first end-to-end call. Bigint and
other families may adopt typed declarations after their non-uniform adaptation
and `NotImplemented` behavior have been reviewed.

## Completed Opaque-Call Criteria

The initial opaque-call slice now establishes that:

- an actual warmed inline cache selects a registered trusted handler;
- matching inputs execute the handler and return the correct tagged result;
- a shape or validity mismatch exits before the handler and replays the
  original bytecode correctly;
- native caller-saved clobbers do not corrupt live compiled values;
- `x30` is preserved through `fp[1]` without changing native `sp`;
- leaf generated functions retain their current code shape;
- unsupported, raising, safepointing, and Python-calling handlers
  remain interpreter paths.

## Deferred Work

- exception-marker results and compiled exception-table dispatch;
- safepoint-capable handlers and canonical root publication;
- handlers that re-enter Python;
- precise optimizer memory-effect analysis beyond call-boundary capabilities;
- general Python call adaptation and JIT-to-JIT calls;
- generalized function-specialization caches;
- general spilling outside trusted-handler call boundaries;
- canonical-home selection for spill slots using later Snapshot demands;
- safepoint-visible spill root maps and frame-aware stack scanning;
- shrink-wrapped return-continuation stores;
- exceptional recovery after a native call has begun.
