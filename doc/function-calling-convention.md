# CloverVM Function Calling Convention

This note documents the calling convention currently implemented by CloverVM's bytecode compiler and interpreter. It is based on the runtime behavior in [src/interpreter.cpp](/Users/runehol/projects/clovervm/src/interpreter.cpp) and the lowering logic in [src/codegen.cpp](/Users/runehol/projects/clovervm/src/codegen.cpp).

## Executive Summary

- CloverVM uses a register-based bytecode.
- The interpreter also carries key VM state in native function arguments/registers: `fp`, `pc`, `accumulator`, `dispatch`, and `code_object`.
- Call arguments are laid out in a contiguous register window in the caller.
- Entering a function does not copy arguments into a separate argument array. Instead, the interpreter moves `fp` so the existing call window becomes the callee's frame.
- The stack grows toward lower addresses.
- Parameters live at positive offsets from `fp`.
- Locals and temporaries live at negative offsets from `fp`.
- A small frame header is reserved around `fp`; current call paths store:
  - previous frame pointer at `fp[0]`
  - return code object at `fp[-1]`
  - return program counter at `fp[-2]`
- One reserved header slot at `fp[1]` exists today but is not populated by the current call paths.

## Native Interpreter State

The CloverVM frame layout is only part of the calling convention. The threaded interpreter also passes its live execution state as native function parameters:

```cpp
#define PARAMS                                                                 \
    Value *fp, const uint8_t *pc, Value accumulator, void *dispatch,           \
        CodeObject *code_object
#define ARGS fp, pc, accumulator, dispatch, code_object
```

from [src/interpreter.cpp](/Users/runehol/projects/clovervm/src/interpreter.cpp).

Conceptually:

- `fp` is the current CloverVM frame pointer
- `pc` is the current bytecode program counter
- `accumulator` is the current accumulator value
- `code_object` is the current bytecode object
- `dispatch` is the dispatch table pointer

With the `MUSTTAIL` threaded-dispatch style in the interpreter, these values are intended to stay live in native calling-convention registers across opcode handlers, rather than being reloaded from the CloverVM stack frame on each step. In particular, the dispatch table pointer is kept permanently live as part of that native register state.

## Relevant Definitions

In [src/code_object.h](/Users/runehol/projects/clovervm/src/code_object.h):

```cpp
static constexpr int32_t FrameHeaderSizeAboveFp = 2;
static constexpr int32_t FrameHeaderSizeBelowFp = 2;
static constexpr int32_t FrameHeaderSize =
    FrameHeaderSizeAboveFp + FrameHeaderSizeBelowFp;
```

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

Codegen reserves the header slots in function and class scopes before collecting locals:

- function bodies: [src/codegen.cpp](/Users/runehol/projects/clovervm/src/codegen.cpp)
- class bodies: [src/codegen.cpp](/Users/runehol/projects/clovervm/src/codegen.cpp)

## Register Naming and Placement

The bytecode printer exposes the register naming convention in [src/code_object_print.h](/Users/runehol/projects/clovervm/src/code_object_print.h):

- `a0`, `a1`, ... are argument/parameter registers
- `r0`, `r1`, ... are local/temporary registers

The encoding rule is in [src/code_object.h](/Users/runehol/projects/clovervm/src/code_object.h):

```cpp
int8_t encode_reg(uint32_t reg)
{
    return n_parameters - 1 + FrameHeaderSizeAboveFp - reg;
}
```

Combined with interpreter access through `fp[reg]`, this gives the physical layout:

```text
higher addresses

    fp[n_parameters + 1]   a0
    fp[n_parameters + 0]   a1
    ...
    fp[3]                  a(n-2)
    fp[2]                  a(n-1)
    fp[1]                  reserved header slot (currently unused)
fp->fp[0]                  previous frame pointer
    fp[-1]                 return code object
    fp[-2]                 return program counter
    fp[-3]                 r0
    fp[-4]                 r1
    fp[-5]                 r2
    ...

lower addresses
```

So:

- arguments/parameters are above `fp`
- locals/temporaries are below `fp`
- larger logical register numbers move downward in memory

## How Codegen Lays Out a Call

### Simple calls

For a direct call like `f(x, y)`, codegen emits:

1. the callable into a temporary base register
2. each argument into the next registers in sequence
3. `CallSimple base, n_args`

The lowering is in `codegen_function_call` in [src/codegen.cpp](/Users/runehol/projects/clovervm/src/codegen.cpp):

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
higher addresses

    callable
    arg0
    arg1
    ...

lower addresses
```

### Method calls

For `obj.method(x)`, codegen reserves one extra slot:

- slot for the callable
- slot for `self`
- slots for user arguments

in [src/codegen.cpp](/Users/runehol/projects/clovervm/src/codegen.cpp):

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
- `self` at `fp[call_base_reg - 1]`

from [src/interpreter.cpp](/Users/runehol/projects/clovervm/src/interpreter.cpp):

```cpp
fp[call_base_reg] = callable;
fp[call_base_reg - 1] = self;
```

If the resolved method is not a normal function needing implicit `self`, `self` remains `not_present`. `CallMethod` then shifts the user arguments down by one slot so the call still presents a normal contiguous `callable, arg0, arg1, ...` layout. If `self` is present, `CallMethod` increases the effective arity by one so the callee sees:

- `a0 = self`
- `a1 = first user arg`
- ...

## Function Entry

The core transition for function calls is in `op_call_simple` and `op_call_method` in [src/interpreter.cpp](/Users/runehol/projects/clovervm/src/interpreter.cpp):

```cpp
Value *new_fp = fp + reg - n_args - FrameHeaderSizeAboveFp;

new_fp[0].as.ptr = (Object *)fp;
new_fp[-1] = Value::from_oop(code_object);
new_fp[-2].as.ptr = (Object *)pc;

fp = new_fp;
code_object = fun.get_ptr<Function>()->code_object.extract();
pc = code_object->code.data();
```

This is the key idea: the interpreter does not allocate/copy a fresh argument block. It reinterprets the caller's already-laid-out call window as the callee frame.

### Why `new_fp` is computed this way

At the call site:

- `reg` points at the callable slot
- the `n_args` argument values sit immediately below that slot

So:

```text
callable slot        = fp[reg]
last argument slot   = fp[reg - n_args]
new fp               = fp[reg - n_args - 2]
```

The extra `2` is `FrameHeaderSizeAboveFp`, which places the new `fp` so that:

- the last parameter lands at `fp[2]`
- the first parameter lands at `fp[n_args + 1]`
- header words occupy `fp[1]`, `fp[0]`, `fp[-1]`, `fp[-2]`

## Frame Diagram

The following shows a normal function frame for a function with three parameters and two local/temporary registers:

```text
stack grows downward

    higher addresses
        |
        v

    fp[4]   a0   first parameter
    fp[3]   a1
    fp[2]   a2   last parameter
    fp[1]        reserved header slot (currently unused)
fp  fp[0]        previous frame pointer
    fp[-1]       return code object
    fp[-2]       return PC
    fp[-3]  r0   first local/temporary
    fp[-4]  r1

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

After `new_fp = fp + reg - n_args - 2`:

    new_fp[3]  a0 = x
    new_fp[2]  a1 = y
    new_fp[1]      reserved
    new_fp[0]      previous fp
    new_fp[-1]     return code object
    new_fp[-2]     return PC
    new_fp[-3]     r0
    ...
```

## Return Path

Returning is the inverse operation. `op_return` in [src/interpreter.cpp](/Users/runehol/projects/clovervm/src/interpreter.cpp) restores the caller context from the frame header:

```cpp
pc = (const uint8_t *)fp[-2].as.ptr;
code_object = fp[-1].get_ptr<CodeObject>();
fp = (Value *)fp[0].as.ptr;
```

The return value itself is carried in the interpreter accumulator, not in a stack slot.

So the full convention is:

- arguments flow into the callee through frame slots
- return values flow back in the accumulator
- return control state is restored from `fp[0]`, `fp[-1]`, and `fp[-2]`

## Top-Level Entry

The module entrypoint is special. `ThreadState::run` starts interpretation at a fixed location near the end of the thread stack:

```cpp
return run_interpreter(&stack[stack.size() - 1024], obj, 0);
```

from [src/thread_state.cpp](/Users/runehol/projects/clovervm/src/thread_state.cpp).

Top-level module code is not expected to execute `Return`; the parser/codegen reject `return` outside a function. So the normal saved-caller metadata contract applies to nested function/class execution, not to the initial module entry frame.

## Practical Rules

If you are reasoning about CloverVM calls, the safest mental model is:

1. Codegen lays out `callable, arg0, arg1, ...` in a contiguous downward-growing register window.
2. The interpreter moves `fp` so those argument cells become `a0`, `a1`, ...
3. `fp[0]`, `fp[-1]`, and `fp[-2]` hold the caller state needed by `Return`.
4. Locals/temporaries for the callee start at `r0 = fp[-3]`.
5. The accumulator carries the return value across the `Return` instruction.

## Source Pointers

- Call lowering: [src/codegen.cpp](/Users/runehol/projects/clovervm/src/codegen.cpp)
- Frame/header constants and register encoding: [src/code_object.h](/Users/runehol/projects/clovervm/src/code_object.h)
- Register names in disassembly: [src/code_object_print.h](/Users/runehol/projects/clovervm/src/code_object_print.h)
- Call and return execution: [src/interpreter.cpp](/Users/runehol/projects/clovervm/src/interpreter.cpp)
- Top-level interpreter entry: [src/thread_state.cpp](/Users/runehol/projects/clovervm/src/thread_state.cpp)
