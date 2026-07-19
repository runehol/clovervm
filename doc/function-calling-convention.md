# CloverVM Function Calling Convention

| Field | Value |
|---|---|
| Document type | Architecture contract |
| Status | Accepted |
| Implementation | Interpreter convention implemented; JIT stack-transition extension proposed |
| Scope | Managed frame layout, call argument windows, entry, return, and interpreter dispatch state |
| Owning layers | `CodeObject` owns frame metadata; codegen owns call-window construction; the interpreter owns managed entry and return; native boundaries and the JIT consume the contract |
| Validated against | `df8fe91` (2026-07-18) |
| Supersedes | N/A |

This document defines CloverVM's managed frame and call conventions. It focuses
on the layout and transition rules shared by the interpreter, runtime-generated
thunks, native boundary adapters, and future compiled managed code. Opcode
inventories, copied implementation snippets, and helper signatures are not part
of the contract.

CloverVM uses register/accumulator bytecode over managed frame slots:

- parameters, locals, temporaries, and call argument windows live in managed
  frame slots;
- most expression results and every ordinary return value travel through the
  accumulator;
- the frame pointer identifies the current managed frame;
- the current hand-written interpreter and native runtime execute on the native
  machine stack while accessing the separate Clover stack explicitly.

The Clover stack is not a C or C++ ABI stack. During JIT bring-up, generated
Python code may temporarily use it as the architectural stack for managed call
and return instructions, but arbitrary native frames must never be placed on
it. Generated code switches to the host stack before entering the hand-written
interpreter or any C or C++ target. Native/managed transition rules are defined
separately in [Native/Managed Boundary Contracts](native-managed-boundaries.md).

## Logical Registers

Each `CodeObject` describes three logical slot groups:

- parameters, printed as `p0`, `p1`, ...;
- locals and temporaries, printed as `r0`, `r1`, ...;
- a fixed frame header around the frame pointer.

Parameters occupy positive offsets from the frame pointer. Locals and
temporaries occupy negative offsets and grow downward as their logical register
number increases.

Physical parameter storage is padded to the native ABI alignment. With the
current 8-byte `Value` and 16-byte alignment, an odd parameter count reserves
one padding slot. Padding is not a logical Python parameter and must never be
treated as one by binding or introspection.

## Frame Header

The current managed frame header occupies four slots at and above the frame
pointer:

```text
fp[3]  interpreter return program counter
fp[2]  interpreter return code object
fp[1]  compiled-code return program counter
fp[0]  previous managed frame pointer
```

The slots have distinct roles:

- `fp[0]` links the active managed frame chain;
- `fp[2]` and `fp[3]` restore interpreter execution after an interpreted
  callee returns;
- `fp[1]` holds an executable compiled return target. For generated managed
  calls it is the architectural return address consumed by the target's return
  sequence; an interpreted caller installs the interpreter-return thunk. It is
  not general-purpose native ABI storage.

Frame payload pointers stored in header slots use the runtime's documented
frame-payload encoding. Code that scans ordinary managed `Value` slots must not
mistake header payloads or padding for Python object references.

The conceptual layout is:

```text
higher addresses

    p0
    p1
    ...
    final parameter
    optional ABI padding
    interpreter return pc       fp[3]
    interpreter return code     fp[2]
    compiled return pc          fp[1]
fp  previous frame pointer      fp[0]
    r0                          fp[-1]
    r1                          fp[-2]
    ...

lower addresses
```

Frame construction, stack scanning, unwinding, native entry, and future JIT
code must agree on this layout. Changing it is a cross-layer architecture
change, not a local opcode refactor.

## Call Argument Windows

The caller reserves a contiguous, ABI-aligned temporary window for positional
arguments. The callable lives outside that window. For a zero-argument call,
the caller still reserves an anchor slot so entry and default-argument
adaptation have a well-defined boundary.

Before a managed callee is entered, the argument window must contain the
callee's physical parameter values in parameter order. Depending on the call
shape, the managed adaptation path may:

- validate arity;
- copy default values;
- bind explicit keyword values;
- construct callee `*args` or `**kwargs` values;
- insert a bound receiver;
- reject duplicates or missing required arguments.

Those are Python call semantics owned by
[Function Call Adaptation](function-call-adaptation.md). The calling convention
only requires that the resulting parameter window match the selected target
`CodeObject` before entry.

The call argument window must be the topmost live temporary range at the entry
point. This allows the interpreter to place the callee frame immediately below
the window and reinterpret the existing argument cells as `p0`, `p1`, ...
without allocating a second parameter array.

## Function Entry

Ordinary Python-visible calls enter through a `Function`. The `Function` entry
path owns target selection and call adaptation; a call site must not select an
arbitrary code object and bypass those semantics.

After adaptation, frame entry:

1. derives the callee frame pointer from the prepared argument window and the
   target's padded parameter count;
2. links the callee to the caller through the previous-frame slot;
3. records the canonical interpreter return code object and program counter;
4. installs an executable compiled return target, either a generated
   continuation or the interpreter-return thunk;
5. initializes the target's required local/temporary state;
6. begins execution at the selected code object's entry point.

The argument cells are not copied merely to establish the frame. Moving the
frame pointer changes their interpretation from caller temporaries to callee
parameters.

### Method Calls

Direct method-call lowering reserves room for the receiver and explicit
arguments in one call window. Method lookup decides whether the receiver must be
inserted as `self`. After that decision, the physical argument window follows
the same parameter-order contract as every other function entry.

Receiver binding belongs to guarded method lookup and call adaptation. It is
not encoded as a different callee frame layout.

### Internal Code-Object Entry

VM-generated thunks sometimes enter an already-selected `CodeObject`. That is a
narrow internal operation with these preconditions:

- the thunk has selected the correct code object;
- the complete parameter window has already been prepared;
- no public callable lookup or argument binding remains to perform.

Internal code-object entry uses the ordinary frame construction and return
layout, but it must not become a public shortcut around `Function` semantics.
Class bodies, module startup, protocol adapters, and constructor thunks may have
different target-selection policy while still sharing the managed frame
contract.

### Native Function Thunks

Python-visible native implementations are represented through managed
`Function` entry. Their thunk frames receive parameters through the same `p`
slots, then cross to native execution through the native/managed boundary.

The number of intrinsic helper variants and their concrete bytecodes are not
part of this calling convention. Adding a helper arity does not change the
managed frame contract.

## Return

An ordinary managed return carries its result in the accumulator. It restores:

- the previous frame pointer from `fp[0]`;
- the caller's interpreter code object from `fp[2]`;
- the caller's interpreter program counter from `fp[3]`.

Generated managed returns consume the architectural return target in `fp[1]`.
Native boundary returns additionally restore the thread's published Clover
frame frontier as described in the boundary document.

Exception propagation may leave a frame through unwind machinery rather than
ordinary return, but it must preserve the same frame-chain and caller-state
invariants. `Value::exception_marker()` is a boundary/protocol result, not an
ordinary Python return value.

## Threaded Interpreter State

The threaded interpreter carries its hot execution state through a fixed native
handler signature:

- current managed frame pointer;
- bytecode program counter;
- accumulator;
- dispatch table;
- current code object.

Handler-to-handler dispatch uses `musttail`. The fixed signature and frameless
hot handlers are performance constraints: they allow the compiler to keep
interpreter state in stable native registers across dispatch. Adding persistent
interpreter state to that signature or introducing native frames in hot handlers
requires measurement and frame-shape verification.

Only the managed frame pointer belongs to the Clover frame chain. The other
interpreter values are live execution state and must be published explicitly
when a safepoint or native transition requires them to become durable roots or
resume metadata.

## Stack Ownership And Root Visibility

The current hand-written interpreter uses a separate Clover stack containing
only VM-controlled managed state:

- frame headers;
- parameters;
- locals and temporaries;
- call argument windows;

The initial JIT preserves this split. Generated Python code installs the
architectural SP in Clover storage, claims managed frame space before writing
durable state, and uses the compiled-return-PC header slot as its architectural
return address. Before entering the interpreter, runtime, extension, or another
C or C++ target, a transition thunk publishes the managed frontier and switches
to the host stack. Nested re-entry records both stack positions so returns
restore the immediately enclosing execution state.

This arrangement lets the existing reclaimer continue scanning canonical
Clover storage while treating the host stack as opaque. Managed stack scanning
understands which Clover slots are live values, padding, or frame metadata; it
does not scan arbitrary host-stack words.

A later generated interpreter and mixed-stack walker may move managed Python
frames onto the ordinary platform stack. That migration requires exact frame
classification across managed and native regions and is not part of the initial
calling convention used to bring up the JIT.

## Required Invariants

- Parameters live above the frame pointer; locals and temporaries live below
  it.
- Physical parameter storage is ABI-aligned without changing the logical
  Python signature.
- The caller prepares one contiguous argument window in callee parameter order.
- The argument window is the topmost live temporary range when the callee frame
  is established.
- Frame entry reuses the prepared argument cells as callee parameter slots.
- Every managed frame links to the previous frame through the fixed header.
- Interpreter return state and compiled return state remain distinct.
- Ordinary return values travel in the accumulator.
- Public calls use `Function` entry semantics; direct code-object entry is an
  internal pre-adapted operation.
- Method receiver binding changes argument contents, not frame layout.
- During JIT bring-up, C, C++, extension, and hand-written interpreter execution
  remains on the host stack and opaque to managed root scanning; transition or
  handle records publish every managed value needed there.
- Generated managed code moves the architectural stack pointer before storing
  durable managed frame state in Clover storage; it does not use a host ABI red
  zone there.
- Safepoint scanning distinguishes live managed values from padding and frame
  payload metadata.
- Changes to header layout, register encoding, alignment, or entry/return shape
  are coordinated across codegen, interpreter, unwinding, safepoints, native
  boundaries, tests, and future JIT metadata.

## Related Documents

- [Function Call Adaptation](function-call-adaptation.md) owns Python signature
  binding, defaults, keyword calls, `*args`, and `**kwargs` policy.
- [Native/Managed Boundary Contracts](native-managed-boundaries.md) owns stack
  transitions, frame-frontier publication, and native result conventions.
- [Exception Transport And Protocols](exception-transport-and-protocols.md) owns
  managed unwinding and exception-marker adaptation.
- [Refcounting and Reclamation](refcounting-and-reclamation.md) owns live-stack
  publication and root scanning.
- [JIT Compiler and IR](jit-compiler-and-ir.md) owns compiled entry, return, and
  deoptimization requirements built on this frame contract.
