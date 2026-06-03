# CloverVM Function Calling Convention

This note documents the calling convention currently implemented by CloverVM's bytecode compiler and interpreter. It is based on the runtime behavior in [src/interpreter.cpp](../src/interpreter.cpp) and the lowering logic in [src/codegen.cpp](../src/codegen.cpp).

## Executive Summary

- CloverVM uses a register-based bytecode.
- The interpreter also carries key VM state in native function arguments/registers: `fp`, `pc`, `accumulator`, `dispatch`, and `code_object`.
- Call arguments are laid out in a contiguous register window in the caller.
- Entering a function does not copy arguments into a separate argument array. Instead, the interpreter moves `fp` so the existing call window becomes the callee's frame.
- Internal forwarding thunks can enter an explicit `CodeObject` with
  `CallCodeObject` after preparing the call window themselves.
- Fixed-arity native functions use the same managed frame path: a native thunk
  `CodeObject` reads `p0`, `p1`, ... directly and calls a C++ target on the
  native stack.
- The stack grows toward lower addresses.
- Parameters live at positive offsets from `fp`.
- Locals and temporaries live at negative offsets from `fp`.
- A small frame header is kept around `fp`; current call paths store:
  - interpreter return program counter at `fp[3]`
  - interpreter return code object at `fp[2]`
  - return pc for JITed code at `fp[1]`
  - previous frame pointer at `fp[0]`

This layout is intended to be friendly to future compiled managed code while
still having enough metadata to jump back to the interpreter when needed. It is
not a native C++ frame layout: arbitrary native frames must not be placed on the
Clover stack.

Native function thunks build on the same layout. A `CallIntrinsic0`,
`CallIntrinsic1`, `CallIntrinsic2`, or `CallIntrinsic3` opcode runs inside an ordinary
managed function frame, reads fixed positional parameters from the `p`
registers, calls the native C++ target on the native stack, and returns through
`ReturnOrRaiseException`. See
[native-managed-boundaries.md](native-managed-boundaries.md) for native thunk
and native-to-managed call boundary planning.

For native AArch64 stack-frame convention details, use LLVM's frame lowering
implementation as the reference point:
[AArch64FrameLowering.cpp](https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/AArch64/AArch64FrameLowering.cpp).

## Native Interpreter State

The CloverVM frame layout is only part of the calling convention. The threaded interpreter also passes its live execution state as native function parameters:

```cpp
#define PARAMS                                                                 \
    Value *fp, const uint8_t *pc, Value accumulator, void *dispatch,           \
        CodeObject *code_object
#define ARGS fp, pc, accumulator, dispatch, code_object
```

from [src/interpreter.cpp](../src/interpreter.cpp).

Conceptually:

- `fp` is the current CloverVM frame pointer
- `pc` is the current bytecode program counter
- `accumulator` is the current accumulator value
- `code_object` is the current bytecode object
- `dispatch` is the dispatch table pointer

With the `MUSTTAIL` threaded-dispatch style in the interpreter, these values are intended to stay live in native calling-convention registers across opcode handlers, rather than being reloaded from the CloverVM stack frame on each step. In particular, the dispatch table pointer is kept permanently live as part of that native register state.

## Relevant Definitions

In [src/code_object.h](../src/code_object.h):

```cpp
static constexpr int32_t FrameHeaderSizeAboveFp = 4;
static constexpr int32_t FrameHeaderSizeBelowFp = 0;
static constexpr int32_t FrameHeaderSize =
    FrameHeaderSizeAboveFp + FrameHeaderSizeBelowFp;
```

Each `CodeObject` tracks:

- `n_parameters`
- `n_locals`
- `n_temporaries`

Parameter slots are physically padded to the ABI alignment. Since `Value` is 8
bytes and the ABI alignment is 16 bytes, this currently means rounding slot
counts up to an even number:

```cpp
constexpr uint32_t round_up_to_abi_alignment(uint32_t value)
{
    return (value + 1u) & ~1u;
}

uint32_t get_padded_n_parameters() const
{
    return round_up_to_abi_alignment(n_parameters);
}

uint32_t get_padded_n_ordinary_below_frame_slots() const
{
    return round_up_to_abi_alignment(n_locals + n_temporaries);
}
```

and reports total register storage as:

```cpp
uint32_t get_n_registers() const
{
    return n_parameters + n_temporaries + n_locals;
}
```

Codegen reserves any parameter padding and the header slots in function and
class scopes before collecting locals:

- function bodies: [src/codegen.cpp](../src/codegen.cpp)
- class bodies: [src/codegen.cpp](../src/codegen.cpp)

## Register Naming and Placement

The bytecode printer exposes the register naming convention in [src/code_object_print.h](../src/code_object_print.h):

- `p0`, `p1`, ... are parameter registers
- `r0`, `r1`, ... are local/temporary registers

The encoding rule is in [src/code_object.h](../src/code_object.h):

```cpp
int8_t encode_reg(uint32_t reg)
{
    return get_padded_n_parameters() - 1 + FrameHeaderSizeAboveFp - reg;
}
```

Combined with interpreter access through `fp[reg]`, this gives the physical layout:

```text
higher addresses

    fp[padded_n_parameters + 3]   p0
    fp[padded_n_parameters + 2]   p1
    ...
    fp[5]                         p(n-1) if n is even, padding if n is odd
    fp[4]                         p(n-1) if n is odd, otherwise last param
    fp[3]                  interpreter return program counter
    fp[2]                  interpreter return code object
    fp[1]                  return pc for JITed code
fp->fp[0]                  previous frame pointer
    fp[-1]                 r0
    fp[-2]                 r1
    fp[-3]                 r2
    ...

lower addresses
```

So:

- parameters are above `fp`
- locals/temporaries are below `fp`
- larger logical register numbers move downward in memory

## Call Argument Layout

### Simple calls

For a direct call like `f(x, y)`, codegen reserves one ABI-aligned temporary
span for the positional call arguments:

- the callable itself lives outside the call argument span
- the first temporary in the span is the first user argument
- the next temporary in the span is the second user argument
- and so on

For a zero-argument call, codegen still reserves a one-register call argument
span and emits an argument count of zero. That anchor gives the callee a
well-defined place to append default arguments before entering the frame.

### Method calls

For direct method-call syntax such as `obj.method(x)`, the receiver and
explicit arguments occupy one contiguous temporary call argument span:

- receiver
- user argument 0
- user argument 1
- ...

At runtime, `CallMethodAttrPositional` resolves the attribute in call context. If the
cached or resolved plan binds the receiver as `self`, the handler writes `self`
into the receiver register and uses that register as the first argument. If no
implicit receiver is needed, the handler copies the explicit arguments up by
one slot so the first explicit argument occupies the receiver register.

When `self` is inserted, the callee sees:

- `p0 = self`
- `p1 = first user arg`
- ...

When no `self` is inserted, the callee sees:

- `p0 = first user arg`
- `p1 = second user arg`
- ...

## Function Entry

The core transition for function calls is shared by `op_call_positional` and
`op_call_method_attr_positional` in [src/interpreter.cpp](../src/interpreter.cpp):

```cpp
int32_t new_fp_reg = first_arg_reg - round_up_to_abi_alignment(n_args) + 1 -
                     FrameHeaderSizeAboveFp;
Value *new_fp = fp + new_fp_reg;

new_fp[0].as.ptr = (Object *)fp;
new_fp[2] = Value::from_oop(code_object);
new_fp[3].as.ptr = (Object *)pc;

fp = new_fp;
code_object = fun.get_ptr<Function>()->code_object.extract();
pc = code_object->code.data();
```

This is the key idea: the interpreter does not allocate/copy a fresh argument block. It reinterprets the caller's already-laid-out call window as the callee frame.

`CallPositional` is the public callable path: it resolves a callable's `Function`
semantics, including selecting the `ordinary_code_object` and applying any
arity/default adaptation. Future call-site policies may choose an alternate code
object such as `stop_returning_code_object` when available. Some VM-generated
thunks instead need to forward into already selected code. Those thunks can use
`CallCodeObject`:

```text
CallCodeObject target_code_object, first_arg, argc
```

`CallCodeObject` has a narrower contract:

- the target is an explicit `CodeObject` value
- the surrounding thunk has already prepared the argument/register window
- no `Function` entry selection, default handling, or callable protocol lookup
  happens at the opcode
- the interpreter enters exactly the supplied `CodeObject` using the ordinary
  frame setup/return machinery

This keeps constructor thunks and future protocol adapters from having to say
"call this `Function`, but ignore the normal `Function` call behavior".

Class bodies are adjacent but already have their own direct-code path:
`CreateClass` loads a class body `CodeObject`, prepares the class-body frame,
and enters that code object internally. It does not need to be expressed through
`CallCodeObject` unless the implementation later chooses to unify those paths.

### Why `new_fp` is computed this way

At the call site:

- `first_arg_reg` points at the first argument slot, or at the reserved
  zero-argument anchor
- the `n_args` argument values sit at that slot and the slots immediately below
  it

So:

```text
first argument slot  = fp[first_arg_reg]
last argument slot   = fp[first_arg_reg - n_args + 1]
new fp               = fp[first_arg_reg - round_up_to_abi_alignment(n_args) + 1 - 4]
```

The extra `4` is `FrameHeaderSizeAboveFp`, which places the new `fp` so that:

- the last parameter lands above any odd-argument padding
- the first parameter lands at `fp[round_up_to_abi_alignment(n_args) + 3]`
- header words occupy `fp[3]`, `fp[2]`, `fp[1]`, and `fp[0]`

## Frame Diagrams

The following shows a normal function frame for a function with two parameters
and two local/temporary registers. Even parameter counts need no padding:

```text
stack grows downward

    higher addresses
        |
        v

    fp[5]   p0   first parameter
    fp[4]   p1   last parameter
    fp[3]        interpreter return PC
    fp[2]        interpreter return code object
    fp[1]        return pc for JITed code
fp  fp[0]        previous frame pointer
    fp[-1]  r0   first local/temporary
    fp[-2]  r1

    lower addresses
```

The following shows a normal function frame for a function with three
parameters and two local/temporary registers. Odd parameter counts are padded to
the next even physical slot count:

```text
stack grows downward

    higher addresses
        |
        v

    fp[7]   p0   first parameter
    fp[6]   p1
    fp[5]   p2   last parameter
    fp[4]        parameter padding
    fp[3]        interpreter return PC
    fp[2]        interpreter return code object
    fp[1]        return pc for JITed code
fp  fp[0]        previous frame pointer
    fp[-1]  r0   first local/temporary
    fp[-2]  r1

    lower addresses
```

The caller-to-callee transition for `f(x, y)` looks like this:

```text
Before call in caller frame:

    ... caller locals/temps ...
    [callable = f]
    [aligned call arg 0 = x]
    [call arg 1 = y]
    ...

After `new_fp = fp + first_arg_reg - round_up_to_abi_alignment(n_args) + 1 - 4`:

    new_fp[5]  p0 = x
    new_fp[4]  p1 = y
    new_fp[3]      interpreter return PC
    new_fp[2]      interpreter return code object
    new_fp[1]      return pc for JITed code
    new_fp[0]      previous fp
    new_fp[-1]     r0
    ...
```

## Return Path

Returning is the inverse operation. `op_return` in [src/interpreter.cpp](../src/interpreter.cpp) restores the caller context from the frame header:

```cpp
pc = (const uint8_t *)fp[3].as.ptr;
code_object = fp[2].get_ptr<CodeObject>();
fp = (Value *)fp[0].as.ptr;
```

The return value itself is carried in the interpreter accumulator, not in a stack slot.

So the full convention is:

- arguments flow into the callee through frame slots
- return values flow back in the accumulator
- return control state is restored from `fp[0]`, `fp[2]`, and `fp[3]`

## Top-Level Entry

The module entrypoint is special. `ThreadState::run` starts interpretation at a fixed location near the end of the thread stack:

```cpp
return run_interpreter(&stack[stack.size() - 1024], obj, 0);
```

from [src/thread_state.cpp](../src/thread_state.cpp).

`ThreadState::run` passes a Clover frame pointer into the interpreter. It does
not switch the machine stack pointer. The interpreter itself runs on the native
C++ stack and mutates Clover frames explicitly through `fp`.

Top-level module code is not expected to execute `Return`; the parser/codegen reject `return` outside a function. So the normal saved-caller metadata contract applies to nested function/class execution, not to the initial module entry frame.

## Stack Ownership

The calling convention describes managed Clover frames, not arbitrary native
stack frames.

```text
Clover stack:
  VM-managed frame headers, parameters, locals, temporaries, temporary call
  argument spans, interpreted frame materialization, and future JIT frames

native stack:
  threaded interpreter implementation frames, C++ runtime helpers, native
  builtin functions, host ABI spills, and return-address bookkeeping
```

Native functions may receive `Value` arguments loaded from Clover frame slots,
but their own machine stack activity must remain on the native stack. This keeps
the Clover stack understandable to root scanning, exception unwinding, and
future deoptimization metadata.

## Practical Rules

If you are reasoning about CloverVM calls, the safest mental model is:

1. The callable lives separately from the temporary call argument span.
2. The call argument span is ABI-aligned and must be the topmost live
   temporary range when the call opcode is emitted.
3. The interpreter moves `fp` below the argument window padded up to the ABI alignment, so those argument cells become `p0`, `p1`, ...
4. `fp[0]`, `fp[2]`, and `fp[3]` hold the caller state needed by `Return`.
5. Locals/temporaries for the callee start at `r0 = fp[-1]`.
6. The accumulator carries the return value across the `Return` instruction.
7. Native C++ callees run on the native stack; the Clover stack contains only
   VM-managed frame/register state.

## Source Pointers

- Call lowering: [src/codegen.cpp](../src/codegen.cpp)
- Frame/header constants and register encoding: [src/code_object.h](../src/code_object.h)
- Register names in disassembly: [src/code_object_print.h](../src/code_object_print.h)
- Call and return execution: [src/interpreter.cpp](../src/interpreter.cpp)
- Top-level interpreter entry: [src/thread_state.cpp](../src/thread_state.cpp)
