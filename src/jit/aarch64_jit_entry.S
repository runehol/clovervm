#if defined(__APPLE__)
#define CL_ASM_SYMBOL(name) _##name
#define CL_ASM_HIDDEN(name) .private_extern CL_ASM_SYMBOL(name)
#else
#define CL_ASM_SYMBOL(name) name
#define CL_ASM_HIDDEN(name) .hidden CL_ASM_SYMBOL(name)
#endif

.text
.p2align 2

.macro CL_JIT_ENTER_PROLOGUE return_label
    stp x19, x20, [sp, #-32]!
    str x30, [sp, #16]
    mov x19, x0
    mov x20, x1
    mov x16, x2
    mov x17, x3
    adr x9, \return_label
    str x9, [x20, #8]
.endm

.macro CL_JIT_ENTER_EPILOGUE
    ldr x30, [sp, #16]
    ldp x19, x20, [sp], #32
    ret
.endm

.globl CL_ASM_SYMBOL(cl_aarch64_enter_jit_0)
CL_ASM_HIDDEN(cl_aarch64_enter_jit_0)
CL_ASM_SYMBOL(cl_aarch64_enter_jit_0):
    CL_JIT_ENTER_PROLOGUE .Lcl_aarch64_enter_jit_0_return
    blr x17
.Lcl_aarch64_enter_jit_0_return:
    CL_JIT_ENTER_EPILOGUE

.globl CL_ASM_SYMBOL(cl_aarch64_enter_jit_2)
CL_ASM_HIDDEN(cl_aarch64_enter_jit_2)
CL_ASM_SYMBOL(cl_aarch64_enter_jit_2):
    CL_JIT_ENTER_PROLOGUE .Lcl_aarch64_enter_jit_2_return
    ldp x1, x0, [x20, #32]
    blr x17
.Lcl_aarch64_enter_jit_2_return:
    CL_JIT_ENTER_EPILOGUE

.globl CL_ASM_SYMBOL(cl_aarch64_enter_jit_4)
CL_ASM_HIDDEN(cl_aarch64_enter_jit_4)
CL_ASM_SYMBOL(cl_aarch64_enter_jit_4):
    CL_JIT_ENTER_PROLOGUE .Lcl_aarch64_enter_jit_4_return
    ldp x3, x2, [x20, #32]
    ldp x1, x0, [x20, #48]
    blr x17
.Lcl_aarch64_enter_jit_4_return:
    CL_JIT_ENTER_EPILOGUE

.globl CL_ASM_SYMBOL(cl_aarch64_enter_jit_6)
CL_ASM_HIDDEN(cl_aarch64_enter_jit_6)
CL_ASM_SYMBOL(cl_aarch64_enter_jit_6):
    CL_JIT_ENTER_PROLOGUE .Lcl_aarch64_enter_jit_6_return
    ldp x5, x4, [x20, #32]
    ldp x3, x2, [x20, #48]
    ldp x1, x0, [x20, #64]
    blr x17
.Lcl_aarch64_enter_jit_6_return:
    CL_JIT_ENTER_EPILOGUE

.globl CL_ASM_SYMBOL(cl_aarch64_enter_jit_8)
CL_ASM_HIDDEN(cl_aarch64_enter_jit_8)
CL_ASM_SYMBOL(cl_aarch64_enter_jit_8):
    CL_JIT_ENTER_PROLOGUE .Lcl_aarch64_enter_jit_8_return
    ldp x7, x6, [x20, #32]
    ldp x5, x4, [x20, #48]
    ldp x3, x2, [x20, #64]
    ldp x1, x0, [x20, #80]
    blr x17
.Lcl_aarch64_enter_jit_8_return:
    CL_JIT_ENTER_EPILOGUE

#if !defined(__APPLE__)
.section .note.GNU-stack,"",%progbits
#endif
