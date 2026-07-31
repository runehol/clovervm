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

#if !defined(__APPLE__)
.section .note.GNU-stack,"",%progbits
#endif
