# AArch64 JIT Entry And Exit Transition Plan

| Field | Value |
|---|---|
| Document type | Implementation transition plan |
| Status | Proposed |
| Scope | Interpreter-to-JIT entry and ordinary JIT return on AArch64 |
| Target milestone | Execute a manually published iterative Fibonacci function through an ordinary interpreted call |
| Design authority to update | [CloverVM Function Calling Convention](function-calling-convention.md), [Native/Managed Boundary Contracts](native-managed-boundaries.md), [JIT Compiler and IR](jit-compiler-and-ir.md), [AArch64 JIT Calling Convention](aarch64-jit-calling-convention.md), and [JIT Register Allocation](jit-register-allocation.md) |

This plan brings up the successful interpreter-to-JIT call path without first
solving side exits, generated calls, tiering, or a generated interpreter. It
replaces the proposed bring-up stack switch with a split-stack arrangement:
native execution keeps the architectural stack, while generated code addresses
Clover-managed frames through a dedicated register.

The first implementation slice must update the owning architecture documents.
Until then, this plan records the agreed replacement direction for their
existing `sp`-switching and `x29`-managed-frame wording.

## Transitional Register Contract

```text
sp  = native platform stack pointer
x29 = native platform frame pointer
x19 = active ThreadState *
x20 = current Clover managed frame pointer
x30 = active hardware return address
```

Generated code does not place Python values, JIT spills, managed frame headers,
or outgoing managed argument windows on the native stack. Those remain in
Clover storage and use `x20`-relative addressing. Native frames, native spills,
and platform return state remain on `sp` and are opaque to managed root
scanning.

`x19` and `x20` are excluded from allocation. AArch64 code must obtain both
register numbers from shared target constants rather than spelling local
register numbers. `x29` is not a managed frame register and is unavailable to
ordinary JIT allocation.

This arrangement is compatible with calling native AAPCS64 functions from JIT
code: native callees preserve `x19` and `x20`, and `sp` already satisfies the
platform ABI. A generated Python entry is not itself a public C ABI function.
Native code enters it only through the adapter defined below.

## Managed Frame And Return Contract

The interpreter constructs the ordinary callee frame before entering the
adapter. Its fixed header retains the existing meanings:

```text
managed_fp[0] = previous managed frame pointer
managed_fp[1] = executable compiled return continuation
managed_fp[2] = interpreter return CodeObject
managed_fp[3] = interpreter return pc
```

An ordinary generated return leaves its tagged result in `x0`, restores the
caller's managed frame pointer, and returns through the hardware link register:

```asm
ldr x20, [x20, #FrameHeaderPreviousFpOffset]
ret
```

The caller's allocation constraints already place the result in `x0` before
the return sequence. A native helper preserves the current `x20` through the
platform ABI. A future managed callee restores its caller's `x20` from its own
frame header before returning.

A future non-leaf compiled caller preserves its incoming `x30` in its own
compiled-return cell and installs the post-call continuation in the callee's
compiled-return cell. It may then use `blr` for a balanced managed call and
reload its own incoming return address after the callee restores `x20`.

## Adapter Boundary

The runtime-facing API is typed and target-specific:

```cpp
[[nodiscard]] Value enter_aarch64_jit(
    ThreadState &thread,
    Value *callee_fp,
    CodeObject &code_object,
    JitCodeObject &jit_code);
```

The C++ wrapper derives logical arity from `code_object` and obtains the entry
address from `jit_code`. Arity is ephemeral private-adapter input, not duplicated
persistent metadata and not supplied by the interpreter call site. The initial
compiler and adapter support zero through eight tagged parameters.

The wrapper calls a private AArch64 assembly thunk using a small internal
platform ABI. The assembly thunk:

```text
save host x19, x20, x29, and x30 on the native stack
install ThreadState * in x19
install callee_fp in x20
place CodeObject * in x16 for the controlled entry handoff
store jit_return_label in callee_fp[1]
load p0..p7 from their canonical x20-relative cells as required by arity
blr jit_code.entry()

jit_return_label:
    preserve the tagged x0 result
    restore host x19, x20, x29, and x30
    return to the interpreted call handler
```

The thunk never installs Clover storage in `sp`. It has one process-wide static
implementation rather than one generated copy per `CodeCache`.

## Interpreter Call Path

The first runtime integration applies only to a fixed-arity cached positional
call whose selected `CodeObject` already has manually published JIT code.
Python call adaptation and cache validation remain interpreter-owned.

```text
validate the existing fixed-arity call cache
compute callee_fp from the prepared argument window
initialize callee_fp[0], callee_fp[2], and callee_fp[3]
call enter_aarch64_jit(...)
receive the tagged result
resume the caller at the post-call pc with that result as accumulator
```

The handler retains its caller `fp`, `pc`, and `CodeObject` across the native
adapter call. It does not install the compiled callee as threaded-interpreter
dispatch state on the successful path.

The integration must inspect the generated `preserve_none` call handler. A
normal ABI call may spill threaded-interpreter state to the native stack. That
is acceptable for bring-up but remains a measured transition cost, not an
assumed final shape.

## Safepoints And Roots

Keeping native `sp` active does not make managed roots visible. Before a future
compiled native call or safepoint, generated code must still publish the active
managed frontier and every live managed register root. Native stack words are
never treated as managed roots.

General runtime selection of compiled code also requires bounded safepoint
latency. Loop-backedge safepoint polling is the intended solution and is
separate from this calling-convention transition. The initial Fibonacci target
uses explicit compilation and constrained inputs; it is not a general tiering
policy.

## Side-Exit Bootstrap

The happy-path milestone does not implement recovery. Every published function
must nevertheless receive a valid side-exit target. The first target is an
intentional AArch64 trap stub, never address zero. Taking a guard or overflow
exit therefore fails immediately and diagnostically.

The later side-exit thunk can capture the compiled register image while native
`sp` is already valid, then call portable C++ transition code. Completing that
path still requires an interpreter return adapter that consumes
`managed_fp[1]` when a side-exited compiled frame finishes in the interpreter.
That work is explicitly outside this plan's successful-return milestone.

## Implementation Slices

### 1. Publish The Split-Stack Contract

Update every owning design document to assign `x20` as the managed frame
register, retain native `sp` and `x29`, remove architectural stack switching,
and state the limited adapter-based native compatibility precisely.

### 2. Move Managed Addressing To `x20`

Introduce shared constexpr AArch64 thread and managed-frame register encodings.
Use them in allocation constraints, stack-transfer emission, return emission,
transition binding, and diagnostics. Remove local AArch64 managed-frame
register literals.

Change ordinary generated return to restore `x20` from the previous-frame
header cell before `ret`. Update direct execution tests so runtime-shaped code
enters through an installed managed frame rather than relying on an accidental
C-function-compatible entry.

### 3. Add The Standalone Entry/Exit Adapter

Add the typed C++ wrapper and the static AArch64 assembly thunk. Verify zero,
one, and eight register parameters, tagged return through `x0`, installed
`x19`, installed `x20`, unchanged native stack discipline, and restoration of
the host's callee-saved registers.

### 4. Add The Trap Side-Exit Target

Publish one intentional trap stub and pass its real address into compilation
for normal-path runtime fixtures. Add no recovery state or graceful fallback in
this slice.

### 5. Enter JIT Code From `CallPositional`

Use the adapter after existing fixed-arity cache validation and frame
construction when the selected `CodeObject` has published JIT code. Keep
keyword adaptation, default adaptation, overflow parameters, and uncached
selection on the interpreter path.

### 6. Run And Measure Iterative Fibonacci

Use a source shape already supported by the direct Core translator:

```python
def fib(n):
    a = 0
    b = 1
    i = 0
    while i is not n:
        next_value = a + b
        a = b
        b = next_value
        i += 1
    return a
```

Compile and publish `fib` explicitly, invoke it through ordinary interpreted
`CallPositional`, and verify a tagged SMI result for inputs that cannot take a
side exit. Benchmark repeated warm calls with compilation excluded. Record the
adapter cost and retain the generated disassembly so later branch fusion,
edge-transfer cleanup, and immediate folding have a stable initial target.

## Deferred Work

This plan does not implement:

- automatic compilation, tiering, invalidation, or publication policy;
- side-exit recovery or interpreter-to-compiled return adaptation;
- JIT-emitted managed calls;
- native helper lowering, root publication, or call safepoints;
- loop-backedge safepoint polling;
- stack-passed managed parameters beyond `x7`;
- exception-marker propagation through compiled execution;
- native unwinding metadata, arm64e pointer authentication, or Linux BTI;
- a generated interpreter.

These exclusions keep the first milestone honest: it proves the real
interpreter-to-JIT happy path without presenting normal-path execution as a
runtime-complete JIT.

## Completion Criteria

The transition is complete when:

- all AArch64 managed frame accesses use the shared `x20` contract;
- native `sp`, `x19`, `x20`, and `x29` are restored after ordinary JIT return;
- compiled return restores the previous managed frame before `ret`;
- zero-through-eight fixed tagged parameters enter in `x0` through `x7`;
- interpreted fixed-arity calls can invoke manually published JIT code;
- all unimplemented exits target the intentional trap stub;
- iterative Fibonacci executes correctly through that interpreter call path;
- debug tests pass and the generated call handler and Fibonacci loop are
  inspected for avoidable transition overhead.
