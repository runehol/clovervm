#ifndef CL_JIT_TRANSITION_PROGRAM_EMITTER_H
#define CL_JIT_TRANSITION_PROGRAM_EMITTER_H

#include "jit/side_exit_binding.h"
#include "jit/transition_program.h"

#include <span>
#include <vector>

namespace cl::jit
{
    class BytecodeStateOrder;
    class CompilationStorage;
    class SideExit;

    std::vector<TransitionInstruction> emit_side_exit_transition_program(
        const CompilationStorage &storage,
        const BytecodeStateOrder &state_order, const SideExit &side_exit,
        std::span<const TransitionLocation> input_locations);

    std::vector<TransitionInstruction> emit_side_exit_transition_program(
        const CompilationStorage &storage,
        const BytecodeStateOrder &state_order, SideExitBinding binding,
        std::span<const TransitionLocation> argument_locations);

}  // namespace cl::jit

#endif  // CL_JIT_TRANSITION_PROGRAM_EMITTER_H
