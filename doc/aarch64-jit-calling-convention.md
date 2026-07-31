# AArch64 JIT Calling Convention

| Field | Value |
|---|---|
| Document type | Architecture contract |
| Status | Accepted |
| Implementation | Partial; interpreter-aligned fixed context registers, managed-frame-relative stack access and return, result constraints, canonical incoming stack locations, allocation-order register sets, per-class scratch registers, emitted side-exit transition references, the unhandled-side-exit diagnostic target, tail-called interpreter/JIT entry thunks, and interpreter call integration are implemented; stack-passed arguments, managed calls, and the side-exit thunk remain |
| Scope | AArch64 compiled managed argument transport, call adaptation, cross-engine entry, return, and safepoint placement |
| Owning layers | Call-site lowering owns guarded Python adaptation; the AArch64 backend owns argument and result locations; transition adapters own cross-engine reshuffling; the generic allocator and materializer implement the resulting fixed-location constraints and transfers |
| Builds on | [CloverVM Function Calling Convention](function-calling-convention.md) |

This document defines the initial calling convention between compiled
AArch64 Python functions. It changes physical argument transport without
changing CloverVM's canonical managed frame layout, Python call semantics, or
interpreter-visible parameter homes.

## Boundary Representation

All Python-visible parameters use `TaggedValue` at a non-inlined function
boundary:

```text
managed frame pointer -> x21
interpreter PC       -> x22
owning CodeObject *  -> x24
active ThreadState * -> x25
native stack pointer  -> sp
native frame pointer  -> x29
parameter 0..7       -> x0..x7
parameter 8..N       -> canonical parameter cells in the argument window
result               -> x0
```

`x25` is the fixed Clover JIT thread register. The active `ThreadState *` is
reserved machine context alongside the managed frame pointer in `x21`; it is
not a Python argument, Core SSA value, bytecode-state position, Snapshot value,
allocator member, or backend scratch register. Every compiled function
preserves it simply by never writing it. Cross-engine entry installs it and
ordinary platform-ABI calls preserve it.

`x21` is the fixed Clover JIT managed-frame register. It is also reserved
machine context rather than an allocated value. Generated frame state, spills,
and outgoing managed argument windows use `x21`-relative addressing. Native
`sp` and `x29` retain their platform ABI meanings, and generated code never
places managed values on the native stack. Ordinary generated return restores
the caller's `x21` from
the managed frame header before returning.

`x22` and `x24` hold the interpreter PC and owning `CodeObject *` that become
authoritative at interpreter boundaries. Together, `x21`, `x22`, `x24`, and
`x25` match the registers Clang assigns to the corresponding interpreter state
under the `preserve_none` dispatch convention. They are callee-saved under the
generic AAPCS64 and Apple arm64 conventions, so ordinary C and C++ helpers
preserve the active JIT context automatically.

The initial JIT relies on inlining to eliminate boxing. It does not introduce
representation-specialized entry signatures. A later convention may add such
entries explicitly; arity alone describes the proposed initial boundary.

Native Clover helpers such as `Value handler(ThreadState *, Value, ...)` retain
their platform-ABI signatures. JIT-to-native call preparation moves `x25` into
`x0` and shifts helper arguments into `x1` onward. This loses the old accidental
register-prefix compatibility in exchange for keeping persistent VM context
out of the primary argument, result, and temporary register.

## Canonical Argument Window

The caller reserves the target's complete ABI-padded canonical parameter
window, including cells for parameters transported in registers. Canonical
cells zero through seven therefore exist even when they are not current at the
call transition. Parameters beyond seven are written directly to their
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
caller argument 0..7  -> Use FixedLocation(x0..x7)
caller argument 8..N  -> Use FixedLocation(
                              OutgoingCallArgument(caller_frame_offset))

callee parameter 0..7 -> Def FixedLocation(x0..x7)
callee parameter 8..N -> Def FixedLocation(
                              IncomingParameter(callee_frame_offset))

return result          -> FixedLocation(x0)
```

`x25` does not appear as an allocation constraint because it is not an
allocated value. Managed calls inherit the active thread context. Native-helper
lowering adds physical source `x25` to its argument shuffle when producing the
platform-ABI `x0` thread argument.

Caller-saved registers are ordinary call clobbers. The generic allocator may
split surrounding live ranges and the generic materializer schedules the
required register shuffles and overflow stores. Python adaptation semantics do
not enter the allocator.

## Cross-Engine Calls

Cross-engine call entry uses this internal AArch64 transition state:

```text
x0..x7 = first eight adapted tagged parameters
x21    = committed callee managed frame pointer
x22    = callee interpreter entry PC
x23    = compiled entry address during interpreter entry
x24    = target CodeObject *
x25    = active ThreadState *
sp     = native platform stack pointer
x29    = native platform frame pointer
```

Overflow parameters are already current in the callee's canonical parameter
cells. The target `CodeObject` is the authoritative source of logical arity,
padded arity, bytecode entry, and frame metadata. The transition does not carry
arity as a separate value.

When JIT code is published, a target helper uses that authoritative arity to
select one process-wide interpreter-entry thunk from the padded register-arity
family `0`, `2`, `4`, `6`, and `8`. `JitCodeObject` caches only the selected
thunk address. Logical odd and even arities therefore share the same
pair-loading thunk. Every logical arity greater than eight selects the `8`
thunk because additional arguments already occupy canonical stack cells. Entry
does not inspect arity or compute parameter offsets at runtime.

The JIT-to-interpreter adapter:

```text
store the first min(arity, 8) registers from x0..x7 into canonical cells
retain x24 as the current CodeObject
retain x22 as the interpreter pc
publish the managed frame frontier
enter interpreter dispatch
```

The interpreter-to-JIT adapter is symmetric:

```text
retain the active ThreadState * in x25
retain the committed managed frame pointer in x21
retain the interpreter PC in x22 and target CodeObject * in x24
load the selected fixed pairs of parameters into x0..x7
leave overflow parameters in their canonical cells
leave sp and x29 in native platform state
enter compiled code
```

Call adaptation remains arity-dependent in both engine directions. The
interpreter-to-JIT direction resolves that dependency once, when publication
selects the cached pair-loading thunk. A compiled call site may emit its known
register-to-canonical stores directly when doing so produces better code.

`x23` is the interpreter dispatch register. Immediately before tail-calling an
interpreter-to-JIT thunk, the call handler repurposes its `void *dispatch`
argument to carry the compiled entry address. The thunk consumes it before
loading Python arguments. Interpreter reentry reconstructs the active normal
or tracing dispatch table from `ThreadState`; dispatch is not persistent JIT
context.

## Entry And Exit Thunks

Cross-engine transitions use a small target-specific family of interpreter/JIT
entry-exit thunks. Each thunk installs the fixed generated-code context while
retaining the native stack discipline, then loads a fixed number of parameter
pairs with `ldp`. Compiled code is entered with a real AArch64 call so ordinary
successful returns use the hardware return-address predictor:

```text
interpreter call handler
    commit x21, x22, x24, and x25 for the callee
    replace x23 dispatch with the compiled entry address
    tail-call the arity-selected entry-exit thunk

interpreter-to-JIT entry-exit thunk
    save x30 on the native stack
    move x23 compiled entry to x17
    load the thunk's fixed parameter pairs into x0..x7
    blr compiled_entry

jit_return_label
    move the x0 JIT result to the x20 interpreter accumulator
    restore x30
    tail-branch to interpreter reentry
```

The standalone native execution harness calls a separate thunk through the
same six-argument `preserve_none` signature. The native compiler therefore owns
preservation of any host register state live across that call. The standalone
thunk receives the same x20-x25 entry state, saves only x30 across its
`blr compiled_entry`, and returns the x0 result directly to its native caller.
It is test and bring-up infrastructure, not the production interpreter entry
path.

A normal compiled Python return places the tagged result in `x0`, restores the
caller's interpreter state, and executes `ret` through the link register
established by the entry thunk's
`blr compiled_entry`. The hot call/return pair is therefore balanced:

```text
compiled normal return
    x0 = tagged result
    x22 = decode interpreter PC from [x21 + return_pc_offset]
    x24 = decode CodeObject * from [x21 + return_code_object_offset]
    x21 = decode managed frame pointer from [x21 + previous_fp_offset]
    ret
```

Generated Python functions are not public C ABI entry points. The entry-exit
thunk is the only ordinary caller of compiled code from native or hand-written
interpreter code. Compiled functions may call other compiled functions and
platform-ABI native helpers under this convention. The thunk must be able to
distinguish an ordinary compiled result from any later explicit JIT-exit state,
but successful returns stay on the single-result path above.

## Side-Exit Register-Save Thunk

A compiled side exit branches to a target-specific side-exit register-save
thunk. That thunk captures enough compiled machine state for recovery, calls
the portable transition executor, and produces the same JIT exit state as an
ordinary compiled return. It then returns to the entry-exit thunk's existing
JIT return label:

```text
compiled guard failure
    x16 = address of side-exit transition program
    x17 = available branch-materialization scratch
    b side_exit_register_save_thunk

side_exit_register_save_thunk
    save the fixed compiled register image
    save x25 as the authoritative active ThreadState *
    save x21 as the authoritative managed frame pointer
    save the transition-program address from x16
    call the portable transition executor
    place the recovered accumulator in x0
    place recovered fp, pc, CodeObject *, and ThreadState * in x21/x22/x24/x25
    restore the incoming JIT link register
    ret
```

The outstanding return-stack entry from the entry thunk's `blr compiled_entry`
predicts exactly the entry thunk's `jit_return_label`. The side-exit return
therefore balances the original call just like an ordinary compiled return.
The entry-exit thunk moves x0 to the interpreter accumulator and tail-branches
to interpreter reentry, which reconstructs dispatch from `ThreadState`.

The native stack is already valid at a side exit. After the thunk captures all
caller-saved generated state and publishes the managed frame and roots, it may
call portable C++ transition code using the platform ABI. The initial runtime
does not yet use that path: published code targets a non-returning C++ wrapper
that reports an unhandled side exit and aborts until register capture, recovery,
and interpreted return through the compiled frame are implemented together.

The emitted side-exit sequence and transition-program address convention are
implemented; the thunk itself is not. Fixed `x25` gives the thunk immediate
access to thread-owned transition state without transition metadata or
allocator cooperation, while fixed `x21` identifies the active managed frame.
The remaining thunk design must settle register-save storage and return the
same x0/x21/x22/x24/x25 JIT exit state to the entry-exit thunk as an ordinary
compiled return.

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
- `x25` contains the active `ThreadState *` throughout compiled execution. It
  is reserved machine context, not an allocator-visible value.
- `x21` contains the current managed frame pointer throughout compiled
  execution. It is reserved machine context, not an allocator-visible value.
- `sp` and `x29` retain their native platform meanings throughout generated
  execution; no managed values, spills, or outgoing managed argument windows
  are placed on the native stack.
- Compiled arguments zero through seven use `x0` through `x7`; ordinary results
  use `x0`.
- `x22` and `x24` carry the interpreter PC and owning `CodeObject *` at
  compiled/interpreter boundaries.
- Interpreter-to-JIT entry reuses the interpreter's x21/x22/x24/x25 state
  directly; x23 temporarily carries the compiled entry address.
- Successful adaptation determines the callee arity before argument transport.
- Interpreter-to-JIT publication selects a `0`, `2`, `4`, `6`, or `8`
  pair-loading entry thunk from the target `CodeObject` arity and caches its
  address in `JitCodeObject`; entry does not decode arity.
- Return transitions do not depend on arity.
- Overflow arguments already in canonical cells are not copied merely because
  execution changes engine.
- The interpreter/JIT entry-exit thunk enters compiled code with a real call;
  ordinary successful compiled returns restore x21/x22/x24 and use
  `ret` back to that thunk.
- Generated Python functions are not directly callable through the public C
  ABI; native code enters them through the adapter.
- Side exits branch to a side-exit register-save thunk, capture compiled state,
  execute recovery, and return JIT exit state to the entry-exit thunk's normal
  return label.
- A side-exit thunk may call C++ only after capturing generated register state
  and publishing the managed frame and roots required by the native call.
- Side exits before call commitment reconstruct the original bytecode call
  state rather than the adapted JIT argument vector.
- Safepoints discover every live root regardless of whether its canonical home
  is current.
