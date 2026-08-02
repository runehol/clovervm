#ifndef CL_JIT_INSTRUCTION_ATTRIBUTE_POOL_H
#define CL_JIT_INSTRUCTION_ATTRIBUTE_POOL_H

#include "runtime/fatal.h"

#include <cstdint>
#include <limits>
#include <vector>

namespace cl::jit
{
    template <typename T> class InstructionAttributePool
    {
    public:
        uint32_t append(T value)
        {
            if(values_.size() > std::numeric_limits<uint32_t>::max())
            {
                fatal("too many JIT instruction attributes");
            }
            uint32_t index = static_cast<uint32_t>(values_.size());
            values_.push_back(value);
            return index;
        }

        T at(uint32_t index) const
        {
            if(index >= values_.size())
            {
                fatal("invalid JIT instruction attribute index");
            }
            return values_[index];
        }

    private:
        std::vector<T> values_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_INSTRUCTION_ATTRIBUTE_POOL_H
