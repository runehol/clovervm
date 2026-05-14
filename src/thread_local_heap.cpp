#include "thread_local_heap.h"

#include <algorithm>

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

    void ThreadLocalHeap::release_for_failed_construction(char *memory)
    {
        SlabAllocator *allocator =
            global_heap->slab_for_address_unlocked(memory);
        if(forget_dedicated_epoch_slab_for_failed_construction(allocator))
        {
            global_heap->release_slab_if_empty(allocator);
            return;
        }

        global_heap->release_for_failed_construction(memory);
    }

    void ThreadLocalHeap::remember_ordinary_epoch_slab(SlabAllocator *allocator)
    {
        assert(std::find(slabs_active_since_reclamation.begin(),
                         slabs_active_since_reclamation.end(),
                         allocator) == slabs_active_since_reclamation.end());
        allocator->add_epoch_discovery_pin();
        slabs_active_since_reclamation.push_back(allocator);
    }

    void
    ThreadLocalHeap::remember_dedicated_epoch_slab(SlabAllocator *allocator,
                                                   size_t n_bytes)
    {
        assert(std::find_if(dedicated_slabs_since_reclamation.begin(),
                            dedicated_slabs_since_reclamation.end(),
                            [allocator](const DedicatedEpochSlab &entry) {
                                return entry.allocator == allocator;
                            }) == dedicated_slabs_since_reclamation.end());
        allocator->add_epoch_discovery_pin();
        dedicated_slabs_since_reclamation.push_back(
            DedicatedEpochSlab{allocator, n_bytes});
        dedicated_large_bytes_since_reclamation += n_bytes;
    }

    bool ThreadLocalHeap::forget_dedicated_epoch_slab_for_failed_construction(
        SlabAllocator *allocator)
    {
        auto it = std::find_if(dedicated_slabs_since_reclamation.begin(),
                               dedicated_slabs_since_reclamation.end(),
                               [allocator](const DedicatedEpochSlab &entry) {
                                   return entry.allocator == allocator;
                               });
        if(it == dedicated_slabs_since_reclamation.end())
        {
            return false;
        }

        allocator->drop_epoch_discovery_pin();
        dedicated_large_bytes_since_reclamation -= it->bytes;
        dedicated_slabs_since_reclamation.erase(it);
        return true;
    }

    void ThreadLocalHeap::drop_epoch_discovery_pins_and_release_slabs()
    {
        for(SlabAllocator *allocator: slabs_active_since_reclamation)
        {
            allocator->drop_epoch_discovery_pin();
        }
        for(const DedicatedEpochSlab &entry: dedicated_slabs_since_reclamation)
        {
            entry.allocator->drop_epoch_discovery_pin();
        }
        for(SlabAllocator *allocator: slabs_active_since_reclamation)
        {
            global_heap->release_slab_if_empty(allocator);
        }
        for(const DedicatedEpochSlab &entry: dedicated_slabs_since_reclamation)
        {
            global_heap->release_slab_if_empty(entry.allocator);
        }
    }
}  // namespace cl
