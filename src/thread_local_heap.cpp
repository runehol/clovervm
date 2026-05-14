#include "thread_local_heap.h"

namespace cl
{
    ThreadLocalHeap::ThreadLocalHeap(GlobalHeap *_global_heap)
        : global_heap(_global_heap),
          local_allocator(global_heap->make_new_slab())
    {
        add_active_allocator_pin(local_allocator);
    }

    ThreadLocalHeap::~ThreadLocalHeap()
    {
        drop_active_allocator_pin(local_allocator);
        global_heap->release_slab_if_empty(local_allocator);
    }

    char *ThreadLocalHeap::allocate_slow(size_t n_bytes)
    {
        if(n_bytes >= LargeAllocationSize)
        {
            return global_heap->allocate_large_object(n_bytes);
        }

        SlabAllocator *old_allocator = local_allocator;
        SlabAllocator *new_allocator = global_heap->make_new_slab();
        local_allocator = new_allocator;
        add_active_allocator_pin(local_allocator);
        drop_active_allocator_pin(old_allocator);
        global_heap->release_slab_if_empty(old_allocator);
        char *memory = local_allocator->allocate(n_bytes);
        assert(memory != nullptr);
        return memory;
    }

    void ThreadLocalHeap::switch_to_new_slabs()
    {
        SlabAllocator *old_allocator = local_allocator;
        local_allocator = global_heap->make_new_slab();
        add_active_allocator_pin(local_allocator);
        drop_active_allocator_pin(old_allocator);
        global_heap->release_slab_if_empty(old_allocator);
    }
}  // namespace cl
