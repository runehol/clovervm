#ifndef CL_JIT_SIDE_EXIT_BINDING_H
#define CL_JIT_SIDE_EXIT_BINDING_H

#include "jit/instruction.h"
#include "jit/side_exit_region_id.h"

#include <cstddef>
#include <utility>

namespace cl::jit
{
    struct SideExitBinding
    {
        SideExitRegionId region;
        ProgramValueRefRange arguments;
    };

    template <typename InstructionT>
    SideExitBinding make_side_exit_binding(InstructionT instruction)
    {
        return {instruction.side_exit_region(),
                instruction.side_exit_arguments()};
    }

    inline bool operator==(SideExitBinding lhs, SideExitBinding rhs)
    {
        if(lhs.region != rhs.region ||
           lhs.arguments.size() != rhs.arguments.size())
        {
            return false;
        }
        for(size_t index = 0; index < lhs.arguments.size(); ++index)
        {
            if(lhs.arguments[index].instruction_id() !=
               rhs.arguments[index].instruction_id())
            {
                return false;
            }
        }
        return true;
    }

    template <typename H> H AbslHashValue(H hash, SideExitBinding binding)
    {
        H result = H::combine(std::move(hash), binding.region,
                              binding.arguments.size());
        for(size_t index = 0; index < binding.arguments.size(); ++index)
        {
            result = H::combine(std::move(result),
                                binding.arguments[index].instruction_id());
        }
        return result;
    }

}  // namespace cl::jit

#endif  // CL_JIT_SIDE_EXIT_BINDING_H
