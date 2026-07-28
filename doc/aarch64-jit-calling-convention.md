# Proposed AArch64 JIT Calling Convention

| Field | Value |
|---|---|
| Document type | Proposed architecture contract |
| Status | Proposed |
| Implementation | Partial; hidden `ThreadState *` and the first seven Python entry arguments have fixed register constraints, while result constraints, canonical incoming stack locations, allocation-order register sets, and per-class scratch registers are implemented; stack-passed entry arguments, managed calls, and transition adapters are not implemented |
| Scope | AArch64 compiled managed argument transport, call adaptation, cross-engine entry, return, and safepoint placement |
| Owning layers | Call-site lowering owns guarded Python adaptation; the AArch64 backend owns argument and result locations; transition adapters own cross-engine reshuffling; the generic allocator and materializer implement the resulting fixed-location constraints and transfers |
| Builds on | [CloverVM Function Calling Convention](function-calling-convention.md) |

This document proposes the initial calling convention between compiled
AArch64 Python functions. It changes physical argument transport without
changing CloverVM's canonical managed frame layout, Python call semantics, or
interpreter-visible parameter homes.

## Boundary Representation

All Python-visible parameters use `TaggedValue` at a non-inlined function
boundary:

```text
hidden ThreadState * -> x0
parameter 0..6       -> x1..x7
parameter 7..N       -> canonical parameter cells in the argument window
result               -> x0
```

The thread pointer is compiler state, not Python parameter zero. It does not
contribute to Python arity or the canonical parameter window. It is position
one in `BytecodeStateOrder` and in Snapshots so side exits can recover the
active execution context. It enters Core as a hidden `Pointer` value and may be
moved or spilled by ordinary allocation after its fixed `x0` entry definition.

The initial JIT relies on inlining to eliminate boxing. It does not introduce
representation-specialized entry signatures. A later convention may add such
entries explicitly; arity alone describes the proposed initial boundary.

This register prefix deliberately matches native Clover handlers such as
`Value handler(ThreadState *, Value, ...)`: the thread is in `x0`, the first
seven Python values are in `x1` through `x7`, and the result returns in `x0`.
It is not the platform ABI as a whole. Native overflow arguments use the host
ABI stack, while compiled managed overflow arguments use Clover's canonical
managed argument window.

## Canonical Argument Window

The caller reserves the target's complete ABI-padded canonical parameter
window, including cells for parameters transported in registers. Canonical
cells zero through six therefore exist even when they are not current at the
call transition. Parameters beyond six are written directly to their
ordinary canonical outgoing cells.

Moving the managed frame pointer reinterprets overflow cells as the callee's
incoming parameter cells without copying them. Caller-relative
`OutgoingCallArgument` offsets and callee-relative `IncomingParameter` offsets
belong to different frame coordinate systems. Their numeric values need not be
equal. The frame-transition equation derived from the first argument position
and padded callee arity establishes that they refer to the same physical cells.

Canonical homes for register parameters become current only when interpreter
entry, recovery, or root publication requires synchronization.

## Guarded Call Adaptation

Compiled call sites inline the adaptation predicted by their call inline
cache. Guards validate the callable, selected `CodeObject`, signature, and
adaptation assumptions. After all guards pass, optimized adaptation may:

- reorder keyword values;
- insert a bound receiver;
- select default values;
- construct other final callee parameters supported by the lowering.

The final adapted parameter vector, not the original bytecode operand order, is
placed in the JIT calling convention. A guard mismatch takes a pre-call side
exit. Its recovery reshuffler reconstructs the original bytecode call state
expected by the interpreter; it does not materialize the speculative adapted
vector.

## Allocation Contract

The AArch64 backend describes a compiled managed call with ordinary fixed
locations:

```text
hidden caller thread  -> Use FixedLocation(x0)
caller argument 0..6  -> Use FixedLocation(x1..x7)
caller argument 7..N  -> Use FixedLocation(
                              OutgoingCallArgument(caller_frame_offset))

hidden callee thread  -> Def FixedLocation(x0)
callee parameter 0..6 -> Def FixedLocation(x1..x7)
callee parameter 7..N -> Def FixedLocation(
                              IncomingParameter(callee_frame_offset))

return result          -> FixedLocation(x0)
```

Caller-saved registers are ordinary call clobbers. The generic allocator may
split surrounding live ranges and the generic materializer schedules the
required register shuffles and overflow stores. Python adaptation semantics do
not enter the allocator.

## Cross-Engine Calls

Cross-engine call entry uses this internal AArch64 transition state:

```text
x0      = active ThreadState *
x1..x7 = first seven adapted tagged parameters
x16    = target CodeObject *
x29/fp = committed callee managed frame pointer
```

Overflow parameters are already current in the callee's canonical parameter
cells. The target `CodeObject` is the authoritative source of logical arity,
padded arity, bytecode entry, and frame metadata. The transition does not carry
arity as a separate value.

The JIT-to-interpreter adapter:

```text
arity = x16->function_signature.n_parameters
store x1..x[min(arity, 7)] into fp-relative canonical parameter cells
publish x16 as the current CodeObject
set the interpreter pc to x16->code.data()
publish the managed frame frontier
switch to the host stack
enter interpreter dispatch
```

The interpreter-to-JIT adapter is symmetric:

```text
arity = x16->function_signature.n_parameters
place the active ThreadState * in x0
load the first min(arity, 7) parameters into x1..x7
leave overflow parameters in their canonical cells
switch the architectural stack pointer to managed Clover storage
enter compiled code
```

Call adaptation remains arity-dependent in both engine directions, but the
shared transition adapter derives that arity from `x16` rather than requiring
an arity-specialized entry address or a second metadata input. A compiled call
site may still emit its known register-to-canonical stores directly when doing
so produces better code.

`x16` is interprocedural scratch at this controlled Clover boundary. It is an
entry/setup carrier for the target `CodeObject *`, not a durable code-identity
register. It does not remain an allocator-visible live value after compiled
entry and is not a preserved call argument. Generated far-call or transition
sequences must not overwrite it before the entry handoff consumes it; they may
use `x17` for target materialization when `x16` still carries the target
`CodeObject *`. This convention does not rely on `x16` surviving an arbitrary
platform-ABI call or linker veneer.

Compiled code that needs its owning `CodeObject` or `JitCodeObject` after
entry must materialize that identity explicitly from generated-code metadata,
the compiled code object's constant pool, or another backend-defined metadata
source. It must not treat `x16` as a persistent self pointer.

## Entry And Exit Thunks

Cross-engine transitions use a target-specific interpreter/JIT entry-exit
thunk. This thunk owns the durable transition between host/interpreter stack
discipline and compiled Clover stack discipline. Compiled code is entered with
a real AArch64 call so ordinary successful returns use the hardware return
address predictor:

```text
interpreter-to-JIT entry-exit thunk
    save host/interpreter execution state
    install the compiled Clover stack pointer
    place ThreadState * in x0
    place target CodeObject * in x16
    derive arity from the target CodeObject in x16
    place first min(arity, 7) tagged parameters in x1..x7
    place the committed managed frame pointer in x29/fp
    blr compiled_entry

jit_return_label
    restore host/interpreter stack discipline
    consume the JIT return result
    continue in the interpreter or caller path
```

A normal compiled Python return places the tagged result in `x0` and executes
`ret` through the link register established by the entry thunk's
`blr compiled_entry`. The hot call/return pair is therefore balanced:

```text
compiled normal return
    x0 = tagged result
    ret
```

The entry-exit thunk is the only ordinary caller of compiled code from the
hand-written interpreter. It must be able to distinguish an ordinary compiled
result from any later explicit JIT-exit state, but successful returns stay on
the single-result path above.

## Side-Exit Register-Save Thunk

A compiled side exit is not modeled as a normal return to the entry-exit thunk.
It branches to a target-specific side-exit register-save thunk. That thunk
captures enough compiled machine state for recovery and then enters the
interpreter's threaded dispatch path by loading the handler for the selected
re-entry program counter and returning to that handler:

```text
compiled guard failure
    b side_exit_register_save_thunk

side_exit_register_save_thunk
    save the fixed compiled register image
    save compiled fp/sp and the owning side-exit identity
    publish or record the side-exit capture for interpreter recovery
    pc = interpreter return/recovery opcode pc
    ldr xtarget, [dispatch_table, pc]
    ret xtarget
```

This deliberately spends one cold-path return-prediction miss. The outstanding
return-stack entry from the entry thunk's `blr compiled_entry` predicts the
entry thunk's `jit_return_label`, while the actual side-exit target is the
interpreter dispatch handler. Executing `ret xtarget` nevertheless consumes that
stale return-stack entry, so the interpreter continues in its usual
threaded-dispatch regime instead of carrying a polluted return predictor into a
chain of later returns.

The side-exit thunk must not call C++ transition code while still using the
compiled stack as an ordinary platform stack. Portable transition execution, if
needed for a side exit, belongs behind the interpreter re-entry path or another
thunk that has first restored host-stack discipline. The side-exit
register-save thunk owns only target state capture and the cold `ret xtarget`
handoff into interpreter dispatch.

## Returns

Return transitions are arity-independent in both directions. A compiled return
places its one ordinary result in `x0`; an interpreter return carries the same
result in the accumulator. The return continuation recorded in the managed
frame header selects the required direction-specific adapter.

Exception propagation uses the existing managed frame and pending-exception
contracts rather than the ordinary result path.

## Safepoints and Side Exits

Safepoint polling may occur on either stable side of call adaptation. The
chosen point must expose every live managed root and a valid interpreter
handoff, but interpreter and compiled code need not use identical poll
placement.

A convenient initial compiled policy is to poll before guarded adaptation. A
tripped poll can then reuse the pre-call Snapshot and the same recovery
reshuffler used by an IC mismatch. If a later lowering places the poll after
adaptation, it must publish the corresponding committed state precisely.

## Desired Direction: Demand-Driven Frame State

The intended long-term direction is analogous to AArch64 leaf-function frame
elision. Compiled entry should not automatically store the current
`CodeObject`, caller frame pointer, return address, and interpreter return
metadata into canonical frame-header cells merely because those cells exist.
A compiled leaf should be able to retain or derive that state, return directly
through `x30`, and avoid an eager managed-frame prologue when no runtime-visible
frame state is required on its successful path.

Frame-state values remain explicit SSA values and block arguments so ordinary
liveness, allocation, splitting, and spilling can determine how they survive.
A non-leaf function may preserve only the values that cross a nested call, and
preservation should be shrink-wrapped to the calling paths where practical.
Register pressure may spill those values through the ordinary allocator rather
than forcing every invocation to use fixed canonical header slots.

Side exits, interpreter transitions, safepoints that require canonical
publication, and unwind paths may materialize the missing frame header on their
uncommon paths. Return-site and compiled-PC metadata may make some caller state
derivable instead of independently preserved.

This section records a desired design direction, not a completed contract. The
exact division between live SSA state, allocator spills, return-site metadata,
and eagerly materialized header fields remains to be designed together with
compiled unwinding and stack walking.

## Required Invariants

- The complete padded canonical parameter window exists for every managed
  frame even when its register-parameter cells are stale.
- Non-inlined Python boundaries use tagged parameters; inlining is the initial
  mechanism for avoiding boxing.
- Every compiled entry receives the active `ThreadState *` in `x0`; it is state
  position one, but not a Python argument or canonical frame home.
- `x16` may carry the target `CodeObject *` only as entry/setup state; compiled
  code must rematerialize code-object identity when it needs it after entry.
- Successful adaptation determines the callee arity before argument transport.
- Call transitions are specialized by arity in both engine directions.
- Return transitions do not depend on arity.
- Overflow arguments already in canonical cells are not copied merely because
  execution changes engine.
- The interpreter/JIT entry-exit thunk enters compiled code with a real call;
  ordinary successful compiled returns use `ret` back to that thunk.
- Side exits branch to a side-exit register-save thunk, capture compiled state,
  and enter interpreter dispatch with `ldr xtarget, [dispatch_table, pc]` plus
  `ret xtarget` rather than a plain branch.
- The side-exit register-save thunk must not call C++ while using the compiled
  stack as an ordinary platform stack.
- Side exits before call commitment reconstruct the original bytecode call
  state rather than the adapted JIT argument vector.
- Safepoints discover every live root regardless of whether its canonical home
  is current.
