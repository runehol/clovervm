#include "jit/side_exit_region.h"

#include "jit/compilation_storage.h"

namespace cl::jit
{
    SideExitRegion::SideExitRegion(
        const CompilationStorage &storage, SideExitRegionId id,
        std::span<const InstructionId> parameter_ids,
        std::span<const InstructionId> instruction_ids)
        : id_(id), storage_(&storage),
          parameter_ids_(parameter_ids.begin(), parameter_ids.end()),
          instruction_ids_(instruction_ids.begin(), instruction_ids.end())
    {
    }

    Instruction SideExitRegion::parameter_at(size_t index) const
    {
        return storage_->instruction(parameter_ids_.at(index));
    }

    Instruction SideExitRegion::instruction_at(size_t index) const
    {
        return storage_->instruction(instruction_ids_.at(index));
    }

}  // namespace cl::jit
