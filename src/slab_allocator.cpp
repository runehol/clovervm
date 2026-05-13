#include "slab_allocator.h"

#include "heap.h"

#include <stdexcept>
#include <sys/mman.h>

namespace cl
{
    SlabAllocator::SlabAllocator(GlobalHeap *_global_heap, size_t offset,
                                 size_t slab_size)
        : global_heap(_global_heap)
    {
        void *addr = mmap(nullptr, slab_size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANON, -1, 0);
        if(addr == MAP_FAILED)
        {
            throw std::runtime_error("Out of memory - panic!");
        }
        start_ptr = reinterpret_cast<char *>(addr);
        assert(reinterpret_cast<uintptr_t>(start_ptr) % SlabLookupGranuleSize ==
               0);
        curr_ptr = start_ptr + offset;
        end_ptr = start_ptr + slab_size;
    }
    SlabAllocator::~SlabAllocator() { munmap(start_ptr, end_ptr - start_ptr); }

    void SlabAllocator::add_reclaim_blocker() { ++n_reclaim_blockers; }

    void SlabAllocator::drop_reclaim_blocker()
    {
        assert(n_reclaim_blockers > 0);
        --n_reclaim_blockers;
        if(n_reclaim_blockers == 0)
        {
            global_heap->reclaim_slab(this);
        }
    }

}  // namespace cl
