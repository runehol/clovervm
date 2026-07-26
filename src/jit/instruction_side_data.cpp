#include "jit/instruction_side_data.h"

#include <algorithm>
#include <new>

namespace cl::jit
{
    std::span<uintptr_t> InstructionSideDataPool::allocate_words(size_t count)
    {
        if(count == 0)
        {
            return {};
        }

        if(slabs_.empty() ||
           slabs_.back().capacity - slabs_.back().used < count)
        {
            size_t capacity = std::max(WordsPerSlab, count);
            slabs_.push_back(
                {std::unique_ptr<uintptr_t[]>(new uintptr_t[capacity]),
                 capacity, 0});
        }

        Slab &slab = slabs_.back();
        uintptr_t *result = slab.storage.get() + slab.used;
        slab.used += count;
        return {result, count};
    }

}  // namespace cl::jit
