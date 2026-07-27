#ifndef CL_JIT_SIDE_EXIT_H
#define CL_JIT_SIDE_EXIT_H

#include "jit/instruction.h"

#include <span>
#include <vector>

namespace cl::jit
{
    class CompilationStorage;

    class SideExit
    {
    public:
        SideExit(const CompilationStorage &storage,
                 std::span<const ProgramValueRef> inputs,
                 std::span<const InstructionId> instructions);

        std::span<const ProgramValueRef> inputs() const { return inputs_; }
        std::span<const InstructionId> instructions() const
        {
            return instructions_;
        }

    private:
        std::vector<ProgramValueRef> inputs_;
        std::vector<InstructionId> instructions_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_SIDE_EXIT_H
