# Native/Managed Boundaries

## Goal

CloverVM needs two explicit boundary directions:

- managed code calls native C++ functions
- native C++ code calls managed `Function` objects

Both directions should preserve one managed frame model, one pending-exception
model, and one root-scanning story. Native C++ code runs on the native machine
stack. Managed interpreter/JIT state lives on the Clover stack and is reached
through explicit frame pointers and transition metadata.

## Stack Ownership

CloverVM has two different stack roles:

- the Clover stack stores VM-managed frames, registers, call windows, and future
  JIT frames
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
  switch to the native stack when coming from JIT/managed machine code
  call C++/native code
  return Value, or exception_marker with pending exception state
  switch back to managed execution

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
interpreter frame path. The call site should not need a broad "is this a
BuiltinFunction?" branch for every native implementation detail.

The current direction is:

- native functions are represented as ordinary `Function` objects with tiny
  managed thunk `CodeObject`s
- call sites perform arity/default/varargs adaptation at the `Function`
  boundary before entering the thunk frame
- native functions execute on the native machine stack, not on the Clover stack
- managed thunks normalize native pending-exception results back into managed
  exceptional unwind
- packed `*args` conventions and JIT/native transition stubs are later steps

### Current Implementation

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

Native callbacks no longer receive `ThreadState *`. Code that needs thread
state should use the TLS-backed helpers such as `active_thread()` or wrappers
like `make_object_value<T>(...)`.

### Variable Arity

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

C++ exceptions are still temporary outer panic plumbing in some old paths. In
the current interpreter-only runtime they unwind the native C++ interpreter
stack to the outer harness cleanly enough for fatal/unhandled cases, but they
must not become the native-call convention. Expected VM failures at native
boundaries use pending exception state plus `Value::exception_marker()`.
Future handwritten stack-switch bridges must not allow C++ exceptions to unwind
across the bridge. Native functions used from those paths must return through
the pending-exception/sentinel convention.

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

Native-to-managed entry has two different call shapes:

- function entry, where native code calls a `TValue<Function>` and wants normal
  Python function-call semantics
- raw code-object entry, where host/runtime code enters a `CodeObject` that is
  explicitly not a `Function`

The distinction matters. Module code objects are not functions. They have arity
0, do not own `Function` defaults or varargs policy, and should continue to use
the prepared direct-code path.

## Native To Managed: Function Call Wrappers

Native code should be able to call a managed `Function` and receive the same
local result convention native functions use:

```text
success:
  return normal Value
  no pending exception

failure:
  return Value::exception_marker()
  pending exception remains on ThreadState
```

The native caller should not handle defaults, varargs, constructor thunk
selection, or other `Function` entry policy. The wrapper should call the target
through `CallSimple` so the existing managed call path handles those semantics.

The first contract is intentionally narrow:

- the callable is a `TValue<Function>`
- the native caller supplies only actual positional arguments
- `self`, when needed, is already included as argument `0`
- the primary C++ API uses fixed-arity overloads rather than a materialized
  argument span
- keyword arguments and arbitrary callable protocol dispatch are later steps
- method lookup, including special-method lookup such as `obj.__str__()`, is a
  separate later API because lookup and binding semantics are different

### Wrapper Shape

Build and cache one native-call wrapper per positional arity:

```text
native_call_wrapper_0(callable)
native_call_wrapper_1(callable, arg0)
native_call_wrapper_2(callable, arg0, arg1)
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

The copying is intentionally explicit in the first design. `CallSimple` expects
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

That is acceptable for the first wrapper implementation. The wrapper can later
grow specialized construction if measurements justify avoiding those moves.

### Native C++ API

The native-facing API should make the fixed-arity path the normal path. CloverVM
is currently a C++17 code base, and the common native call sites know their
arity statically. Avoid making `std::span` or another materialized contiguous
argument view the primary interface.

Initial overloads:

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
  -> native_call_wrapper_2(function, arg0, arg1)
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

If arbitrary arity becomes necessary, add it as an explicit fallback such as:

```cpp
Value ThreadState::call_clovervm_function_array(TValue<Function> function,
                                                const Value *args,
                                                uint32_t argc);
```

That fallback should not replace the fixed-arity overloads as the preferred API.

Do not fold method lookup into `call_clovervm_function`. That API assumes the
caller already has a `TValue<Function>` and exact positional arguments. A later
special-method API, for example for `obj.__str__()`, should encode its own
lookup and binding semantics deliberately before forwarding to the function-call
overloads.

## Native To Managed: Startup Code-Object Entry

Module startup enters a `CodeObject` that is explicitly not a `Function`. A
module body has arity 0, does not own `Function` defaults or varargs policy, and
should not be wrapped in a `Function` just to reuse function-call machinery.

The startup wrapper is a real native boundary frame linked to the current Clover
frame frontier. It uses the same local result convention as native-to-Clover
function entry:

```text
CallCodeObject c[target_code_object], a0, 0
ReturnToNative

handler:
  ReturnPendingExceptionToNative
```

On success, `ThreadState::run()` returns the accumulator. On failure, it returns
`Value::exception_marker()` and leaves the pending exception on `ThreadState`.
The host layer can choose whether to format that pending exception as a
`PythonException`, propagate the marker, or handle it locally.

This direct startup path is intentionally separate from function call wrappers.
It bypasses `Function` arity/default/varargs behavior because module code
objects do not have that public call contract. There is no general raw
`CodeObject` native-call API yet; add one only when there is a concrete runtime
entry point that needs it.

### Halt

`Halt` is the legacy low-level interpreter exit. It returns the accumulator from
the current interpreter invocation without restoring a caller frame from the
current Clover frame header.

Before returning to native C++ code, `Halt` sets the Clover frame frontier to
the current `fp`. It is useful for hand-built test code and other deliberately
unlinked interpreter invocations, not for ordinary native-to-Clover entry.

`Halt` should not be used for reusable native-to-managed call wrappers that are
linked into an existing Clover frame chain. Those wrappers need `ReturnToNative`
or `ReturnPendingExceptionToNative` so they pop themselves and set the Clover
frame frontier to the restored live fp.

### ReturnToNative

`ReturnToNative` is a native boundary return. It intentionally diverges from
`Halt`.

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

The startup wrapper now uses this opcode in its exception-table handler, so
startup failure has the same native boundary result convention as function
entry: exception marker return plus pending exception state on `ThreadState`.

### Clover Frame Frontier

`ThreadState` keeps a Clover frame frontier pointer for native execution. This
is the newest live Clover frame available to native C++ code while the
interpreter is not actively carrying `fp` in its dispatch state. It is a
frame-chain anchor, not the full stack extent.

`ThreadState` initializes the frontier to a permanent sentinel Clover frame. The
sentinel frame terminates the frame chain with `previous_fp == nullptr`.
`ThreadState::run()` pushes a startup boundary frame whose previous fp is the
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

Halt:
  thread->clover_frame_frontier = fp
  return accumulator
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

While `C` is active, the Clover frame chain should still be walkable:

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
should be handled by traceback state, not by preserving dead stack frames.

## JIT Transition Direction

The existing bytecode native thunk model is a good interpreter scaffold, but it
is not the long-term optimized JIT ABI.

When future JIT code runs on the Clover stack, it should call native functions
through managed-to-native transition stubs:

```text
JIT/managed code on Clover stack
  save managed continuation state
  publish or materialize live Value roots not already in managed slots
  switch machine sp/fp to the native stack
  call the native target with the platform ABI
  receive Value in the normal ABI return location
  restore the managed stack and continuation state
  resume with the returned Value as the managed accumulator/result
```

If the native target returns `Value::exception_marker()`, the transition resumes
managed code at an adapter/continuation that enters the same
`ReturnOrRaiseException`-style exceptional path. The pending exception state on
`ThreadState` remains the source of truth.

The transition record belongs to the managed-to-native boundary, not to the
native target's Clover frame. Native code must not create Clover frames by using
the Clover stack as its machine stack. Any native-visible `Value` arguments are
loaded from managed frame slots or passed through ABI registers before the stack
switch.

## Frame Layout Implications

The interpreted frame layout remains useful for interpreter calls, constructor
thunks, native thunk frames, native-to-managed wrappers, exception unwinding,
and future JIT materialization:

```text
higher addresses

    fp[padded_n_parameters + header_size - 1]   p0
    ...
    fp[4]                                      last param / padding
    fp[3]                                      interpreter return PC
    fp[2]                                      interpreter return code object
    fp[1]                                      return pc for JITed code
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

`fp[1]` remains the return pc slot for JITed code, but it should not be treated
as a native LR slot for arbitrary C++ code. Native continuations and
stack-switch records should live in explicit transition metadata owned by the
native/managed boundary.

## Implemented Uses

The first migrated native methods and builtins are:

- `str.__str__`
- `str.__add__`
- `range`

Tests cover direct native thunk calls for arities 0, 1, 2, and 3, the
`ReturnOrRaiseException` thunk shape, native marker-to-exception unwinding, the
string method cases, `range`'s defaulted three-argument native thunk, Clover
frame frontier updates, native boundary returns, Clover function entry adapter
wrappers, and startup boundary returns.

Clover function entry adapter wrappers are generated and cached by positional
arity. `ReturnToNative` and `ReturnPendingExceptionToNative` are implemented for
boundary wrappers. The Clover frame frontier is initialized to a terminated
sentinel frame and set by `Halt`, by native boundary returns, and by the
fixed-arity `CallNative0`/`CallNative1`/`CallNative2`/`CallNative3` interpreter
opcodes.

## Remaining Work

1. [x] Build/cache Clover function entry adapters by positional arity.
   Keep raw code-object entry as a separate prepared `CallCodeObject` wrapper
   path for module startup and similar non-`Function` code.
2. [x] Add fixed-arity `ThreadState::call_clovervm_function` overloads backed by the
   matching arity wrappers.
3. [x] Switch startup code-object entry to the native boundary return
   convention. Do not add a general raw `CodeObject` native-call API until there
   is a concrete runtime entry point that needs it.
4. [ ] Design and implement the packed tuple/vector native convention for true
   variadic native callables.
5. [ ] Add specialized interpreter or JIT fast paths for trivial native thunk code
   objects when measurements justify it.
6. [ ] Add a separate special-method native-call API once lookup and binding
   semantics are ready.
7. [ ] Design the JIT managed-to-native transition ABI:
   - where managed continuation state lives
   - how live roots are published when they are not already materialized in
     Clover frame slots
   - how the transition switches from the Clover stack to the native stack
   - how `Value::exception_marker()` returns resume managed exceptional unwind
8. [ ] Continue converting old C++ exception paths to pending-exception/sentinel
   results before JIT/native stack-switch bridges or any other boundary where
   C++ unwinding would cross manually switched stack state. Until then, those
   old throws are panic plumbing, not the native boundary contract.
9. [ ] Keep `CallNative0`/`CallNative1`/`CallNative2`/`CallNative3` as the portable
   interpreter path. A JIT fast path may bypass the bytecode thunk, but it must
   preserve the same arity, root, and exception contracts.

## Invariants

- Fixed-arity native callables are ordinary `Function` objects with managed
  thunk `CodeObject`s.
- Native-to-managed calls initially target `TValue<Function>` and use
  `CallSimple` so defaults, varargs, arity checks, and constructor thunks remain
  in the managed call path.
- The primary native-to-managed function API is fixed-arity overloads. A
  pointer-plus-count fallback may exist later, but should not force common call
  sites to materialize an argument array.
- Method lookup is not part of `call_clovervm_function`; special-method calls
  get a separate API.
- Startup code-object entry is separate from native-to-`Function` calls. Module
  code objects are not `Function`s and use prepared `CallCodeObject` entry.
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
- `Halt` and `ReturnToNative` intentionally diverge: `Halt` exits the current
  interpreter invocation without popping a caller frame, while `ReturnToNative`
  pops a linked native-call wrapper and saves the restored live managed fp.
  Startup entry uses `ReturnToNative`, not `Halt`.
- Native/C boundaries are not first-order managed unwinder frames; managed
  thunks and transition continuations adapt native results back into the VM
  exception model.
- Completed native-to-managed wrapper frames are popped before returning to
  native and must not remain live as GC roots.
