#include "memory/slab_allocator.h"

#include "runtime/fatal.h"

#include <sys/mman.h>

namespace cl
{
    SlabAllocator::SlabAllocator(size_t offset, size_t _slab_size)
        : slab_size(_slab_size)
    {
        void *addr = mmap(nullptr, slab_size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANON, -1, 0);
        if(addr == MAP_FAILED)
        {
            fatal("failed to allocate slab");
        }
        start_ptr = reinterpret_cast<char *>(addr);
        assert(reinterpret_cast<uintptr_t>(start_ptr) % SlabLookupGranuleSize ==
               0);
        curr_ptr = start_ptr + offset;
        char *mapping_end = start_ptr + slab_size;
        size_t usable_bytes = static_cast<size_t>(mapping_end - curr_ptr);
        usable_bytes -= usable_bytes % value_ptr_granularity;
        allocation_end_ptr = curr_ptr + usable_bytes;
        first_object_header = curr_ptr;
    }

    SlabAllocator::~SlabAllocator() { munmap(start_ptr, slab_size); }

    void SlabAllocator::reset()
    {
        assert(!has_reclaim_blockers());
        curr_ptr = first_object_header;
        valid_object_bitmap = {};
    }

}  // namespace cl
