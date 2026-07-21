#ifndef CL_JIT_INSTRUCTION_POOL_H
#define CL_JIT_INSTRUCTION_POOL_H

#include "jit/instruction.h"

#include <absl/types/span.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace cl::jit
{
    class InstructionPool
    {
    public:
        InstructionPool() = default;

        InstructionPool(const InstructionPool &) = delete;
        InstructionPool &operator=(const InstructionPool &) = delete;
        InstructionPool(InstructionPool &&) = delete;
        InstructionPool &operator=(InstructionPool &&) = delete;

        Instruction *make(InstructionKind kind, uint16_t operand_count,
                          bool indirect_operands,
                          absl::Span<const Instruction::Slot> inline_slots);

    private:
        static constexpr size_t RecordsPerSlab = 64;
        static constexpr size_t SlabBytes =
            RecordsPerSlab * sizeof(Instruction) + 16;

        struct Slab
        {
            std::unique_ptr<std::byte[]> storage;
            std::byte *next = nullptr;
            size_t remaining = 0;
        };

        void add_slab();

        uint32_t next_serial_ = 0;
        std::vector<Slab> slabs_;
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

        absl::Span<uintptr_t> allocate_words(size_t count);

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

#endif  // CL_JIT_INSTRUCTION_POOL_H
