# Native Function Thunks

## Goal

Native C++ functions should be callable through the normal `Function` object and
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

## Call Path

For fixed-arity native functions, the generic call path sees a normal
`Function`:

```text
caller
  CallSimple sets up a managed callee frame on the Clover stack

native thunk frame
  CallNative0/1/2/3 reads p0, p1, ...
  calls the C++ target on the native stack
  stores the returned Value in the accumulator
  ReturnOrRaiseException either returns normally or enters managed unwind
```

The thunk reads arguments directly from the managed frame. No argument array or
tuple is allocated for fixed arity.

Native callbacks no longer receive `ThreadState *`. Code that needs thread
state should use the TLS-backed helpers such as `active_thread()` or wrappers
like `make_object_value<T>(...)`.

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

Therefore native function thunks deliberately call C++ targets from the native
stack. The interpreter already has this shape: it runs on the native stack while
mutating Clover frames through explicit `fp` accesses. Future JIT code may use
the Clover stack as its active managed stack, but it must switch to the native
stack before calling C++ runtime helpers or native functions.

The boundary rule is:

```text
managed-to-managed:
  may stay on the Clover stack

managed-to-native:
  publish/materialize managed roots as needed
  switch to the native stack
  call C++/native code
  return Value, or exception_marker with pending exception state
  switch back to managed execution
```

Interpreted bytecode is already on the native stack, so interpreted
`CallNativeN` does not need a machine-stack switch. It just reads the managed
frame slots and calls the target.

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

C++ exceptions are still temporary outer panic plumbing in some old paths, but
they must not become the native-call convention. In particular, future
handwritten stack-switch bridges must not allow C++ exceptions to unwind across
the bridge. Native functions used from those paths must return through the
pending-exception/sentinel convention.

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

The ordinary thunk normalizes native failure and stop-returning completion into
ordinary managed exceptional unwind. The stop-returning thunk leaves its own
protocol completion as pending `StopIteration` plus
`Value::exception_marker()`, while marker plus any other pending exception enters
managed exceptional unwind.

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

Tests cover direct native thunk calls for arities 0, 1, 2, and 3, the
`ReturnOrRaiseException` thunk shape, native marker-to-exception unwinding, the
string method cases, and `range`'s defaulted three-argument native thunk.

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
thunks, native thunk frames, exception unwinding, and future JIT materialization:

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

`fp[1]` remains the return pc slot for JITed code, but it
should not be treated as a native LR slot for arbitrary C++ code. Native
continuations and stack-switch records should live in explicit transition
metadata owned by the managed-to-native bridge.

## Remaining Work

1. Design and implement the packed tuple/vector native convention for true
   variadic native callables.
2. Continue converting old C++ exception paths to pending-exception/sentinel
   results where they may be reached by native functions or future transition
   stubs.
3. Add specialized interpreter or JIT fast paths for trivial native thunk code
   objects when measurements justify it.
4. Design the JIT managed-to-native transition ABI:
   - where managed continuation state lives
   - how live roots are published when they are not already materialized in
     Clover frame slots
   - how the transition switches from the Clover stack to the native stack
   - how `Value::exception_marker()` returns resume managed exceptional unwind
5. Keep `CallNative0`/`CallNative1`/`CallNative2`/`CallNative3` as the portable
   interpreter path. A JIT fast path may bypass the bytecode thunk, but it must
   preserve the same arity, root, and exception contracts.

## Invariants

- Fixed-arity native callables are ordinary `Function` objects with managed
  thunk `CodeObject`s.
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
- Native/C boundaries are not first-order managed unwinder frames; managed
  thunks and transition continuations adapt native results back into the VM
  exception model.
