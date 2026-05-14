#include "thread_local_heap.h"

namespace cl
{
    ThreadLocalHeap::ThreadLocalHeap(GlobalHeap *_global_heap)
        : global_heap(_global_heap),
          local_allocator(global_heap->make_new_slab())
    {
        add_allocator_reclaim_blocker(local_allocator);
    }

    ThreadLocalHeap::~ThreadLocalHeap()
    {
        drop_allocator_reclaim_blocker(local_allocator);
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
        add_allocator_reclaim_blocker(local_allocator);
        drop_allocator_reclaim_blocker(old_allocator);
        char *memory = local_allocator->allocate(n_bytes);
        assert(memory != nullptr);
        local_allocator->add_reclaim_blocker();
        return memory;
    }

    void ThreadLocalHeap::switch_to_new_slabs()
    {
        SlabAllocator *old_allocator = local_allocator;
        local_allocator = global_heap->make_new_slab();
        add_allocator_reclaim_blocker(local_allocator);
        drop_allocator_reclaim_blocker(old_allocator);
    }
}  // namespace cl
