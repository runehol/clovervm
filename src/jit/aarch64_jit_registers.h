#ifndef CL_AARCH64_JIT_REGISTERS_H
#define CL_AARCH64_JIT_REGISTERS_H

#include "jit/aarch64_assembler.h"

namespace cl::jit
{
    inline constexpr XRegister AArch64ManagedFramePointerRegister{21};
    inline constexpr XRegister AArch64InterpreterPcRegister{22};
    inline constexpr XRegister AArch64CodeObjectRegister{24};
    inline constexpr XRegister AArch64ThreadStateRegister{25};

}  // namespace cl::jit

#endif  // CL_AARCH64_JIT_REGISTERS_H
