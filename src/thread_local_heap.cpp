#include "thread_local_heap.h"

#include <algorithm>
#include <iterator>

namespace cl
{
    ThreadLocalHeap::ThreadLocalHeap(GlobalHeap *_global_heap)
        : global_heap(_global_heap),
          local_allocator(global_heap->make_new_slab())
    {
        add_active_allocator_pin(local_allocator);
        remember_ordinary_epoch_slab(local_allocator);
    }

    ThreadLocalHeap::~ThreadLocalHeap()
    {
        drop_active_allocator_pin(local_allocator);
        drop_epoch_discovery_pins_and_release_slabs();
    }

    char *ThreadLocalHeap::allocate_slow(size_t n_bytes)
    {
        if(n_bytes >= LargeAllocationSize)
        {
            char *memory = global_heap->allocate_large_object(n_bytes);
            remember_dedicated_epoch_slab(
                global_heap->slab_for_address_unlocked(memory), n_bytes);
            return memory;
        }

        SlabAllocator *old_allocator = local_allocator;
        SlabAllocator *new_allocator = global_heap->make_new_slab();
        local_allocator = new_allocator;
        add_active_allocator_pin(local_allocator);
        remember_ordinary_epoch_slab(local_allocator);
        ++ordinary_inactive_slabs_since_reclamation;
        drop_active_allocator_pin(old_allocator);
        char *memory = local_allocator->allocate(n_bytes);
        assert(memory != nullptr);
        return memory;
    }

    void ThreadLocalHeap::switch_to_new_slabs()
    {
        SlabAllocator *old_allocator = local_allocator;
        local_allocator = global_heap->make_new_slab();
        add_active_allocator_pin(local_allocator);
        remember_ordinary_epoch_slab(local_allocator);
        ++ordinary_inactive_slabs_since_reclamation;
        drop_active_allocator_pin(old_allocator);
    }

    void ThreadLocalHeap::adopt_epoch_state_from(ThreadLocalHeap &child)
    {
        assert(this != &child);
        assert(global_heap == child.global_heap);
        for(SlabAllocator *allocator: child.epoch_slabs_since_reclamation)
        {
            assert(!owns_epoch_slab(allocator));
        }

        epoch_slabs_since_reclamation.insert(
            epoch_slabs_since_reclamation.end(),
            std::make_move_iterator(
                child.epoch_slabs_since_reclamation.begin()),
            std::make_move_iterator(child.epoch_slabs_since_reclamation.end()));
        ordinary_inactive_slabs_since_reclamation +=
            child.ordinary_inactive_slabs_since_reclamation;
        dedicated_large_bytes_since_reclamation +=
            child.dedicated_large_bytes_since_reclamation;

        child.epoch_slabs_since_reclamation.clear();
        child.ordinary_inactive_slabs_since_reclamation = 0;
        child.dedicated_large_bytes_since_reclamation = 0;
    }

    std::vector<SlabAllocator *> ThreadLocalHeap::finish_reclamation_epoch()
    {
        std::vector<SlabAllocator *> finished_epoch =
            std::move(epoch_slabs_since_reclamation);
        ordinary_inactive_slabs_since_reclamation = 0;
        dedicated_large_bytes_since_reclamation = 0;
        remember_ordinary_epoch_slab(local_allocator);
        return finished_epoch;
    }

    void ThreadLocalHeap::remember_epoch_slab(SlabAllocator *allocator)
    {
        assert(std::find(epoch_slabs_since_reclamation.begin(),
                         epoch_slabs_since_reclamation.end(),
                         allocator) == epoch_slabs_since_reclamation.end());
        allocator->add_epoch_discovery_pin();
        epoch_slabs_since_reclamation.push_back(allocator);
    }

    void ThreadLocalHeap::remember_ordinary_epoch_slab(SlabAllocator *allocator)
    {
        remember_epoch_slab(allocator);
    }

    void
    ThreadLocalHeap::remember_dedicated_epoch_slab(SlabAllocator *allocator,
                                                   size_t n_bytes)
    {
        remember_epoch_slab(allocator);
        dedicated_large_bytes_since_reclamation += n_bytes;
    }

    void ThreadLocalHeap::drop_epoch_discovery_pins_and_release_slabs()
    {
        for(SlabAllocator *allocator: epoch_slabs_since_reclamation)
        {
            allocator->drop_epoch_discovery_pin();
        }
        for(SlabAllocator *allocator: epoch_slabs_since_reclamation)
        {
            global_heap->release_slab_if_empty(allocator);
        }
    }

    bool ThreadLocalHeap::owns_epoch_slab(SlabAllocator *allocator) const
    {
        return std::find(epoch_slabs_since_reclamation.begin(),
                         epoch_slabs_since_reclamation.end(),
                         allocator) != epoch_slabs_since_reclamation.end();
    }
}  // namespace cl
