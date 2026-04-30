# Native Function Thunks

## Goal

Native C++ functions should be callable through the normal `Function` object and
interpreter frame path. The call site should not need a broad "is this a
BuiltinFunction?" branch for every native implementation detail.

The transition is incremental:

- fixed-arity native functions are already represented as ordinary `Function`
  objects with tiny thunk `CodeObject`s
- variable-arity builtins still use `BuiltinFunction` for now
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

where `N` is currently `0`, `1`, or `2`. The operand indexes into the code
object's native target table.

Native targets are stored in [src/code_object.h](../src/code_object.h) as an
untagged union:

```cpp
using NativeFunction0 = Value (*)();
using NativeFunction1 = Value (*)(Value);
using NativeFunction2 = Value (*)(Value, Value);

union NativeFunctionTarget
{
    NativeFunction0 fixed0;
    NativeFunction1 fixed1;
    NativeFunction2 fixed2;
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
  CallNative0/1/2 reads p0, p1, ...
  calls the C++ target
  stores the returned Value in the accumulator
  Return leaves the thunk like an ordinary bytecode function
```

The thunk reads arguments directly from the interpreter frame. No argument array
or tuple is allocated for fixed arity.

Native callbacks no longer receive `ThreadState *`. Code that needs thread
state should use the TLS-backed helpers such as `active_thread()` or wrappers
like `make_object_value<T>(...)`.

## Why BuiltinFunction Still Exists

`BuiltinFunction` remains as a transitional representation for variable-arity
native callables such as `range`.

The fixed-arity thunk path deliberately does not solve variable arity by passing
`argc` through to the native target. Python functions do not receive a dynamic
argument count either; callers normalize variable positional arguments into an
object-level representation such as a tuple. The native equivalent should follow
that shape when we add the next convention.

Likely next conventions:

```text
CallNativeTuple     native(args_tuple)
CallNativeVector    native(args_tuple_or_span) after the representation is clear
CallNativeSlot      native slot shapes such as nb_add, tp_iternext, sq_length
```

The exact opcode names are still open for the variable-arity work. The fixed
opcodes are intentionally concrete: `CallNative0`, `CallNative1`, and
`CallNative2`.

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

Today the fixed-arity thunk `CodeObject` records `n_parameters`, and the opcode
assumes the frame already has the right number of arguments. This is enough for
the current internal uses and tests.

A proper arity check belongs on the Python-visible `Function` call boundary, not
inside `BuiltinFunction`. That refactor should cover bytecode functions and
native thunk functions together.

## Implemented Uses

The first migrated methods are string methods:

- `str.__str__`
- `str.__add__`

`str.__add__(other_str)` now exercises the fixed-arity native thunk path.
Passing a non-string currently raises `UnimplementedError`, which is the desired
shape for later binary-operator fallback work.

Tests cover direct native thunk calls for arities 0, 1, and 2, plus the string
method cases.

## Remaining Work

1. Migrate more fixed-arity native methods to `make_native_function()`.
2. Add function-level arity checking for bytecode and native functions.
3. Design and implement the packed variable-arity native convention.
4. Move `range` and any other variable-arity callables off `BuiltinFunction`.
5. Retire the `BuiltinFunction` object and the remaining call-site branches.
6. Add native exception normalization through thunk return adapters.
7. Add specialized interpreter or JIT fast paths for trivial native thunk code
   objects when measurements justify it.
8. Later, experiment with calling native targets directly from the interpreter
   stack, including inline assembly where that becomes useful.

## Invariants

- Fixed-arity native callables are ordinary `Function` objects with thunk
  `CodeObject`s.
- Native target pointers live in `CodeObject::native_function_targets`, not in
  `constant_values`.
- `NativeFunctionTarget` is untagged; the opcode determines the calling
  convention.
- Native callbacks do not receive `ThreadState *`; thread state is available
  through TLS.
- `BuiltinFunction` is transitional and should only remain for conventions that
  have not yet moved to native thunks.
