#ifndef CL_JIT_AARCH64_TRANSITION_H
#define CL_JIT_AARCH64_TRANSITION_H

#include "jit/physical_location.h"
#include "jit/transition_program.h"

#include <cstddef>
#include <cstdint>

namespace cl::jit
{
    // The AArch64 side-exit thunk saves one 64-bit slot per architectural
    // register. GPRs occupy the first half and the low 64 bits of SIMD
    // registers occupy the second half.
    constexpr int16_t AArch64TransitionGPRBase = 0;
    constexpr int16_t AArch64TransitionSIMDBase = 32;
    constexpr size_t AArch64TransitionRegisterSlotCount = 64;

    TransitionLocation aarch64_transition_location(PhysicalLocation location);

}  // namespace cl::jit

#endif  // CL_JIT_AARCH64_TRANSITION_H
