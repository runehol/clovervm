#include "global_heap.h"

#include "thread_local_heap.h"

#include <algorithm>
#include <cassert>

namespace cl
{
    uintptr_t slab_lookup_key_for_address(const void *ptr)
    {
        return reinterpret_cast<uintptr_t>(ptr) >> SlabLookupGranuleShift;
    }

    GlobalHeap::GlobalHeap(size_t _offset, size_t _slab_size)
        : offset(_offset), slab_size(_slab_size)
    {
        assert(SlabLookupGranuleSize == (size_t(1) << SlabLookupGranuleShift));
        assert(offset < value_ptr_granularity);
    }

    SlabAllocator *GlobalHeap::make_new_slab()
    {
        return make_new_slab(slab_size);
    }

    SlabAllocator *GlobalHeap::make_new_slab(size_t actual_slab_size)
    {
        const std::lock_guard<std::mutex> lock(heap_mutex);
        SlabAllocator *slab =
            slabs
                .emplace_front(std::make_unique<SlabAllocator>(
                    this, offset, actual_slab_size))
                .get();
        register_slab_pages_locked(slab);
        return slab;
    }

    void GlobalHeap::register_slab_pages_locked(SlabAllocator *slab)
    {
        assert(slab != nullptr);
        assert(reinterpret_cast<uintptr_t>(slab->start()) %
                   SlabLookupGranuleSize ==
               0);
        uintptr_t first_key = slab_lookup_key_for_address(slab->start());
        uintptr_t last_key = slab_lookup_key_for_address(slab->end() - 1);
        for(uintptr_t key = first_key; key <= last_key; ++key)
        {
            bool inserted = slab_lookup.emplace(key, slab).second;
            assert(inserted);
            (void)inserted;
        }
    }

    void GlobalHeap::unregister_slab_pages_locked(SlabAllocator *slab)
    {
        assert(slab != nullptr);
        uintptr_t first_key = slab_lookup_key_for_address(slab->start());
        uintptr_t last_key = slab_lookup_key_for_address(slab->end() - 1);
        for(uintptr_t key = first_key; key <= last_key; ++key)
        {
            size_t erased = slab_lookup.erase(key);
            assert(erased == 1);
            (void)erased;
        }
    }

    SlabAllocator *GlobalHeap::slab_for_address_unlocked(const void *ptr) const
    {
        auto it = slab_lookup.find(slab_lookup_key_for_address(ptr));
        assert(it != slab_lookup.end());
        return it->second;
    }

    void GlobalHeap::reclaim_slab(SlabAllocator *slab)
    {
        const std::lock_guard<std::mutex> lock(heap_mutex);
        unregister_slab_pages_locked(slab);
        auto it = std::find_if(
            slabs.begin(), slabs.end(),
            [slab](const std::unique_ptr<SlabAllocator> &owned_slab) {
                return owned_slab.get() == slab;
            });
        assert(it != slabs.end());
        slabs.erase(it);
    }

    char *GlobalHeap::allocate_large_object(size_t n_bytes)
    {
        size_t required_slab_size =
            n_bytes + value_ptr_granularity -
            offset;  // make sure we have space for the pointer offset
        SlabAllocator *single_allocator = make_new_slab(required_slab_size);
        char *memory = single_allocator->allocate(n_bytes);
        assert(memory != nullptr);
        single_allocator->add_reclaim_blocker();
        return memory;
    }

    char *GlobalHeap::allocate_global(size_t n_bytes)
    {
        const std::lock_guard<std::mutex> lock(global_allocator_mutex);
        if(global_allocator == nullptr)
            global_allocator = std::make_unique<ThreadLocalHeap>(this);
        return global_allocator->allocate(n_bytes);
    }

    void GlobalHeap::drop_reclaim_blocker_for_failed_construction(char *memory)
    {
        slab_for_address_unlocked(memory)->drop_reclaim_blocker();
    }

    uint64_t GlobalHeap::total_reclaim_blockers_for_testing() const
    {
        const std::lock_guard<std::mutex> lock(heap_mutex);
        uint64_t total = 0;
        for(const std::unique_ptr<SlabAllocator> &slab: slabs)
        {
            total += slab->reclaim_blocker_count();
        }
        return total;
    }

    GlobalHeap::~GlobalHeap() = default;
}  // namespace cl
