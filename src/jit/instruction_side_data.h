#ifndef CL_JIT_INSTRUCTION_SIDE_DATA_H
#define CL_JIT_INSTRUCTION_SIDE_DATA_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cl::jit
{
    struct InstructionSideDataAllocation
    {
        uint32_t offset;
        std::span<uint32_t> words;
    };

    class InstructionSideDataPool
    {
    public:
        InstructionSideDataPool() = default;

        InstructionSideDataPool(const InstructionSideDataPool &) = delete;
        InstructionSideDataPool &
        operator=(const InstructionSideDataPool &) = delete;
        InstructionSideDataPool(InstructionSideDataPool &&) = delete;
        InstructionSideDataPool &operator=(InstructionSideDataPool &&) = delete;

        InstructionSideDataAllocation allocate_words(size_t count);
        std::span<const uint32_t> words(uint32_t offset, size_t count) const;

    private:
        std::vector<uint32_t> words_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_INSTRUCTION_SIDE_DATA_H
