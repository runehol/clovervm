#include "jit/aarch64_transition.h"

#include "jit/location_assignments.h"
#include "jit/transition_program_emitter.h"
#include "runtime/fatal.h"

#include <cassert>
#include <limits>
#include <vector>

namespace cl::jit
{
    TransitionLocation aarch64_transition_location(PhysicalLocation location)
    {
        if(location.is_stack())
        {
            int32_t frame_offset = location.stack().frame_offset();
            assert(frame_offset >= std::numeric_limits<int16_t>::min());
            assert(frame_offset <= std::numeric_limits<int16_t>::max());
            return TransitionLocation::stack(
                static_cast<int16_t>(frame_offset));
        }

        PhysicalRegister reg = location.reg();
        assert(reg.number() < 32);
        switch(reg.register_class())
        {
            case RegisterClass::GPR:
                return TransitionLocation::register_file(
                    AArch64TransitionGPRBase + reg.number());
            case RegisterClass::SIMD:
                return TransitionLocation::register_file(
                    AArch64TransitionSIMDBase + reg.number());
            case RegisterClass::Count:
                break;
        }
        fatal("invalid AArch64 transition register class");
    }

    std::vector<TransitionInstruction>
    emit_aarch64_bound_side_exit_transition_program(
        const CompilationStorage &storage,
        const BytecodeStateOrder &state_order, SideExitBinding binding,
        const LocationAssignments &locations)
    {
        std::vector<TransitionLocation> argument_locations;
        argument_locations.reserve(binding.arguments.size());
        for(size_t index = 0; index < binding.arguments.size(); ++index)
        {
            argument_locations.push_back(aarch64_transition_location(
                locations.location_for(binding.arguments[index])));
        }

        return emit_side_exit_transition_program(storage, state_order, binding,
                                                 argument_locations);
    }

}  // namespace cl::jit
