#include "slab_allocator.h"

#include <stdexcept>
#include <sys/mman.h>

namespace cl
{

    SlabAllocator::SlabAllocator(size_t offset, size_t slab_size)
    {
        void *addr = mmap(nullptr, slab_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
        if(addr == MAP_FAILED)
        {
            throw std::runtime_error("Out of memory - panic!");
        }
        start_ptr = reinterpret_cast<char*>(addr);
        curr_ptr = start_ptr+offset;
        end_ptr = start_ptr + slab_size;
    }
    SlabAllocator::~SlabAllocator()
    {
        munmap(start_ptr, end_ptr-start_ptr);
    }

}
