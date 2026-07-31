#ifndef CL_AARCH64_JIT_REGISTERS_H
#define CL_AARCH64_JIT_REGISTERS_H

#include "jit/aarch64_assembler.h"

namespace cl::jit
{
    inline constexpr XRegister AArch64ThreadStateRegister{19};
    inline constexpr XRegister AArch64ManagedFramePointerRegister{20};

}  // namespace cl::jit

#endif  // CL_AARCH64_JIT_REGISTERS_H
