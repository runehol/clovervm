#include "heap.h"
#include "slab_allocator.h"

#include <stdexcept>
#include <sys/mman.h>

namespace cl
{

    GlobalHeap::GlobalHeap(size_t _offset, size_t _slab_size)
        : offset(_offset), slab_size(_slab_size)
    {
    }

    SlabAllocator *GlobalHeap::make_new_slab()
    {
        return make_new_slab(slab_size);
    }

    SlabAllocator *GlobalHeap::make_new_slab(size_t actual_slab_size)
    {
        const std::lock_guard<std::mutex> lock(heap_mutex);
        return slabs.emplace_front(std::make_unique<SlabAllocator>(offset, actual_slab_size)).get();
    }


    void *GlobalHeap::allocate_large_object(size_t n_bytes)
    {
        size_t required_slab_size = n_bytes + value_ptr_granularity - offset; // make sure we have space for the pointer offset
        SlabAllocator *single_allocator = make_new_slab(required_slab_size);
        return single_allocator->allocate(n_bytes);
    }

    GlobalHeap::~GlobalHeap() = default;

    ThreadLocalHeap::ThreadLocalHeap(GlobalHeap *_global_heap)
        : global_heap(_global_heap), local_allocator(global_heap->make_new_slab())
    {}


}
