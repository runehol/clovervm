#ifndef CL_JIT_SIDE_EXIT_REGION_H
#define CL_JIT_SIDE_EXIT_REGION_H

#include "jit/instruction.h"
#include "jit/side_exit_region_id.h"

#include <span>
#include <vector>

namespace cl::jit
{
    class CompilationStorage;

    class SideExitRegion
    {
    public:
        SideExitRegion(const CompilationStorage &storage, SideExitRegionId id,
                       std::span<const InstructionId> parameter_ids,
                       std::span<const InstructionId> instruction_ids);

        SideExitRegionId id() const { return id_; }
        std::span<const InstructionId> parameter_ids() const
        {
            return parameter_ids_;
        }
        std::span<const InstructionId> instruction_ids() const
        {
            return instruction_ids_;
        }

        Instruction parameter_at(size_t index) const;
        Instruction instruction_at(size_t index) const;

    private:
        SideExitRegionId id_;
        const CompilationStorage *storage_;
        std::vector<InstructionId> parameter_ids_;
        std::vector<InstructionId> instruction_ids_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_SIDE_EXIT_REGION_H
