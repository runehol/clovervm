#if defined(__APPLE__)
#define CL_ASM_SYMBOL(name) _##name
#define CL_ASM_HIDDEN(name) .private_extern CL_ASM_SYMBOL(name)
#else
#define CL_ASM_SYMBOL(name) name
#define CL_ASM_HIDDEN(name) .hidden CL_ASM_SYMBOL(name)
#endif

/*
 * Clover's interpreter functions use Clang's preserve_none convention. Their
 * six-argument signature has a fixed AArch64 register assignment:
 *
 *     x20  tagged accumulator
 *     x21  managed frame pointer
 *     x22  interpreter PC
 *     x23  dispatch pointer, or compiled entry during JIT entry
 *     x24  current CodeObject *
 *     x25  active ThreadState *
 *
 * preserve_none does not require the callee to preserve ordinary register
 * state. These handwritten thunks therefore retain or rebuild the fixed
 * interpreter registers explicitly. The transition executor is an ordinary
 * C++ AAPCS call, under which x20-x25 remain callee-saved.
 */

.text
.p2align 2

// Standalone entry thunks preserve the native return address, load the fixed
// register-argument prefix from the managed frame, call compiled code through
// x23, and return its x0 result directly to the native caller.
//
// Value PRESERVE_NONE cl_aarch64_standalone_enter_jit_N(
//     Value accumulator, Value *frame_pointer, const uint8_t *interpreter_pc,
//     void *compiled_entry, CodeObject *code_object, ThreadState *thread);
.globl CL_ASM_SYMBOL(cl_aarch64_standalone_enter_jit_0)
CL_ASM_HIDDEN(cl_aarch64_standalone_enter_jit_0)
CL_ASM_SYMBOL(cl_aarch64_standalone_enter_jit_0):
    str x30, [sp, #-16]!
    mov x17, x23
    blr x17
    ldr x30, [sp], #16
    ret

.globl CL_ASM_SYMBOL(cl_aarch64_standalone_enter_jit_2)
CL_ASM_HIDDEN(cl_aarch64_standalone_enter_jit_2)
CL_ASM_SYMBOL(cl_aarch64_standalone_enter_jit_2):
    str x30, [sp, #-16]!
    mov x17, x23
    ldp x1, x0, [x21, #32]
    blr x17
    ldr x30, [sp], #16
    ret

.globl CL_ASM_SYMBOL(cl_aarch64_standalone_enter_jit_4)
CL_ASM_HIDDEN(cl_aarch64_standalone_enter_jit_4)
CL_ASM_SYMBOL(cl_aarch64_standalone_enter_jit_4):
    str x30, [sp, #-16]!
    mov x17, x23
    ldp x3, x2, [x21, #32]
    ldp x1, x0, [x21, #48]
    blr x17
    ldr x30, [sp], #16
    ret

.globl CL_ASM_SYMBOL(cl_aarch64_standalone_enter_jit_6)
CL_ASM_HIDDEN(cl_aarch64_standalone_enter_jit_6)
CL_ASM_SYMBOL(cl_aarch64_standalone_enter_jit_6):
    str x30, [sp, #-16]!
    mov x17, x23
    ldp x5, x4, [x21, #32]
    ldp x3, x2, [x21, #48]
    ldp x1, x0, [x21, #64]
    blr x17
    ldr x30, [sp], #16
    ret

.globl CL_ASM_SYMBOL(cl_aarch64_standalone_enter_jit_8)
CL_ASM_HIDDEN(cl_aarch64_standalone_enter_jit_8)
CL_ASM_SYMBOL(cl_aarch64_standalone_enter_jit_8):
    str x30, [sp, #-16]!
    mov x17, x23
    ldp x7, x6, [x21, #32]
    ldp x5, x4, [x21, #48]
    ldp x3, x2, [x21, #64]
    ldp x1, x0, [x21, #80]
    blr x17
    ldr x30, [sp], #16
    ret

// Interpreter-tail entry thunks preserve the native return address, load the
// fixed register-argument prefix from the managed frame, and call compiled code
// through x23. A normal compiled return is moved from x0 into the interpreter
// accumulator before the thunk restores its native caller and tail-calls
// interpreter reentry with the fixed interpreter state intact.
//
// Value PRESERVE_NONE cl_aarch64_interpreter_tail_enter_jit_N(
//     Value accumulator, Value *frame_pointer, const uint8_t *interpreter_pc,
//     void *compiled_entry, CodeObject *code_object, ThreadState *thread);
.globl CL_ASM_SYMBOL(cl_aarch64_interpreter_tail_enter_jit_0)
CL_ASM_HIDDEN(cl_aarch64_interpreter_tail_enter_jit_0)
CL_ASM_SYMBOL(cl_aarch64_interpreter_tail_enter_jit_0):
    str x30, [sp, #-16]!
    mov x17, x23
    blr x17
    mov x20, x0
    ldr x30, [sp], #16
    b CL_ASM_SYMBOL(cl_aarch64_jit_reenter_interpreter)

.globl CL_ASM_SYMBOL(cl_aarch64_interpreter_tail_enter_jit_2)
CL_ASM_HIDDEN(cl_aarch64_interpreter_tail_enter_jit_2)
CL_ASM_SYMBOL(cl_aarch64_interpreter_tail_enter_jit_2):
    str x30, [sp, #-16]!
    mov x17, x23
    ldp x1, x0, [x21, #32]
    blr x17
    mov x20, x0
    ldr x30, [sp], #16
    b CL_ASM_SYMBOL(cl_aarch64_jit_reenter_interpreter)

.globl CL_ASM_SYMBOL(cl_aarch64_interpreter_tail_enter_jit_4)
CL_ASM_HIDDEN(cl_aarch64_interpreter_tail_enter_jit_4)
CL_ASM_SYMBOL(cl_aarch64_interpreter_tail_enter_jit_4):
    str x30, [sp, #-16]!
    mov x17, x23
    ldp x3, x2, [x21, #32]
    ldp x1, x0, [x21, #48]
    blr x17
    mov x20, x0
    ldr x30, [sp], #16
    b CL_ASM_SYMBOL(cl_aarch64_jit_reenter_interpreter)

.globl CL_ASM_SYMBOL(cl_aarch64_interpreter_tail_enter_jit_6)
CL_ASM_HIDDEN(cl_aarch64_interpreter_tail_enter_jit_6)
CL_ASM_SYMBOL(cl_aarch64_interpreter_tail_enter_jit_6):
    str x30, [sp, #-16]!
    mov x17, x23
    ldp x5, x4, [x21, #32]
    ldp x3, x2, [x21, #48]
    ldp x1, x0, [x21, #64]
    blr x17
    mov x20, x0
    ldr x30, [sp], #16
    b CL_ASM_SYMBOL(cl_aarch64_jit_reenter_interpreter)

.globl CL_ASM_SYMBOL(cl_aarch64_interpreter_tail_enter_jit_8)
CL_ASM_HIDDEN(cl_aarch64_interpreter_tail_enter_jit_8)
CL_ASM_SYMBOL(cl_aarch64_interpreter_tail_enter_jit_8):
    str x30, [sp, #-16]!
    mov x17, x23
    ldp x7, x6, [x21, #32]
    ldp x5, x4, [x21, #48]
    ldp x3, x2, [x21, #64]
    ldp x1, x0, [x21, #80]
    blr x17
    mov x20, x0
    ldr x30, [sp], #16
    b CL_ASM_SYMBOL(cl_aarch64_jit_reenter_interpreter)

// Generated cold side-exit blocks place their TransitionInstruction program
// address in x16 and branch here while the compiled value registers are still
// live. The thunk captures those allocator-visible values in the active
// ThreadState, executes the transition against the current managed frame, and
// rebuilds the fixed interpreter state from the returned resume record. A side
// exit remains in the current managed function: it does not pop x21's frame.
// Finally, the thunk removes the native-stack record established by the
// interpreter-to-JIT entry thunk and tail-calls interpreter dispatch directly.
//
// void cl_aarch64_jit_side_exit(
//     const TransitionInstruction *program /* x16 */,
//     Value *frame_pointer /* x21 */, ThreadState *thread /* x25 */);
.globl CL_ASM_SYMBOL(cl_aarch64_jit_side_exit)
CL_ASM_HIDDEN(cl_aarch64_jit_side_exit)
CL_ASM_SYMBOL(cl_aarch64_jit_side_exit):
    // TransitionExecutionContext is the first ThreadState member, and its
    // register file is at offset zero. Save every allocator-visible GPR while
    // the compiled values are still live.
    stp x0, x1, [x25, #0]
    stp x2, x3, [x25, #16]
    stp x4, x5, [x25, #32]
    stp x6, x7, [x25, #48]
    stp x8, x9, [x25, #64]
    stp x10, x11, [x25, #80]
    stp x12, x13, [x25, #96]
    stp x14, x15, [x25, #112]
    stp x19, x20, [x25, #152]
    stp x22, x23, [x25, #176]
    str x24, [x25, #192]
    stp x26, x27, [x25, #208]
    str x28, [x25, #224]

    // Save the low 64 bits of every allocator-visible SIMD register into the
    // architectural-number-indexed second half of the register file.
    stp d0, d1, [x25, #256]
    stp d2, d3, [x25, #272]
    stp d4, d5, [x25, #288]
    stp d6, d7, [x25, #304]
    stp d16, d17, [x25, #384]
    stp d18, d19, [x25, #400]
    stp d20, d21, [x25, #416]
    stp d22, d23, [x25, #432]
    stp d24, d25, [x25, #448]
    stp d26, d27, [x25, #464]
    stp d28, d29, [x25, #480]

    // Execute x16's transition program against the current managed frame. The
    // executor returns a pointer to context-owned InterpreterResumeState.
    mov x0, x25  // TransitionExecutionContext * at ThreadState offset zero
    mov x1, x16  // const TransitionInstruction * selected by the side exit
    mov x2, x21  // Value * for the current managed frame
    bl CL_ASM_SYMBOL(cl_execute_transition_program)

    // x0 is the returned context-owned InterpreterResumeState *.
    ldr x20, [x0, #0]   // recovered tagged interpreter accumulator
    ldr x22, [x0, #8]   // recovered interpreter bytecode PC
    ldr x24, [x0, #16]  // authoritative CodeObject * named by the program

    // Discard the entry thunk's outstanding compiled-call frame, restore its
    // native caller, and resume the interpreter inside the current function.
    ldr x30, [sp], #16
    b CL_ASM_SYMBOL(cl_aarch64_jit_reenter_interpreter)

#if !defined(__APPLE__)
.section .note.GNU-stack,"",%progbits
#endif
