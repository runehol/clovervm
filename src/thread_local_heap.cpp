#include "thread_local_heap.h"

namespace cl
{
    ThreadLocalHeap::ThreadLocalHeap(GlobalHeap *_global_heap)
        : global_heap(_global_heap),
          local_allocator(global_heap->make_new_slab())
    {
        add_allocator_reclaim_blocker();
    }

    ThreadLocalHeap::~ThreadLocalHeap() { drop_allocator_reclaim_blocker(); }

    void ThreadLocalHeap::add_allocator_reclaim_blocker()
    {
        local_allocator->add_reclaim_blocker();
    }

    void ThreadLocalHeap::drop_allocator_reclaim_blocker()
    {
        drop_allocator_reclaim_blocker(local_allocator);
    }

    void
    ThreadLocalHeap::drop_allocator_reclaim_blocker(SlabAllocator *allocator)
    {
        allocator->drop_reclaim_blocker();
    }

    void
    ThreadLocalHeap::drop_reclaim_blocker_for_failed_construction(char *memory)
    {
        global_heap->drop_reclaim_blocker_for_failed_construction(memory);
    }
}  // namespace cl
