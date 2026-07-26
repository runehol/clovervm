#include "jit/instruction_operand_table.h"

#include "runtime/fatal.h"

#include <limits>

namespace cl::jit
{
    InstructionOperandTable::Allocation
    InstructionOperandTable::allocate(size_t count)
    {
        if(count == 0)
        {
            return {0, {}};
        }
        if(words_.size() > std::numeric_limits<uint32_t>::max() ||
           count > std::numeric_limits<uint32_t>::max() - words_.size())
        {
            fatal("too many JIT instruction operands");
        }

        uint32_t offset = static_cast<uint32_t>(words_.size());
        words_.resize(words_.size() + count);
        return {offset, std::span<uint32_t>(words_).subspan(offset, count)};
    }

    std::span<const uint32_t> InstructionOperandTable::words(uint32_t offset,
                                                             size_t count) const
    {
        if(count == 0)
        {
            return {};
        }
        if(offset > words_.size() || count > words_.size() - offset)
        {
            fatal("invalid JIT instruction operand range");
        }
        return std::span<const uint32_t>(words_).subspan(offset, count);
    }

}  // namespace cl::jit
