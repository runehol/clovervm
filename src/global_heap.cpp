#include "global_heap.h"

#include "thread_local_heap.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>

namespace cl
{
    namespace
    {
        [[noreturn]] NOINLINE void
        abort_missing_slab_for_address(const void *ptr)
        {
            (void)ptr;
            std::abort();
        }
    }  // namespace

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
        if(actual_slab_size == slab_size)
        {
            SlabAllocator *cached_slab = try_take_cached_empty_slab_locked();
            if(cached_slab != nullptr)
            {
                register_slab_pages_locked(cached_slab);
                return cached_slab;
            }
        }

        SlabAllocator *slab =
            slabs
                .emplace_front(
                    std::make_unique<SlabAllocator>(offset, actual_slab_size))
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
        if(unlikely(it == slab_lookup.end()))
        {
            assert(false &&
                   "address is not covered by this heap's slab lookup");
            abort_missing_slab_for_address(ptr);
        }
        return it->second;
    }

    void GlobalHeap::release_slab_locked(SlabAllocator *slab)
    {
        unregister_slab_pages_locked(slab);
        erase_slab_locked(slab);
    }

    void GlobalHeap::erase_slab_locked(SlabAllocator *slab)
    {
        auto it = std::find_if(
            slabs.begin(), slabs.end(),
            [slab](const std::unique_ptr<SlabAllocator> &owned_slab) {
                return owned_slab.get() == slab;
            });
        assert(it != slabs.end());
        slabs.erase(it);
    }

    void GlobalHeap::cache_empty_slab_locked(SlabAllocator *slab)
    {
        assert(slab != nullptr);
        assert(slab->size() == slab_size);
        unregister_slab_pages_locked(slab);
        slab->reset();
        empty_slab_cache.push_back(slab);
        if(empty_slab_cache.size() > EmptySlabCacheCapacity)
        {
            SlabAllocator *oldest = empty_slab_cache.front();
            empty_slab_cache.pop_front();
            erase_slab_locked(oldest);
        }
    }

    SlabAllocator *GlobalHeap::try_take_cached_empty_slab_locked()
    {
        if(empty_slab_cache.empty())
        {
            return nullptr;
        }

        SlabAllocator *slab = empty_slab_cache.back();
        empty_slab_cache.pop_back();
        return slab;
    }

    bool GlobalHeap::release_slab_if_empty(SlabAllocator *slab)
    {
        assert(slab != nullptr);
        const std::lock_guard<std::mutex> lock(heap_mutex);
        if(slab->has_reclaim_blockers())
        {
            return false;
        }

        if(slab->size() == slab_size)
        {
            cache_empty_slab_locked(slab);
            return true;
        }

        release_slab_locked(slab);
        return true;
    }

    HeapAllocation GlobalHeap::allocate_large_object(size_t n_bytes)
    {
        size_t required_slab_size =
            n_bytes + value_ptr_granularity -
            offset;  // make sure we have space for the pointer offset
        SlabAllocator *single_allocator = make_new_slab(required_slab_size);
        char *memory = single_allocator->allocate(n_bytes);
        assert(memory != nullptr);
        return {memory, single_allocator};
    }

    HeapAllocation GlobalHeap::allocate_global(size_t n_bytes)
    {
        const std::lock_guard<std::mutex> lock(global_allocator_mutex);
        if(global_allocator == nullptr)
            global_allocator = std::make_unique<ThreadLocalHeap>(this);
        return global_allocator->allocate(n_bytes);
    }

    void GlobalHeap::mark_valid_object(HeapObject *obj)
    {
        slab_for_object_unlocked(obj)->mark_valid_object(obj);
    }

    uint64_t GlobalHeap::total_reclaim_blockers_for_testing() const
    {
        const std::lock_guard<std::mutex> lock(heap_mutex);
        uint64_t total = 0;
        for(const std::unique_ptr<SlabAllocator> &slab: slabs)
        {
            total += slab->count_reclaim_blockers_slow();
        }
        return total;
    }

    uint64_t GlobalHeap::count_valid_objects_slow() const
    {
        const std::lock_guard<std::mutex> lock(heap_mutex);
        uint64_t total = 0;
        for(const std::unique_ptr<SlabAllocator> &slab: slabs)
        {
            total += slab->count_valid_objects_slow();
        }
        return total;
    }

    bool GlobalHeap::has_slab_for_address_for_testing(const void *ptr) const
    {
        const std::lock_guard<std::mutex> lock(heap_mutex);
        return slab_lookup.find(slab_lookup_key_for_address(ptr)) !=
               slab_lookup.end();
    }

    size_t GlobalHeap::empty_slab_cache_size_for_testing() const
    {
        const std::lock_guard<std::mutex> lock(heap_mutex);
        return empty_slab_cache.size();
    }

    GlobalHeap::~GlobalHeap() = default;
}  // namespace cl
