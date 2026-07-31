#ifndef CL_JIT_CONFIG_H
#define CL_JIT_CONFIG_H

#include "build_config.h"

#include <cstdint>

namespace cl::jit
{
    inline constexpr bool JitTieringEnabled = CL_JIT_TIERING_ENABLED != 0;
    inline constexpr uint32_t InitialJitTieringBudget = 100;

}  // namespace cl::jit

#endif  // CL_JIT_CONFIG_H
