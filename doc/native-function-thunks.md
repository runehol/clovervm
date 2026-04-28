# Native Function Thunks

## Goal

Unify Python bytecode functions and native/builtin functions under the existing
`Function` object and frame calling convention.

Today, native builtin calls tend to force special cases into call opcodes. That
is awkward for performance and semantics:

- `CallSimple` has to notice builtin function objects and route them through a
  native slow path.
- Native callees speak C-style result conventions, where failure is represented
  by `NULL`, `-1`, or another sentinel plus pending exception state.
- Ordinary bytecode call sites should not accidentally receive
  `Value::exception_marker()` unless the call site was explicitly set up for
  `ViaResult`.
- CPython C API compatibility requires representation adaptation at the native
  boundary, such as packing SMIs into `PyLong`-compatible objects.

The proposed design removes the separate builtin call path. Native functions are
ordinary `Function` objects whose `CodeObject` is a tiny bytecode thunk.

## Core Model

There is no separate `BuiltinFunction` call target. A native callable is a
regular `Function`:

```text
Function
  code_object = native thunk CodeObject
```

The thunk code object contains a small adapter program. A typical thunk is:

```text
NativeCall <address-or-descriptor>, <calling-convention>
NativeReturn
```

or, for common cases, specialized native-call opcodes:

```text
NativeCallNoArgs <address>
NativeCallOneArg <address>
NativeCallVector <address>
NativeCallKeywords <address>
NativeReturn
```

`CallSimple` does not need to know whether the target is Python-authored
bytecode or a native builtin. It sets up a normal callee frame and enters the
callee's code object:

```text
resolve callable
verify Function
move fp so the call window becomes the callee frame
tag the callee return target as BytecodeViaUnwind or BytecodeViaResult
jump to function.code_object->code.data()
```

The native thunk is just bytecode executed inside that normal frame.

## Thunk Responsibilities

The thunk is the boundary between CloverVM's calling convention and native/C API
calling conventions.

On entry, the thunk sees a normal CloverVM frame:

```text
a0, a1, ...       arguments
fp[0]             caller fp
fp[-1]            tagged caller return target
fp[-2]            caller return pc
accumulator       scratch/result channel
```

The `NativeCall` opcode adapts those frame arguments to the target native
signature. Depending on the thunk kind, that may include:

- reading fixed positional arguments directly from `a0`, `a1`, ...
- building a temporary vectorcall argument array
- packing positional arguments into a tuple for legacy APIs
- building or passing keyword metadata
- inserting or exposing `self` for method descriptors
- checking arity and reporting Python exceptions on mismatch
- converting CloverVM `Value`s to CPython-compatible `PyObject *` values
- normalizing ownership and borrowed/new reference conventions

On return, the thunk adapts the native result back to the VM:

```text
native success:
  convert native result to a CloverVM Value
  store it in the accumulator
  continue to NativeReturn

native failure:
  leave the pending Python exception on ThreadState / PyErr state
  store Value::exception_marker() in the accumulator
  continue to NativeReturn
```

Native thunks must not use ordinary `Return`. `Return` treats the accumulator as
a normal value and would pass `Value::exception_marker()` to an ordinary caller's
next instruction. `NativeReturn` is the adapter that interprets the accumulator:

```text
accumulator is a normal value:
  return it like ordinary Return

accumulator is Value::exception_marker():
  inspect the thunk frame's tagged return target
  BytecodeViaUnwind -> promote the pending exception into table unwinding
  BytecodeViaResult -> return the marker to the caller's saved return pc
  Native -> return the native/C sentinel to the outer native boundary
```

This keeps native sentinel handling inside the thunk while preserving the normal
VM rule that only `ViaResult` callers receive `Value::exception_marker()`.

## Exception Delivery

Native thunks compose with the two return modes described in
[lazy-exceptions-and-tracebacks.md](lazy-exceptions-and-tracebacks.md).

The native function itself speaks native sentinel convention:

```text
success:
  return PyObject* / int success / native result

failure:
  set pending Python exception
  return NULL / -1 / native sentinel
```

The thunk converts native failure into a pending exception marker, then lets
`NativeReturn` adapt that marker to the caller's return mode:

```text
NativeCall fails:
  pending exception is already on ThreadState, or is created from native error
  accumulator = Value::exception_marker()
  continue to NativeReturn
```

`NativeReturn` then applies the ordinary return target rules:

```text
BytecodeViaUnwind:
  promote the pending exception into table unwinding in the bytecode caller

BytecodeViaResult:
  restore the bytecode caller
  put Value::exception_marker() in the accumulator
  dispatch the saved return pc normally

Native:
  return the native/C sentinel to the outer native boundary
```

This preserves the key invariant: only call sites that requested `ViaResult`
receive `Value::exception_marker()` in their accumulator. Ordinary bytecode calls
to native functions see either a normal return value or ordinary exception
unwinding.

For example:

```text
ordinary CALL to native len:
  caller return target = BytecodeViaUnwind
  native failure continues table unwinding in the caller

FOR_ITER1 calling native iterator __next__:
  caller return target = BytecodeViaResult
  native StopIteration failure returns Value::exception_marker() to FOR_ITER2
```

## CPython C API Adaptation

The thunk is also the representation membrane for CPython C API compatibility.

CloverVM can keep its internal values compact:

```text
SMI
CLInt
String
Object subclasses
borrowed Value / TValue handles
```

The native thunk converts only when crossing into a native convention:

```text
SMI -> PyLong-compatible object
CLInt -> PyLong-compatible object
String -> PyUnicode-compatible object
Clover object -> PyObject-compatible wrapper or direct PyObject view
Value span -> PyObject* argv
keyword registers -> kwnames / kwargs representation
```

The return direction is symmetrical:

```text
PyObject* result -> CloverVM Value
NULL + PyErr set -> pending exception on ThreadState
borrowed reference -> retain if the VM must keep it
new reference -> adopt and release according to convention
```

Keeping this work in thunks prevents CPython compatibility from leaking into
generic bytecode call dispatch, attribute lookup, descriptor calls, and iterator
opcodes.

## Calling Convention Families

Different native targets should use different thunk shapes. CPython already has
several conventions, and CloverVM should avoid forcing them through one generic
adapter on hot paths.

Useful initial thunk kinds:

```text
NoArgs:
  native(self)

OneArg:
  native(self, arg)

FastVector:
  native(callable, args, nargsf, kwnames)

VarArgs:
  native(self, args_tuple)

VarArgsKeywords:
  native(self, args_tuple, kwargs_dict)

Slot:
  native slot such as tp_iternext, nb_add, sq_length

GetterSetter:
  descriptor getter/setter API shape
```

The callable chooses the native thunk kind. The call site chooses the exception
delivery mode:

```text
target entry:
  bytecode function
  native noargs thunk
  native vectorcall thunk
  native slot thunk

caller return mode:
  BytecodeViaUnwind
  BytecodeViaResult
  Native
```

These axes should remain independent.

## Encoding Native Targets

The thunk bytecode can encode the native target directly:

```text
NativeCallVector <native-address>, <flags>
NativeReturn
```

If direct raw addresses in bytecode become inconvenient for relocation,
debugging, serialization, or GC metadata, the operand can instead be a native
descriptor index:

```text
NativeCallVector <native-descriptor-index>
NativeReturn
```

The descriptor would contain:

```cpp
struct NativeCallDescriptor
{
    void *address;
    NativeCallingConvention convention;
    NativeOwnershipPolicy ownership;
    uint16_t min_args;
    uint16_t max_args;
    // Optional: name, debug metadata, safepoint/GC policy.
};
```

The design does not require choosing between these immediately. The important
property is that the generic call opcode enters a normal `Function` and the thunk
owns native adaptation.

## Frame And Stack Effects

Native thunks intentionally introduce a normal CloverVM frame. This removes the
need for `CallSimple` to host native slow paths and keeps all return-mode logic
in one place.

For a native function call:

```text
caller frame
  CallSimple sets up callee frame

native thunk frame
  NativeCall adapts args and calls native target
  NativeReturn returns native success value
  NativeReturn adapts native failure markers to the caller's return mode
```

The extra frame is semantically useful:

- traceback and lazy traceback machinery see a normal frame boundary
- return target tags work the same as for bytecode functions
- native failures have one place to convert markers into VM unwinding or
  `ViaResult` marker returns
- future JIT work can inline or erase trivial thunk frames without changing
  interpreter semantics

The interpreter should still be able to specialize common thunk code objects.
For example, a cached call site may learn that its target code object is exactly
`NativeCallOneArg; NativeReturn` and jump to a specialized handler. That is an
optimization of the unified model, not a separate builtin calling convention.

## Interaction With Iteration

The iterator protocol benefits directly from thunked native calls.

For a Python-authored iterator:

```text
FOR_ITER1 -> __next__ bytecode function
StopIteration escapes __next__
BytecodeViaResult returns Value::exception_marker() to FOR_ITER2
FOR_ITER2 consumes StopIteration
```

For a native iterator:

```text
FOR_ITER1 -> native __next__ thunk Function
NativeCall invokes tp_iternext or equivalent
NULL + StopIteration / no item becomes pending StopIteration or exhaustion state
NativeReturn adapts the pending exception marker
BytecodeViaResult returns Value::exception_marker() to FOR_ITER2
FOR_ITER2 consumes StopIteration
```

An ordinary user-visible call remains ordinary:

```text
it.__next__()
  caller return target = BytecodeViaUnwind
  StopIteration from native or bytecode __next__ propagates as an exception
```

Thus `StopIteration` remains special only at protocol call sites, not globally
special to native functions.

## Staging

A practical implementation order:

1. Introduce native thunk code objects with minimal `NativeCall` and
   `NativeReturn` opcodes.
2. Represent simple builtins as `Function` objects pointing at thunk code
   objects.
3. Keep one initial native calling convention, such as vectorcall-like
   `(args, nargs)` or a simple fixed-arity convention.
4. Change `CallSimple` to treat builtin thunks as normal functions, removing the
   builtin-specific slow path.
5. On native failure, set pending exception state and leave
   `Value::exception_marker()` in the accumulator.
6. Teach `NativeReturn` to adapt exception markers using tagged return targets:
   `BytecodeViaUnwind`, `BytecodeViaResult`, and `Native`.
7. Add CPython C API representation adapters inside native thunk opcodes.
8. Split hot conventions into specialized opcodes or descriptor kinds:
   no-args, one-arg, vectorcall, slots, getter/setter.
9. Add interpreter/JIT fast paths that recognize trivial native thunks and inline
   the adapter when profitable.

## Invariants

- Native callables are ordinary `Function` objects with thunk `CodeObject`s.
- Generic bytecode call opcodes do not branch on `BuiltinFunction`.
- Native calling convention adaptation happens inside thunk bytecode, not in
  `CallSimple`.
- CPython C API representation adaptation happens at the native thunk boundary.
- Native failure leaves the real pending exception on `ThreadState`.
- Native failure leaves `Value::exception_marker()` in the accumulator for
  `NativeReturn`; ordinary `Return` must not be used by native thunks.
- `NativeReturn` converts exception markers to caller table unwinding,
  `ViaResult` marker return, or native/C sentinel return.
- Only `ViaResult` callers receive `Value::exception_marker()` in the
  accumulator.
- The callable's native convention and the caller's exception delivery mode are
  independent axes.
- Thunk frames are semantic frames in the interpreter, even if a future JIT
  chooses to inline or erase them.
