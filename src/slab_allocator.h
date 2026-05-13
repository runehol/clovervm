#ifndef CL_SLAB_ALLOCATOR_H
#define CL_SLAB_ALLOCATOR_H

#include "value.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>

namespace cl
{
    class GlobalHeap;

    class SlabAllocator
    {
    public:
        SlabAllocator(GlobalHeap *global_heap, size_t offset, size_t slab_size);
        ~SlabAllocator();

        void add_reclaim_blocker() { ++n_reclaim_blockers; }
        void drop_reclaim_blocker();
        uint32_t reclaim_blocker_count() const { return n_reclaim_blockers; }

        char *start() const { return start_ptr; }
        char *end() const { return end_ptr; }

        char *allocate(size_t n_bytes)
        {
            if(curr_ptr + n_bytes > end_ptr)
            {
                return nullptr;
            }
            char *result = curr_ptr;
            n_bytes = (n_bytes + value_ptr_granularity - 1) &
                      ~(value_ptr_granularity - 1);
            curr_ptr += n_bytes;
            return result;
        }

    private:
        GlobalHeap *global_heap;
        char *start_ptr;
        char *curr_ptr;
        char *end_ptr;
        uint32_t n_reclaim_blockers = 0;
    };

}  // namespace cl

#endif  // CL_SLAB_ALLOCATOR_H
