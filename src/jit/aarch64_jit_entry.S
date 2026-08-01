#if defined(__APPLE__)
#define CL_ASM_SYMBOL(name) _##name
#define CL_ASM_HIDDEN(name) .private_extern CL_ASM_SYMBOL(name)
#else
#define CL_ASM_SYMBOL(name) name
#define CL_ASM_HIDDEN(name) .hidden CL_ASM_SYMBOL(name)
#endif

.text
.p2align 2

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

.globl CL_ASM_SYMBOL(cl_aarch64_jit_side_exit)
CL_ASM_HIDDEN(cl_aarch64_jit_side_exit)
CL_ASM_SYMBOL(cl_aarch64_jit_side_exit):
    stp x0, x1, [x25, #0]
    stp x2, x3, [x25, #16]
    stp x4, x5, [x25, #32]
    stp x6, x7, [x25, #48]
    stp x8, x9, [x25, #64]
    stp x10, x11, [x25, #80]
    stp x12, x13, [x25, #96]
    stp x14, x15, [x25, #112]

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

    mov x0, x25
    mov x1, x16
    mov x2, x21
    bl CL_ASM_SYMBOL(cl_execute_transition_program)

    ldr x20, [x0, #0]
    ldr x22, [x0, #8]
    ldr x24, [x0, #16]
    ldr x30, [sp], #16
    b CL_ASM_SYMBOL(cl_aarch64_jit_reenter_interpreter)

#if !defined(__APPLE__)
.section .note.GNU-stack,"",%progbits
#endif
