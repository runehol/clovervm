#ifndef CL_JIT_SIDE_EXIT_BINDING_H
#define CL_JIT_SIDE_EXIT_BINDING_H

#include "jit/instruction.h"
#include "jit/side_exit_region_id.h"

#include <span>

namespace cl::jit
{
    struct SideExitBinding
    {
        SideExitRegionId region;
        std::span<const ProgramValueRef> arguments;
    };

}  // namespace cl::jit

#endif  // CL_JIT_SIDE_EXIT_BINDING_H
