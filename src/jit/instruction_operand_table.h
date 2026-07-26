#ifndef CL_JIT_INSTRUCTION_OPERAND_TABLE_H
#define CL_JIT_INSTRUCTION_OPERAND_TABLE_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cl::jit
{
    class InstructionOperandTable
    {
    public:
        struct Allocation
        {
            uint32_t offset;
            std::span<uint32_t> words;
        };

        InstructionOperandTable() = default;

        InstructionOperandTable(const InstructionOperandTable &) = delete;
        InstructionOperandTable &
        operator=(const InstructionOperandTable &) = delete;
        InstructionOperandTable(InstructionOperandTable &&) = delete;
        InstructionOperandTable &operator=(InstructionOperandTable &&) = delete;

        Allocation allocate(size_t count);
        std::span<const uint32_t> words(uint32_t offset, size_t count) const;

    private:
        std::vector<uint32_t> words_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_INSTRUCTION_OPERAND_TABLE_H
