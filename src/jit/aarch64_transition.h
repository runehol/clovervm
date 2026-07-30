#ifndef CL_JIT_AARCH64_TRANSITION_H
#define CL_JIT_AARCH64_TRANSITION_H

#include "jit/physical_location.h"
#include "jit/side_exit_binding.h"
#include "jit/transition_program.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cl::jit
{
    class BytecodeStateOrder;
    class CompilationStorage;
    class LocationAssignments;
    class SideExit;

    // The AArch64 side-exit thunk saves one 64-bit slot per architectural
    // register. GPRs occupy the first half and the low 64 bits of SIMD
    // registers occupy the second half.
    constexpr int16_t AArch64TransitionGPRBase = 0;
    constexpr int16_t AArch64TransitionSIMDBase = 32;
    constexpr size_t AArch64TransitionRegisterSlotCount = 64;

    TransitionLocation aarch64_transition_location(PhysicalLocation location);

    std::vector<TransitionInstruction>
    emit_aarch64_side_exit_transition_program(
        const CompilationStorage &storage,
        const BytecodeStateOrder &state_order, const SideExit &side_exit,
        ProgramValueRefRange arguments, const LocationAssignments &locations);

    std::vector<TransitionInstruction>
    emit_aarch64_side_exit_transition_program(
        const CompilationStorage &storage,
        const BytecodeStateOrder &state_order, SideExitBinding binding,
        const LocationAssignments &locations);

}  // namespace cl::jit

#endif  // CL_JIT_AARCH64_TRANSITION_H
