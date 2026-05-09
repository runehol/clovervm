# Native/Managed Boundaries

## Goal

CloverVM has two explicit boundary directions:

- managed code calls native C++ functions
- native C++ code calls managed `Function` objects

Both directions preserve one managed frame model, one pending-exception model,
and one root-scanning story. Native C++ code runs on the native machine stack.
Managed interpreter state lives on the Clover stack and is reached through
explicit frame pointers.

## Stack Ownership

CloverVM has two different stack roles:

- the Clover stack stores VM-managed frames, registers, and call windows
- the native machine stack stores the C++ interpreter implementation, C++
  runtime helpers, native builtin calls, and arbitrary host/native spills

Only VM-controlled frame contents belong on the Clover stack. The GC and
deferred-refcounting design rely on being able to understand Clover stack
contents as managed `Value` slots and frame metadata. Native C++ code is not
under that control: the compiler may spill non-`Value` data, stale pointers,
temporary integers, return addresses, and ABI bookkeeping into its stack frame.
Running native functions with machine `sp` on the Clover stack would make that
memory either imprecise to scan or unsafe to scan as managed slots.

The boundary rule is:

```text
managed-to-managed:
  may stay on the Clover stack

managed-to-native:
  publish/materialize managed roots as needed
  call C++/native code
  return Value, or exception_marker with pending exception state

native-to-managed:
  build a managed wrapper frame linked to the saved Clover fp
  call the target Function through normal Function call semantics
  pop the wrapper before returning to native
  return Value, or exception_marker with pending exception state
```

Interpreted bytecode already follows the split in a different way: the
interpreter executes on the native stack while explicitly reading and writing
Clover frame slots through `fp`. Native function thunks called by the
interpreter also execute their C++ targets on the native stack.

## Managed To Native: Native Function Thunks

Native C++ functions are callable through the normal `Function` object and
interpreter frame path. The call site does not need a broad "is this a
BuiltinFunction?" branch for every native implementation detail.

The implemented representation is:

- native functions are represented as ordinary `Function` objects with tiny
  managed thunk `CodeObject`s
- call sites perform arity/default/varargs adaptation at the `Function`
  boundary before entering the thunk frame
- native functions execute on the native machine stack, not on the Clover stack
- managed thunks normalize native pending-exception results back into managed
  exceptional unwind

### Implementation

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

Each generated function owns an immortal managed thunk `CodeObject` with this
shape:

```text
CallNativeN 0
ReturnOrRaiseException
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

For fixed-arity native functions, the generic call path sees a normal
`Function`:

```text
caller
  CallSimple sets up a managed callee frame on the Clover stack

native thunk frame
  CallNative0/1/2/3 reads p0, p1, ...
  sets ThreadState's Clover frame frontier to the current fp
  calls the C++ target on the native stack
  stores the returned Value in the accumulator
  ReturnOrRaiseException either returns normally or enters managed unwind
```

The thunk reads arguments directly from the managed frame. No argument array or
tuple is allocated for fixed arity.

Native callbacks do not receive `ThreadState *`. Code that needs thread state
uses the TLS-backed helpers such as `active_thread()` or wrappers like
`make_object_value<T>(...)`.

### Variable Arity Status

There is no residual `BuiltinFunction` representation. `range` is an ordinary
native thunk `Function`: its C++ target is a three-argument native function, and
the `Function` object supplies two `None` defaults so the public arity is
`range(stop)`, `range(start, stop)`, or `range(start, stop, step)`.

There is no true variadic native convention yet. Fixed native thunks are
intentionally concrete: `CallNative0`, `CallNative1`, `CallNative2`, and
`CallNative3`.

### Managed-To-Native Exception Normalization

Fixed-arity thunks end in `ReturnOrRaiseException`. Native functions report
ordinary success by returning a normal `Value`. They report explicit VM failure
by setting pending exception state on `ThreadState` and returning
`Value::exception_marker()`.

```text
native success:
  store normal Value in accumulator
  ReturnOrRaiseException performs normal Return

native failure:
  leave pending exception on ThreadState
  store Value::exception_marker() in accumulator
  ReturnOrRaiseException enters managed exceptional unwind
```

The adapter keeps ordinary bytecode callers on normal exception unwinding while
still letting native implementations report failure as pending exception plus
`Value::exception_marker()` locally inside the thunk. Native boundaries do not
become a first-order unwinder frame kind.

C++ exceptions are still outer panic plumbing in some old paths. In
the current interpreter-only runtime they unwind the native C++ interpreter
stack to the outer harness cleanly enough for fatal/unhandled cases, but they
must not become the native-call convention. Expected VM failures at native
boundaries use pending exception state plus `Value::exception_marker()`.
There are no handwritten stack-switch bridges in the current runtime.

For native functions that also expose a stop-returning convention, the ordinary
and stop-returning `CodeObject`s are sibling thunks. Both thunks call the same
native implementation directly rather than having one thunk call the other's
`CodeObject`.

```text
ordinary native thunk:
  CallNativeN
  ReturnOrRaiseException

stop-returning native thunk:
  CallNativeN
  ReturnStopIterationOrRaiseException
```

## Native To Managed Entry Shapes

Native-to-managed entry uses the managed `Function` call path:

- function entry, where native code calls a `TValue<Function>` and wants normal
  Python function-call semantics
- startup code-object entry, where host/runtime code temporarily wraps a
  nullary `CodeObject` in a `Function` and then delegates to the same function
  entry path

The distinction is now at API construction time, not at the native boundary
frame protocol. Raw module code objects are not public Python callables, but the
VM can still use a temporary internal `Function` wrapper so startup follows the
same arity, exception, and frontier rules as other native-to-Clover entries.

## Native To Managed: Function Call Wrappers

Native code can call a managed `Function` and receive the same local result
convention native functions use:

```text
success:
  return normal Value
  no pending exception

failure:
  return Value::exception_marker()
  pending exception remains on ThreadState
```

The native caller does not handle defaults, varargs, constructor thunk
selection, or other `Function` entry policy. The wrapper calls the target
through `CallSimple` so the existing managed call path handles those semantics.

The contract is intentionally narrow:

- the callable is a `TValue<Function>`
- the native caller supplies only actual positional arguments
- `self`, when needed, is already included as argument `0`
- the primary C++ API uses fixed-arity overloads rather than a materialized
  argument span
- keyword arguments and arbitrary callable protocol dispatch are not part of
  this API
- method lookup is handled by a separate native method-call API because lookup
  and binding semantics are different

### Wrapper Shape

Build and cache one native-call wrapper per positional arity:

```text
clover_function_entry_adapter_0(callable)
clover_function_entry_adapter_1(callable, arg0)
clover_function_entry_adapter_2(callable, arg0, arg1)
...
```

Each wrapper is reusable for any `Function` with that supplied arity. The target
is passed in parameter `p0`, not baked into a wrapper constant.

Conceptually, wrapper `N` has:

```text
p0      callable
p1      arg0
p2      arg1
...
pN      arg(N - 1)
```

The wrapper prepares an ordinary call window and uses `CallSimple`:

```text
copy p0 into callable_slot
copy p1..pN into a0..a(N - 1)
CallSimple callable_slot, a0, N, cache_idx
ReturnToNative

handler:
  ReturnPendingExceptionToNative

Exception table:
  CallSimple range -> handler
```

The copying is intentionally explicit. `CallSimple` expects
the callable separately from the outgoing argument span, while parameters and
outgoing call slots live in different parts of the frame. With the current
bytecode set, each copy is a two-step accumulator move:

```text
Ldar p1
Star a0
Ldar p2
Star a1
...
```

That is the implemented wrapper construction. It is intentionally simple and
can be specialized if measurements justify avoiding those moves.

### Native C++ API

The native-facing API makes the fixed-arity path the normal path. CloverVM is a
C++17 code base, and the common native call sites know their arity statically.
There is no primary `std::span` or other materialized contiguous argument-view
interface.

Implemented overloads:

```cpp
Value ThreadState::call_clovervm_function(TValue<Function> function);
Value ThreadState::call_clovervm_function(TValue<Function> function,
                                          Value arg0);
Value ThreadState::call_clovervm_function(TValue<Function> function,
                                          Value arg0,
                                          Value arg1);
Value ThreadState::call_clovervm_function(TValue<Function> function,
                                          Value arg0,
                                          Value arg1,
                                          Value arg2);
```

Each overload enters the matching arity wrapper:

```text
call_clovervm_function(function, arg0, arg1)
  -> clover_function_entry_adapter_2(function, arg0, arg1)
```

The contract mirrors native function result conventions:

```text
success:
  return normal Value
  no pending exception

failure:
  return Value::exception_marker()
  pending exception remains on ThreadState
```

There is no arbitrary-arity function entry API.

Do not fold method lookup into `call_clovervm_function`. That API assumes the
caller already has a `TValue<Function>` and exact positional arguments.
`call_clovervm_method` performs method lookup and binding deliberately, then
forwards to the function-call overloads.

## Native To Managed: Method Calls

Native method calls use fixed-arity `ThreadState::call_clovervm_method`
overloads:

```cpp
Value ThreadState::call_clovervm_method(Value receiver, TValue<String> name);
Value ThreadState::call_clovervm_method(Value receiver, TValue<String> name,
                                        Value arg0);
Value ThreadState::call_clovervm_method(Value receiver, TValue<String> name,
                                        Value arg0,
                                        Value arg1);
Value ThreadState::call_clovervm_method(Value receiver, TValue<String> name,
                                        Value arg0,
                                        Value arg1,
                                        Value arg2);
```

The method API loads the named method with the same binding rules used by
interpreted method calls. If lookup returns a class function, the receiver is
prepended as `self`; if lookup returns an own function or another callable, no
receiver is inserted. The resulting callable and argument list are then passed
to the ordinary Clover function entry path.

Missing methods return `Value::exception_marker()` with pending
`AttributeError`. Non-callable method values return `Value::exception_marker()`
with pending `TypeError`.

## Native To Managed: Startup Code-Object Entry

Module startup enters a `CodeObject` that is explicitly not a `Function`. A
module body has arity 0 and does not own a public `Function` object.

`ThreadState::run_clovervm_code_object()` creates a temporary nullary
`Function` around the module `CodeObject` and delegates to
`call_clovervm_function`. The resulting call uses the ordinary cached Clover
function entry adapter:

```text
clover_function_entry_adapter_0(function)
  CallSimple function, a0, 0
ReturnToNative

handler:
  ReturnPendingExceptionToNative
```

On success, `ThreadState::run_clovervm_code_object()` returns the accumulator.
On failure, it returns `Value::exception_marker()` and leaves the pending
exception on `ThreadState`. The host layer can choose whether to format that
pending exception, propagate the marker, or handle it locally.

This does not make module code objects user-visible functions. The temporary
wrapper is an internal entry adapter, and there is still no general raw
`CodeObject` native-call API.

### ReturnToNative

`ReturnToNative` is a native boundary return.

Contract:

```text
restore fp from the current frame header, as ordinary Return would
set ThreadState's Clover frame frontier to the restored fp
return the accumulator to native C++ code
```

This pops the native-call wrapper before control returns to native. Completed
wrapper frames do not remain in the managed frame chain and do not stay live as
GC roots.

### ReturnPendingExceptionToNative

`ReturnPendingExceptionToNative` is the exceptional twin of `ReturnToNative`.

Contract:

```text
pending exception must be set on ThreadState
restore fp from the current frame header, as ordinary Return would
set ThreadState's Clover frame frontier to the restored fp
return Value::exception_marker() to native C++ code
```

The opcode must not clear, format, materialize, or throw the pending exception.
The native caller decides whether to propagate it outward, convert it through
another boundary, or handle and clear it explicitly.

The function entry adapters use this opcode in their exception-table handlers,
so startup failure and ordinary native-to-Clover function failure share the
same result convention: exception marker return plus pending exception state on
`ThreadState`.

### Clover Frame Frontier

`ThreadState` keeps a Clover frame frontier pointer for native execution. This
is the newest live Clover frame available to native C++ code while the
interpreter is not actively carrying `fp` in its dispatch state. It is a
frame-chain anchor, not the full stack extent.

`ThreadState` initializes the frontier to a permanent sentinel Clover frame. The
sentinel frame terminates the frame chain with `previous_fp == nullptr`.
Native-to-Clover entry adapters push boundary frames whose previous fp is the
current frontier. While the interpreter is actively running, its local `fp` is
the newest live Clover frame. Any opcode that crosses from interpreted
execution into native C++ must set the frontier before control leaves
interpreted execution:

```text
CallNative0/1/2/3:
  thread->clover_frame_frontier = fp
  call native target on the native stack

ReturnToNative:
  pop the wrapper frame
  thread->clover_frame_frontier = restored caller fp
  return accumulator

ReturnPendingExceptionToNative:
  pop the wrapper frame
  thread->clover_frame_frontier = restored caller fp
  return Value::exception_marker()
```

`pc` and `code_object` do not need to be saved as durable cross-boundary
thread-state fields. While the interpreter is running, they are live interpreter
state. For unwinding and stack scanning, the managed frame headers and exception
metadata carry the durable structure.

### Reentrant Weaving

Native and managed code may weave in and out repeatedly:

```text
managed A -> native f -> managed B -> native g -> managed C
```

While `C` is active, the Clover frame chain is still walkable:

```text
C frames
  wrapper for g -> C
B frames
  wrapper for f -> B
A frames
...
```

When `C` returns to `g`, the `C` frames and its wrapper are popped, and
`ThreadState::clover_frame_frontier` points back to the newest still-live `B`
frame. When `B` returns to `f`, the frontier points back to the live `A` frame.

This keeps GC scanning focused on currently live managed frames. Completed
boundary wrappers are not retained as stack roots. Traceback/history concerns
belong in traceback state, not in preserved dead stack frames.

## Frame Layout Implications

The interpreted frame layout is used for interpreter calls, constructor thunks,
native thunk frames, native-to-managed wrappers, and exception unwinding:

```text
higher addresses

    fp[padded_n_parameters + header_size - 1]   p0
    ...
    fp[4]                                      last param / padding
    fp[3]                                      interpreter return PC
    fp[2]                                      interpreter return code object
    fp[1]                                      compiled return pc slot
fp->fp[0]                                      previous Clover frame pointer
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

`fp[1]` is reserved for compiled-code return state. It is not a native LR slot
for arbitrary C++ code.

## Implemented Uses

Migrated native methods and builtins include:

- `str.__str__`
- `str.__add__`
- `range`

Tests cover direct native thunk calls for arities 0, 1, 2, and 3, the
`ReturnOrRaiseException` thunk shape, native marker-to-exception unwinding, the
string method cases, `range`'s defaulted three-argument native thunk, Clover
frame frontier updates, native boundary returns, Clover function entry adapter
wrappers, native method-call lookup/binding, and startup entry through a
temporary function wrapper.

Clover function entry adapter wrappers are generated and cached by positional
arity. `ReturnToNative` and `ReturnPendingExceptionToNative` are implemented for
boundary wrappers. The Clover frame frontier is initialized to a terminated
sentinel frame and set by native boundary returns and by the
fixed-arity `CallNative0`/`CallNative1`/`CallNative2`/`CallNative3` interpreter
opcodes.

## Invariants

- Fixed-arity native callables are ordinary `Function` objects with managed
  thunk `CodeObject`s.
- Native-to-managed calls target `TValue<Function>` and use `CallSimple` so
  defaults, varargs, arity checks, and constructor thunks remain in the managed
  call path.
- The native-to-managed function API is fixed-arity overloads. Common call
  sites do not materialize an argument array.
- Method lookup is not part of `call_clovervm_function`; native method calls
  use `call_clovervm_method`, which performs lookup and binding before
  delegating to function entry.
- Startup code-object entry wraps the module `CodeObject` in a temporary
  internal nullary `Function` and delegates to `call_clovervm_function`.
- Native target pointers live in `CodeObject::native_function_targets`, not in
  `constant_values`.
- `NativeFunctionTarget` is untagged; the opcode determines the calling
  convention.
- Native callbacks do not receive `ThreadState *`; thread state is available
  through TLS.
- Public arity is owned by `Function`, including native functions with default
  parameters.
- Native C++ functions execute on the native stack.
- Only VM-managed frame/register state belongs on the Clover stack.
- `ReturnToNative` pops a linked native-call wrapper and saves the restored live
  managed fp. Startup entry uses the same native boundary return convention.
- Native/C boundaries are not first-order managed unwinder frames; managed
  thunks and entry adapters adapt native results back into the VM exception
  model.
- Completed native-to-managed wrapper frames are popped before returning to
  native and must not remain live as GC roots.
