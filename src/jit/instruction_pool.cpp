#include "jit/instruction_pool.h"

#include <algorithm>
#include <cassert>
#include <new>

namespace cl::jit
{
    void InstructionPool::add_slab()
    {
        Slab slab;
        slab.storage = std::make_unique<std::byte[]>(SlabBytes);

        uintptr_t base = reinterpret_cast<uintptr_t>(slab.storage.get());
        constexpr uintptr_t alignment = alignof(Instruction);
        uintptr_t first = (base + alignment - 1) & ~(alignment - 1);
        assert(first >= base);
        assert(first + RecordsPerSlab * sizeof(Instruction) <=
               base + SlabBytes);
        assert((first & (alignment - 1)) == 0);

        slab.next = reinterpret_cast<std::byte *>(first);
        slab.remaining = RecordsPerSlab;
        slabs_.push_back(std::move(slab));
    }

    void *InstructionPool::allocate_record()
    {
        if(slabs_.empty() || slabs_.back().remaining == 0)
        {
            add_slab();
        }

        Slab &slab = slabs_.back();
        void *storage = slab.next;
        slab.next += sizeof(Instruction);
        --slab.remaining;

        assert((reinterpret_cast<uintptr_t>(storage) &
                (alignof(Instruction) - 1)) == 0);

        return storage;
    }

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
