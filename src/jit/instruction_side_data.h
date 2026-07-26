#ifndef CL_JIT_INSTRUCTION_SIDE_DATA_H
#define CL_JIT_INSTRUCTION_SIDE_DATA_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace cl::jit
{
    class InstructionSideDataPool
    {
    public:
        InstructionSideDataPool() = default;

        InstructionSideDataPool(const InstructionSideDataPool &) = delete;
        InstructionSideDataPool &
        operator=(const InstructionSideDataPool &) = delete;
        InstructionSideDataPool(InstructionSideDataPool &&) = delete;
        InstructionSideDataPool &operator=(InstructionSideDataPool &&) = delete;

        std::span<uintptr_t> allocate_words(size_t count);

    private:
        static constexpr size_t WordsPerSlab = 256;

        struct Slab
        {
            std::unique_ptr<uintptr_t[]> storage;
            size_t capacity = 0;
            size_t used = 0;
        };

        std::vector<Slab> slabs_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_INSTRUCTION_SIDE_DATA_H
