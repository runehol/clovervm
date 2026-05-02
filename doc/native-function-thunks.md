# Native Function Thunks

## Goal

Native C++ functions should be callable through the normal `Function` object and
interpreter frame path. The call site should not need a broad "is this a
BuiltinFunction?" branch for every native implementation detail.

The transition is incremental:

- native functions are represented as ordinary `Function` objects with tiny
  thunk `CodeObject`s
- call sites perform arity/default/varargs adaptation at the `Function`
  boundary before entering the thunk frame
- exception normalization, packed `*args` conventions, and lower-level native
  stack calling are later steps

## Current Implementation

Fixed-arity native functions are built with overloads of
`make_native_function()` from [src/native_function.h](../src/native_function.h):

```cpp
TValue<Function> make_native_function(VirtualMachine *vm,
                                      NativeFunction0 function);
TValue<Function> make_native_function(VirtualMachine *vm,
                                      NativeFunction1 function);
TValue<Function> make_native_function(VirtualMachine *vm,
                                      NativeFunction2 function);
TValue<Function> make_native_function(VirtualMachine *vm,
                                      NativeFunction3 function);
```

The C++ function type determines the arity. Call sites do not pass a separate
arity argument:

```cpp
Value native_str_add(Value left, Value right);

make_native_function(vm, native_str_add);
```

Each generated function owns an immortal thunk `CodeObject` with this shape:

```text
CallNativeN 0
Return
```

where `N` is currently `0`, `1`, `2`, or `3`. The operand indexes into the code
object's native target table.

Native targets are stored in [src/code_object.h](../src/code_object.h) as an
untagged union:

```cpp
using NativeFunction0 = Value (*)();
using NativeFunction1 = Value (*)(Value);
using NativeFunction2 = Value (*)(Value, Value);
using NativeFunction3 = Value (*)(Value, Value, Value);

union NativeFunctionTarget
{
    NativeFunction0 fixed0;
    NativeFunction1 fixed1;
    NativeFunction2 fixed2;
    NativeFunction3 fixed3;
};
```

The opcode selects the union arm. This keeps the target encoding lightweight
without putting raw function addresses into the constant pool.

## Call Path

For fixed-arity native functions, the generic call path sees a normal
`Function`:

```text
caller
  CallSimple sets up a callee frame

native thunk frame
  CallNative0/1/2/3 reads p0, p1, ...
  calls the C++ target
  stores the returned Value in the accumulator
  Return leaves the thunk like an ordinary bytecode function
```

The thunk reads arguments directly from the interpreter frame. No argument array
or tuple is allocated for fixed arity.

Native callbacks no longer receive `ThreadState *`. Code that needs thread
state should use the TLS-backed helpers such as `active_thread()` or wrappers
like `make_object_value<T>(...)`.

## Variable Arity

There is no residual `BuiltinFunction` representation. `range` is an ordinary
native thunk `Function`: its C++ target is a three-argument native function, and
the `Function` object supplies two `None` defaults so the public arity is
`range(stop)`, `range(start, stop)`, or `range(start, stop, step)`.

True variadic native callables should still avoid receiving a raw `argc`.
Python functions normalize variable positional arguments into an object-level
representation such as a tuple. The native equivalent should follow that shape
when we add the next convention.

Likely next conventions:

```text
CallNativeTuple     native(args_tuple)
CallNativeVector    native(args_tuple_or_span) after the representation is clear
CallNativeSlot      native slot shapes such as nb_add, tp_iternext, sq_length
```

The exact opcode names are still open for the variable-arity work. The fixed
opcodes are intentionally concrete: `CallNative0`, `CallNative1`,
`CallNative2`, and `CallNative3`.

## Exception Normalization

The current fixed-arity thunks use ordinary `Return`. Native functions may still
raise through the current C++ exception strategy.

The later exception plan needs native thunk frames because the thunk is the
natural boundary where native sentinel conventions can become VM exception
delivery:

```text
native success:
  store normal Value in accumulator
  return to caller

native failure:
  leave pending exception on ThreadState / PyErr state
  store Value::exception_marker() in accumulator
  normalize according to the caller's return mode
```

That normalization may become a `NativeReturn` opcode or an equivalent thunk
return adapter. At that point ordinary bytecode callers should still see normal
exception unwinding, while `ViaResult` protocol callers can receive
`Value::exception_marker()`.

## Arity

Arity checks live on the Python-visible `Function` call boundary. The
interpreter checks `Function::accepts_arity()` before entering bytecode,
constructor thunk, or native thunk frames, then applies default and `*args`
adaptation in the shared frame setup path.

## Implemented Uses

The first migrated methods and builtins are:

- `str.__str__`
- `str.__add__`
- `range`

`str.__add__(other_str)` now exercises the fixed-arity native thunk path.
Passing a non-string currently raises `UnimplementedError`, which is the desired
shape for later binary-operator fallback work.

Tests cover direct native thunk calls for arities 0, 1, 2, and 3, plus the string
method cases and `range`'s defaulted three-argument native thunk.

## Remaining Work

1. Design and implement the packed tuple/vector native convention for true
   variadic native callables.
2. Add native exception normalization through thunk return adapters.
3. Add specialized interpreter or JIT fast paths for trivial native thunk code
   objects when measurements justify it.
4. Move function and native-function entry to the Clover/Python stack on
   AArch64, using assembly transition stubs to save interpreter state, switch
   the machine stack pointer, enter the target, and return through an
   interpreter-resume thunk.

## Invariants

- Fixed-arity native callables are ordinary `Function` objects with thunk
  `CodeObject`s.
- Native target pointers live in `CodeObject::native_function_targets`, not in
  `constant_values`.
- `NativeFunctionTarget` is untagged; the opcode determines the calling
  convention.
- Native callbacks do not receive `ThreadState *`; thread state is available
  through TLS.
- Public arity is owned by `Function`, including native functions with default
  parameters.

## AArch64 Python-Stack Entry Plan

The current native thunk path is semantically shaped like a normal Python
function call, but it still executes native targets from inside an interpreter
opcode handler. The handler frame, caller-save registers, and C++ return path
therefore live on the interpreter's native stack, not on Clover's Python stack.

For JIT-compatible execution, function entry should instead make the Clover
frame the active machine frame. Interpreted calls, native thunk calls, and later
JIT-to-JIT calls can then share one frame model. The interpreter only needs a
transition at the interpreted/native boundary.

The proposed split is:

- `ThreadState` owns durable transition state:
  - saved interpreter machine stack pointer
  - saved interpreter frame pointer
  - any machine stack spill area the transition stubs need
- AArch64 entry stubs enter Python code:
  - save the interpreter machine `sp`/`x29` in `ThreadState`
  - perform any required stack/register saves for the transition itself
  - partially enter the callee's Clover frame by computing the same `new_fp`
    that the interpreted call path would use
  - move the machine `sp` to the callee's Clover frame-record location, the
    address that will be the interpreted `fp`, preserving 16-byte ABI alignment
  - enter with `x29` still carrying the previous Clover frame pointer
  - place arguments in the ABI argument registers from `p0`, `p1`, ...
  - put an interpreter-resume thunk in `x30`/LR
  - branch to the target's native entry point
- AArch64 return thunks resume interpreted mode:
  - receive the native return value in the normal ABI return register
  - use that return value as the interpreter accumulator
  - restore the interpreter machine `sp`/`fp`
  - reload the Python `fp`, bytecode `pc`, and `code_object` from the frame
    header
  - reload `dispatch` from the global dispatch table
  - tail-enter the interpreter dispatch loop at the saved return PC

The frame header already reserves `fp[1]` as a compiled return PC and
`fp[0]` as the previous Python frame pointer. Interpreted returns use `fp[2]`
and `fp[3]` to restore the bytecode `code_object` and `pc`. JIT/native entry
should preserve that dual use:

- compiled/native returns use `fp[1]` as the LR-compatible continuation
- interpreter returns use `fp[2]` and `fp[3]` as the resume metadata
- mixed-mode calls write both when crossing from interpreted code into
  native/JIT code, so either return path has enough state

For a call from interpreted code into a native function, the caller-side
transition partially enters the frame instead of completing it. It computes
`new_fp`, writes the interpreter-only resume metadata, then enters native code
with `sp == new_fp`, `x29 == previous Clover fp`, and `x30 == thunk-back
converter`.

The native prologue can then finish the AArch64-shaped frame record in the same
slots Clover already reserves:

- `new_fp[1]`: native/compiled return PC, populated from LR, which holds the
  thunk-back converter
- `new_fp[0]`: previous Clover frame pointer, populated from incoming `x29`
- `new_fp[2]`: interpreter return code object, written by the caller-side
  transition
- `new_fp[3]`: interpreter return PC, written by the caller-side transition

The native target can then follow the same frame convention as JIT code. Its LR
points at the thunk-back converter, and the converter can restore interpreted
mode from the ordinary frame metadata instead of from a separate logical
register save area.

For today's fixed native thunk bytecode shape, the back-transition should not
tail-enter the interpreter dispatch loop. A thunk body is still:

```text
CallNativeN target
Return
```

So the C++ native call must have zero net machine-stack effect: enter the
AArch64 bridge, run the C++ target, restore the interpreter machine stack, and
return to the `CallNativeN` opcode handler with the returned `Value`. The
ordinary bytecode `Return` that follows then unwinds the Clover/Python frame.
Jumping directly to dispatch from the native return path would conflate "return
from the C++ target" with "return from the Python callable" and skip the
bytecode thunk protocol. Direct dispatch resume only belongs to a later
compiled/JIT return path that is deliberately replacing the bytecode `Return`.

That means the transition does not need to copy the interpreter's logical
register state into `ThreadState`. On return, the accumulator is the native ABI
return value, `dispatch` is process-global state, and the caller's Python
`fp`/`pc`/`code_object` are already in the Clover frame header.

This suggests a staged implementation:

0. Finish Python/VM exception normalization for native calls. C++ exceptions
   must not unwind across a handwritten stack-switch bridge; native failures
   should return through the VM's pending-exception/sentinel convention before
   this path becomes generally usable.
1. Add explicit `ThreadState` fields for the saved interpreter machine
   `sp`/`x29` and any transition spill slots. Keep `run_interpreter()`
   source-compatible at first by initializing those fields in
   `ThreadState::run()`.
2. Add a small platform layer, for example `native_entry_aarch64.S`, with one
   bridge for each fixed native calling convention:
   - `cl_enter_native0_aarch64`
   - `cl_enter_native1_aarch64`
   - `cl_enter_native2_aarch64`
   - `cl_enter_native3_aarch64`

   Each bridge can load exactly the ABI argument registers it needs from the
   Clover parameter slots and then call the fixed-arity native target. A
   portable C++ fallback should preserve the existing behavior on non-AArch64.
3. Give `CodeObject` or `Function` an optional native entry pointer. Native
   thunk `CodeObject`s can initially point at generated fixed-arity entry stubs
   that load the target from `native_function_targets` and use the ABI call.
4. Teach `op_call_simple` and `op_call_method_attr` to enter through the native
   entry pointer when present, after they have already performed the existing
   arity/default/varargs frame adaptation.
5. Keep `CallNative0`/`CallNative1`/`CallNative2`/`CallNative3` as the portable
   interpreter fallback until the AArch64 path is stable, then decide whether
   native-only thunk code objects still need bytecode bodies.

### Current Frame Layout

The interpreted frame layout now matches the AArch64/V8-style shape the bridge
needs. The whole fixed header lives above or at `fp`, leaving the first slots
below incoming `sp` available for a normal native frame record:

```asm
stp x29, x30, [sp, #-16]!
mov x29, sp
```

The fixed header is:

```text
higher addresses

    fp[padded_n_parameters + header_size - 1]   p0
    ...
    fp[4]                                      last param / padding
    fp[3]                                      interpreter return PC
    fp[2]                                      interpreter return code object
    fp[1]                                      compiled/native return PC
fp->fp[0]                                      previous frame pointer
    fp[-1]                                     r0
    fp[-2]                                     r1
    ...
    fp[-n]                                     outgoing/scratch area

lower addresses
```

Interpreted calls eagerly initialize the interpreter-visible header:

```cpp
new_fp[0] = old_fp;
new_fp[2] = return_code_object;
new_fp[3] = return_pc;
```

`fp[1]` remains the LR-compatible compiled/native return PC slot. Interpreted
bytecode calls do not currently need to populate it, but mixed-mode entry can
write it when crossing into native/JIT code.

With this layout, native/JIT entry can use ordinary AArch64 incoming state:

```text
entry sp = new_fp + 16
x29      = old_fp
lr       = native/interpreter continuation
```

and a standard prologue naturally creates:

```text
new_fp[0] = old_fp
new_fp[1] = continuation
x29       = new_fp
sp        = new_fp or lower
```

This also prepares the runtime to track a real frame `sp` separately from
`fp`. `fp` is the stable metadata/register anchor, while `sp` gives the lower
bound for frame-local storage. That lower bound will matter for precise stack
root scanning once JIT/native frames can allocate spills, scratch space, and
outgoing call areas dynamically.

### ThreadState Access From Assembly

The current active thread pointer is a private C++ `thread_local`:

```cpp
thread_local ThreadState *ThreadState::current_thread = nullptr;
```

Raw AArch64 assembly should not reach into that compiler-managed TLS symbol
directly.

On the call side, the bridge signatures can receive `ThreadState *` from the
C++ caller, which can obtain it through `active_thread()` before crossing into
assembly.

On the return side, the interpreter-back-transition thunk has no caller help:
it is reached through LR from native code. The first implementation should call
an exported C ABI helper such as:

```cpp
extern "C" ThreadState *cl_active_thread_for_asm();
```

implemented in C++ as a thin wrapper around `ThreadState::get_active()`. The
return thunk can call this helper before restoring the interpreter machine
`sp`/`x29`. This adds one ordinary helper call on mixed-mode return, but keeps
compiler/platform TLS details out of handwritten assembly.

If bridge overhead later matters, expose a stable assembly-facing TLS cell or
fast accessor as a separate step. Good options are:

- add an exported `extern "C" thread_local ThreadState *cl_current_thread` with
  a documented ABI and use platform-specific TLS access sequences in `.S`
- reserve a platform register only if the target platform ABI permits it; do
  not use `x18` casually because AAPCS64 leaves it platform-specific
