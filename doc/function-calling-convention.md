# CloverVM Function Calling Convention

This note documents the calling convention currently implemented by CloverVM's
bytecode compiler and interpreter. It is based on the runtime behavior in
[src/interpreter.cpp](../src/interpreter.cpp) and the lowering logic in
[src/codegen.cpp](../src/codegen.cpp).

## Executive Summary

- CloverVM uses a register-based bytecode.
- The interpreter also carries key VM state in native function
  arguments/registers: `fp`, `pc`, `accumulator`, `dispatch`, and
  `code_object`.
- Call arguments are laid out in a contiguous register window in the caller.
- Entering a function does not copy arguments into a separate argument array.
  Instead, the interpreter moves `fp` so the existing call window becomes the
  callee's frame.
- The stack grows toward higher addresses.
- Parameters live at negative offsets from `fp`.
- Locals and temporaries live at positive offsets from `fp`.
- A small frame header is reserved around `fp`; current call paths store:
  - return program counter at `fp[-1]`
  - previous frame pointer at `fp[0]`
  - return code object at `fp[1]`
- One reserved header slot at `fp[-2]` exists today but is not populated by the
  current call paths.

## Native Interpreter State

The CloverVM frame layout is only part of the calling convention. The threaded
interpreter also passes its live execution state as native function parameters:

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

With the `MUSTTAIL` threaded-dispatch style in the interpreter, these values are
intended to stay live in native calling-convention registers across opcode
handlers, rather than being reloaded from the CloverVM stack frame on each step.
In particular, the dispatch table pointer is kept permanently live as part of
that native register state.

## Relevant Definitions

In [src/code_object.h](../src/code_object.h):

```cpp
static constexpr int32_t FrameHeaderSizeAboveFp = 2;
static constexpr int32_t FrameHeaderSizeBelowFp = 2;
static constexpr int32_t FrameHeaderSize =
    FrameHeaderSizeAboveFp + FrameHeaderSizeBelowFp;
```

`AboveFp` and `BelowFp` are physical directions:

- above means positive `fp` offsets
- below means negative `fp` offsets

They do not imply parameter-side or local-side ownership. In the current
upward-growing layout, locals/temporaries are above `fp` and parameters are
below `fp`.

Each `CodeObject` tracks:

- `n_parameters`
- `n_locals`
- `n_temporaries`

and reports total register storage as:

```cpp
uint32_t get_n_registers() const
{
    return n_parameters + n_temporaries + n_locals;
}
```

Codegen reserves the header slots in function and class scopes before
collecting locals:

- function bodies: [src/codegen.cpp](../src/codegen.cpp)
- class bodies: [src/codegen.cpp](../src/codegen.cpp)

## Register Naming and Placement

The bytecode printer exposes the register naming convention in
[src/code_object_print.h](../src/code_object_print.h):

- `a0`, `a1`, ... are argument/parameter registers
- `r0`, `r1`, ... are local/temporary registers

The encoding rule is in [src/code_object.h](../src/code_object.h):

```cpp
int8_t encode_reg(uint32_t reg)
{
    int32_t encoded =
        int32_t(reg) - int32_t(n_parameters) - FrameHeaderSizeBelowFp;
    assert(encoded == int8_t(encoded));
    return int8_t(encoded);
}
```

Combined with interpreter access through `fp[reg]`, this gives the physical
layout:

```text
higher addresses

    fp[4]                  r2
    fp[3]                  r1
    fp[2]                  r0
    fp[1]                  return code object
fp->fp[0]                  previous frame pointer
    fp[-1]                 return program counter
    fp[-2]                 reserved header slot
    fp[-3]                 a(n-1)
    ...
    fp[-n-1]               a1
    fp[-n-2]               a0

lower addresses
```

So:

- arguments/parameters are below `fp`
- locals/temporaries are above `fp`
- larger logical register numbers move upward in memory

The sign of the encoded register still divides the two sides:

- `encoded_reg < 0` means an argument/parameter slot
- `encoded_reg >= 0` means a local/temporary slot

## How Codegen Lays Out a Call

### Simple calls

For a direct call like `f(x, y)`, codegen emits:

1. the callable into a temporary base register
2. each argument into the next registers in sequence
3. `CallSimple base, n_args`

The lowering is in `codegen_function_call` in
[src/codegen.cpp](../src/codegen.cpp):

```cpp
TemporaryReg regs(this, 1 + args.size());

codegen_node(children[0], mode);
code_obj->emit_opcode_reg(source_offset, Bytecode::Star, regs + 0);

for(size_t i = 0; i < args.size(); ++i)
{
    codegen_node(args[i], mode);
    code_obj->emit_opcode_reg(source_offset, Bytecode::Star, regs + 1 + i);
}
code_obj->emit_opcode_reg_range(source_offset, Bytecode::CallSimple,
                                regs, args.size());
```

This means the caller's register window is:

```text
lower addresses

    callable
    arg0
    arg1
    ...

higher addresses
```

The logical order and physical order are the same.

### Method calls

For `obj.method(x)`, codegen reserves one extra slot:

- slot for the callable
- slot for `self`
- slots for user arguments

in [src/codegen.cpp](../src/codegen.cpp):

```cpp
TemporaryReg regs(this, 2 + args.size());
...
code_obj->emit_opcode_reg_constant_idx_reg(
    source_offset, Bytecode::LoadMethod, receiver_reg.reg,
    constant_idx, regs);
...
code_obj->emit_opcode_reg_range(
    source_offset, Bytecode::CallMethod, regs, args.size());
```

At runtime `LoadMethod` writes:

- callable at `fp[call_base_reg]`
- `self` at `fp[call_base_reg + 1]`

from [src/interpreter.cpp](../src/interpreter.cpp):

```cpp
fp[call_base_reg] = callable;
*reg_ptr(fp, call_method_self_reg(call_base_reg)) = self;
```

If the resolved method is not a normal function needing implicit `self`, `self`
remains `not_present`. `CallMethod` then treats `reg + 1` as the effective call
base, so the existing user arguments can stay where they already are. If `self`
is present, `CallMethod` keeps `reg` as the call base and increases the
effective arity by one so the callee sees:

- `a0 = self`
- `a1 = first user arg`
- ...

## Function Entry

The core transition for function calls is in `op_call_simple` and
`op_call_method` in [src/interpreter.cpp](../src/interpreter.cpp):

```cpp
Value *new_fp =
    fp + call_base_reg + int32_t(n_args) + FrameHeaderSizeBelowFp + 1;

initialize_frame_header(new_fp, fp, code_object, pc);

fp = new_fp;
code_object = fun.get_ptr<Function>()->code_object.extract();
pc = code_object->code.data();
```

This is the key idea: the interpreter does not allocate/copy a fresh argument
block. It reinterprets the caller's already-laid-out call window as the callee
frame.

### Why `new_fp` is computed this way

At the call site:

- `call_base_reg` points at the callable slot
- the `n_args` argument values sit immediately above that slot

So:

```text
callable slot        = fp[call_base_reg]
first argument slot  = fp[call_base_reg + 1]
last argument slot   = fp[call_base_reg + n_args]
new fp               = fp[call_base_reg + n_args + 3]
```

The extra `3` is `FrameHeaderSizeBelowFp + 1`, which places the new `fp` so
that:

- the first parameter lands at `fp[-n_args - 2]`
- the last parameter lands at `fp[-3]`
- header words occupy `fp[-2]`, `fp[-1]`, `fp[0]`, and `fp[1]`

For zero-argument calls, no argument cells are reinterpreted. The callable slot
remains below the callee's reserved/header area.

## Frame Diagram

The following shows a normal function frame for a function with three
parameters and two local/temporary registers:

```text
stack grows upward

    higher addresses
        ^
        |

    fp[3]   r1
    fp[2]   r0   first local/temporary
    fp[1]        return code object
fp  fp[0]        previous frame pointer
    fp[-1]       return PC
    fp[-2]       reserved header slot
    fp[-3]  a2   last parameter
    fp[-4]  a1
    fp[-5]  a0   first parameter

    lower addresses
```

The caller-to-callee transition for `f(x, y)` looks like this:

```text
Before call in caller frame:

    ... caller locals/temps ...
    [callable = f]
    [arg0 = x]
    [arg1 = y]
    ...

After `new_fp = fp + reg + n_args + 3`:

    new_fp[-4]  a0 = x
    new_fp[-3]  a1 = y
    new_fp[-2]      reserved
    new_fp[-1]      return PC
    new_fp[0]       previous fp
    new_fp[1]       return code object
    new_fp[2]   r0
    ...
```

## Return Path

Returning is the inverse operation. `op_return` in
[src/interpreter.cpp](../src/interpreter.cpp) restores the caller context from
the frame header:

```cpp
pc = (const uint8_t *)fp[-1].as.ptr;
code_object = fp[1].get_ptr<CodeObject>();
fp = (Value *)fp[0].as.ptr;
```

The return value itself is carried in the interpreter accumulator, not in a
stack slot.

So the full convention is:

- arguments flow into the callee through frame slots
- return values flow back in the accumulator
- return control state is restored from `fp[-1]`, `fp[0]`, and `fp[1]`

## Top-Level Entry

The module entrypoint is special. `ThreadState::run` starts interpretation near
the beginning of the thread stack, leaving room below the initial frame for
negative offsets:

```cpp
return run_interpreter(&stack[1024], obj, 0);
```

from [src/thread_state.cpp](../src/thread_state.cpp).

Top-level module code is not expected to execute `Return`; the parser/codegen
reject `return` outside a function. So the normal saved-caller metadata contract
applies to nested function/class execution, not to the initial module entry
frame.

## Practical Rules

If you are reasoning about CloverVM calls, the safest mental model is:

1. Codegen lays out `callable, arg0, arg1, ...` in a contiguous upward-growing
   register window.
2. The interpreter moves `fp` so those argument cells become `a0`, `a1`, ...
3. `fp[-1]`, `fp[0]`, and `fp[1]` hold the caller state needed by `Return`.
4. Locals/temporaries for the callee start at `r0 = fp[2]`.
5. The accumulator carries the return value across the `Return` instruction.

## Source Pointers

- Call lowering: [src/codegen.cpp](../src/codegen.cpp)
- Frame/header constants and register encoding:
  [src/code_object.h](../src/code_object.h)
- Register names in disassembly:
  [src/code_object_print.h](../src/code_object_print.h)
- Call and return execution: [src/interpreter.cpp](../src/interpreter.cpp)
- Top-level interpreter entry: [src/thread_state.cpp](../src/thread_state.cpp)
