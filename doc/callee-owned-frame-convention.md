# Callee-Owned Frames and Calling Convention

| Field | Value |
|---|---|
| Document type | Design |
| Status | Draft |
| Implementation | Not started; the clearing-only `Enter` prerequisite is implemented |
| Scope | Explicit AArch64-like interpreter SP, fixed frame headers, argument-window adaptation, callee-owned setup and teardown, CodeObject-free normal compiled entry/return, and deferred JIT frame construction |
| Owning layers | Codegen prepares outgoing arguments; call adaptation binds parameters; the interpreter owns threaded entry/return; the JIT owns compiled frame allocation and side-exit reconstruction |
| Validated against | N/A; proposed convention. The implementation baseline is `f8491b3b` (2026-11-06) |
| Supersedes | N/A |

This records the proposed direction discussed after the clearing-only `Enter`
instruction landed. It does not authorize implementation or replace the current
[managed calling convention](function-calling-convention.md),
[AArch64 JIT convention](aarch64-jit-calling-convention.md), or
[native boundary contracts](native-managed-boundaries.md).

The interpreter continues to use threaded opcode dispatch. Frame activation and
teardown move into the callee so compiled paths can omit canonical frame
construction until a side exit needs it. This is a design draft; implementation
will wait for further discussion.

## Fixed Header Above the Arguments

FP identifies the current managed frame. SP points to the base of its reserved
four-cell outgoing header, immediately below the padded args, locals, and
temporaries allocation:

```text
frame_size = current frame's padded args + locals + temps
sp = fp - frame_size - 4
```

The interpreter carries both FP and SP in `PARAMS` and `ARGS`. Call uses SP
directly as the callee's new frame address; it does not recompute that address
from caller metadata or encode a distance in the accumulator. Arguments are
prepared below SP. Adaptation changes the argument window without moving the
header or its return metadata.

Offsets below are in eight-byte cells. In this four-parameter example, `p0` is
nearest the header and subsequent parameters descend in address order. Padding
placement for odd parameter counts remains unresolved.

```text
higher addresses

                  caller's locals and temporaries
                  (end of caller's ordinary slot allocation)
new_fp[3]         interpreter return program counter
new_fp[2]         interpreter return code object (the caller's)
new_fp[1]         compiled-code return program counter
new_fp[0]         previous managed frame pointer; caller SP / incoming SP
new_fp[-1]        p0 (first parameter)
new_fp[-2]        p1
new_fp[-3]        p2
new_fp[-4]        p3 (last parameter)
                  callee's locals
                  callee's temporaries
sp[3..0]          four reserved cells for the callee's next outgoing header
                  (SP points at slot [0], the lowest-address header cell)

lower addresses
```

The caller writes only the interpreted return fields, `[2]` and `[3]`. Enter
saves the previous managed FP in `[0]` and activates the frame. Interpreted entry
does not transport or write a compiled return PC. In AArch64 compiled execution,
that continuation travels in LR; compiled code saves it into `[1]` when needed
for preservation or reconstruction. Exact cross-engine return routing remains
open.

With arguments below the header, the final adapted parameter count no longer
determines where the header lands. Python argument adaptation is common enough
that a stable header address is central to the proposal.

## Call, Adaptation, Enter, and Return

The following is conceptual opcode state flow. Frame sizes and offsets are in
eight-byte cells. Call keeps the caller's FP and SP intact until Enter saves the
old FP and activates the callee. The accumulator remains an ordinary tagged
value; it does not carry a frame address or distance.

```text
Call (interpreted caller to interpreted callee):
    sp[2] = code_object
    sp[3] = pc + size_of_this_call_instruction

    prepare supplied arguments below sp
    resolve and adapt the call into its final parameter window
    preserve the original sp throughout adaptation

    on failure:
        discard the prepared window
        propagate the exception in the caller with its fp and sp intact

    code_object = selected callee's CodeObject
    pc = selected callee's actual Enter bytecode
    continue threaded dispatch with caller fp and sp unchanged

Interpreted Enter:
    sp[0] = fp
    fp = sp
    arg_slots = code_object's padded parameter-slot count
    n_slots = Enter's ordinary local/temporary-slot operand

    establish space for parameters, locals, temporaries,
        and the next four-slot outgoing header
    sp = fp - arg_slots - n_slots - 4
    clear n_slots cells starting at sp + 4
    advance pc past Enter and continue threaded dispatch

Interpreted Return (to an interpreted caller):
    saved_fp = fp[0]
    saved_code_object = fp[2]
    saved_pc = fp[3]

    sp = fp
    fp = saved_fp
    code_object = saved_code_object
    pc = saved_pc
    continue threaded dispatch with the result in the accumulator
```

There is no separate Enter procedure: `pc` points to the real Enter opcode and
its operands. Call records its own next bytecode PC before switching to the
callee's entry PC. The ordinary dispatch table and current callee code object
keep their ordinary roles.

Return restores the caller's SP by assigning the callee's FP to SP before
restoring the saved caller FP. The callee's header is exactly the caller's
reserved outgoing header, so this needs neither a parameter count nor the
caller's frame size. It discards the entire callee allocation below that header.

For now, the padded parameter count stays in the `CodeObject`, rather than
becoming an additional opcode operand. Interpreted Enter reads it there. It
describes the final bound parameter window, including padding, rather than the
original supplied argument count. The JIT reads it at compilation time and bakes
the layout into generated code, including any required stack arithmetic.

Adaptation changes the bottom of the argument window, never the header address.
For example, using cell addresses and half-open ranges:

```text
caller sp = 100
frame header:         [100, 104)    fixed before and after adaptation
supplied arguments:    [96, 100)    four physical slots
adapted parameters:    [94, 100)    six physical slots
callee fp:             100         independent of adapted count
Return restores caller sp = callee fp = 100.
```

Defaults, receiver insertion, keyword binding, and `*args`/`**kwargs` adaptation
produce the selected callee's final window below that header. Temporary
adaptation frames or scratch allocations must preserve the header address for
final entry, restoring the original SP if scratch work temporarily changes it.
The caller remains active on failure. All initialized argument
references and the saved caller code object must remain rooted during allocating
or reentrant binding; reserving or partially filling a header does not publish a
complete callee frame.

The outgoing window's location is implicit in SP. A Call instruction
would no longer need an explicit `first_arg_reg` operand if codegen always
places arguments there. Supplied argument counts and call-shape information
remain necessary for adaptation.

The implemented `Enter #n_slots` counts only ordinary slots to clear, including
padding, and that count is even. Parameter slots and the four reserved outgoing
header cells are separate from that count. Extending entry must preserve this
operand meaning unless an explicit decision changes it.

## Explicit SP and Native Stack Mapping

SP has an AArch64-like role: at a call it already points to the new header base;
entry saves old FP there, installs FP, and lowers SP for the callee allocation.
During threaded interpretation SP is an explicitly typed managed-stack pointer
handler argument, not a tagged `Value` and not the machine's native stack
pointer. It is not classified or traced as a heap reference. The accumulator
continues to hold tagged values. This replaces the earlier SMI-distance handoff.

For AArch64 compiled calls, architectural SP can be at the header base before
`bl`. The call leaves SP unchanged and puts the return address in LR. The callee
can store old FP and LR into header slots `[0]` and `[1]` when it constructs a
frame. Interpreted Call writes only `[2]` and `[3]`; interpreted Enter writes
`[0]` and leaves `[1]` alone.

The x86-64 backend accounts for `call` pushing its return address. For a compiled
callee that constructs this header, with `H` denoting the header base:

```text
before call:        RSP = H + 16
call:               RSP = H + 8     return address written to H[1]
push previous FP:   RSP = H         previous FP written to H[0]
set FP:             RBP = H
```

The last two steps belong to the callee's prologue. After frame construction,
both targets can have their architectural stack and frame pointers at the
header base, before allocating the remaining callee storage. On x86-64 the
usual restoration of RSP to RBP, followed by popping old FP and `ret`, restores
RSP to `H + 16`, its pre-call coordinate. That differs from the interpreter's
logical `sp = fp` restoration. A frameless x86-64 leaf keeps the pushed return
address at RSP and uses `ret` without constructing the header.

The interpreter keeps one logical SP convention; target-specific offsets and
native pushes belong to JIT lowering and boundary adapters. The precise physical
stack mapping, alignment, and controlled native transitions remain to be fixed.
Threaded opcode dispatch does not become native `call`/`ret` dispatch.

A fresh native-to-interpreter entry adapter must establish a managed boundary
frame, prepare the outgoing header and arguments, and supply the corresponding
managed FP and SP to Enter. Native machine FP is not a substitute for that
managed anchor. Return-to-native routing remains a boundary-design decision.
Resuming an already established frame, including a JIT side exit, restores that
frame's FP, SP, code object, PC, and accumulator and skips fresh Enter.

The temporary register-only experiment carried an extra SP argument unchanged,
without changing existing call-path work. On AArch64 it measured 2.32% more CPU
time for recursive Fibonacci over six samples per variant; the full 92-case
suite's geometric-mean throughput was effectively flat (+0.17%). Both variants
kept all 101 required handlers frame-free. These results measure carrying the
extra register, not this proposed convention. The experiment did not implement
the SMI-distance handoff either. Using SP directly may reduce call-path work,
but the combined convention change has not been measured. The temporary code
edits were discarded.

## Compiled Leaves and Side Exits

Normal jitted entry and return must not require an incoming `CodeObject *` or
inspect the callee's code object to determine frame size, parameter offsets, or
stack cleanup. Generated code knows its own layout and uses the supplied
arguments, frame state, and return continuation. A code object remains available
to interpreted execution and can be recovered from side-exit metadata when
entering the interpreter.

This distinguishes the compiled callee's code identity from the caller's
interpreter context. Returning to an interpreted caller must restore that
caller's context; it does not require carrying the callee's code object through
compiled execution. Target resolution, argument adaptation, and compilation may
still inspect metadata before selecting a compiled entry. Code identity and its
ownership/rooting remain part of recovery metadata.

Callee-owned construction allows the JIT to omit physical setup when the
executed path needs no frame. The caller must select the compiled entry before
activating an interpreted frame. The two interpreted return-field stores are
a caller-codegen decision in jitted mode: they may be omitted or deferred when
recovery can reconstruct the required context.

For `def f(): return None`, a direct AArch64 compiled entry could be:

```asm
f:
    mov     w0, #1       // current tagged None representation
    ret
```

The result is in `x0`, the incoming executable continuation stays in `x30`, and
the native stack and frame pointers are unchanged. The callee neither
constructs nor tears down a frame.

For `def f(a, b): return a + b`, a guarded SMI path can also remain frameless:

```asm
f:
    // Guard that x0 and x1 are SMIs; otherwise exit.
    adds    x9, x0, x1
    b.vs    exit
    mov     x0, x9
    ret

exit:
    // Construct and recover the canonical frame, then enter the interpreter.
```

The tentative sum uses a scratch register so an overflow exit still has both
original arguments. General Python addition can invoke `__add__` or `__radd__`;
the guarded path does not assume those calls are leaf operations.

Frame construction is sunk into side exits. An exit must preserve the incoming
continuation, establish the canonical frame and linkage, recover parameters,
locals, temporaries, accumulator, and interpreter context from its snapshot,
and restore SP to the base of the frame's reserved outgoing header. The
side-exit metadata supplies the code object needed for interpreter execution;
recovery must not depend on a register still containing an incoming code-object
pointer. It then publishes the
completed state and resumes at the exit's bytecode PC.

That resume skips `Enter`: recovery has already populated the frame, and fresh
entry clearing would destroy recovered values. Recovery operations that can
allocate, call, or safepoint need valid roots before they run; a partially
constructed frame is not a publishable frame.

These examples do not require spills or helper calls on their fast paths.
Other compiled paths may need stack storage or preservation across calls. The
proposal does not equate eliminating an interpreter frame with eliminating all
machine-stack allocation, or with forgetting the logical Python activation.
When a side exit reconstructs an interpreted frame, it must populate the full
four-slot header, including the caller's code object and interpreter return PC.
The caller's interpreter context must remain recoverable while a compiled
callee executes without that physical frame; its transport is still an open
boundary-design detail.
Existing [side-exit lowering](jit-side-exit-lowering.md) supplies the recovery
machinery to extend; frame reconstruction is additional work, not already
implemented by that machinery.

## Remaining Decisions Before Implementation

- Padding placement and homes for register-transported compiled parameters.
  The fixed header and descending parameter order are agreed; a complete
  padded/register-transported layout is not.
- Transport, ownership, and rooting of caller interpreter context across
  compiled execution, together with the precise interpreted/compiled/native
  return routing. The interpreted frame header is fixed at the four slots
  shown above; the frameless compiled path must preserve enough context to
  reconstruct it without requiring an incoming code-object argument.
- Transport and rooting of transient call/adaptation state, and the exact
  publication point for completed entry and reconstructed side-exit frames.
- Mapping managed frame geometry onto native stack/frame pointers and controlled
  transitions, including AArch64 LR and x86-64 pushed return addresses. Matching
  a frame-record shape alone does not establish single physical-stack execution
  or native unwinding compatibility.

The discussion used these supplied AArch64 compiler examples, built with
`-O2 -fno-omit-frame-pointer -fno-optimize-sibling-calls`:

| Example | Observed frame shape |
|---|---|
| `nonleaf(x) = transform(x) + x` | Reserves 32 bytes; FP is entry SP minus 32; saves FP/LR at FP and `x19` at FP plus 16 |
| `caller10(x)` calling `consume10(x, 2, ..., 10)` | Reserves 32 bytes; FP is entry SP minus 16; outgoing arguments 8 and 9 are below FP, at the call's native SP |
| `callee10(a, ..., j) = a + i + j` | No frame record or stack adjustment; reads arguments 8 and 9 at entry SP and entry SP plus 8 |

These examples motivate callee ownership, not one mandatory prologue: compiled
frames can put their FP record at different offsets from entry SP, and eligible
leaves can omit it entirely. A concrete implementation sketch must resolve the
remaining decisions and migration boundary before code changes.

Background: [issue #131](https://github.com/runehol/clovervm/issues/131).
