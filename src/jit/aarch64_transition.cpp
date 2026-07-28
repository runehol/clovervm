#include "jit/aarch64_transition.h"

#include "runtime/fatal.h"

#include <cassert>
#include <limits>

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

}  // namespace cl::jit
