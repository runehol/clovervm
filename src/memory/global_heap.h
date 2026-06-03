#ifndef CL_GLOBAL_HEAP_H
#define CL_GLOBAL_HEAP_H

#include "memory/heap_constants.h"
#include "memory/heap_construction.h"
#include "memory/slab_allocator.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace cl
{
    uintptr_t slab_lookup_key_for_address(const void *ptr);

    class ThreadLocalHeap;

    /* global heap, shared between threads */

    class GlobalHeap
    {
    public:
        GlobalHeap(size_t _offset, size_t _slab_size);
        ~GlobalHeap();

        static GlobalHeap refcounted_heap(size_t slab_size = DefaultSlabSize)
        {
            return GlobalHeap(value_refcounted_ptr_tag, slab_size);
        }

        static GlobalHeap interned_heap(size_t slab_size = DefaultSlabSize)
        {
            return GlobalHeap(value_interned_ptr_tag, slab_size);
        }

        HeapAllocation allocate_large_object(
            size_t n_bytes);  // slow path allocation for large objects

        HeapAllocation allocate(size_t n_bytes)
        {
            return allocate_global(n_bytes);
        }
        HeapAllocation allocate_global(size_t n_bytes);
        void mark_valid_object(HeapObject *obj);
        bool release_slab_if_empty(SlabAllocator *slab);

        template <typename T, typename... Args>
        T *make_global_internal_raw(Args &&...args)
        {
            static_assert(std::is_base_of_v<HeapObject, T>);
            static_assert(HasNativeObjectSize<T>::value);
            return construct_object<T>(this, std::forward<Args>(args)...);
        }

        template <typename T, typename... Args>
        TValue<T> make_global_internal_value(Args &&...args)
        {
            return TValue<T>::from_oop(
                make_global_internal_raw<T>(std::forward<Args>(args)...));
        }

        SlabAllocator *make_new_slab();

        // Caller must ensure that this heap's slab lookup map is stable, for
        // example during safepoint reclamation or while holding heap_mutex.
        SlabAllocator *slab_for_address_unlocked(const void *ptr) const;
        SlabAllocator *slab_for_object_unlocked(const HeapObject *obj) const
        {
            return slab_for_address_unlocked(obj);
        }
        uint64_t total_reclaim_blockers_for_testing() const;
        uint64_t count_valid_objects_slow() const;
        bool has_slab_for_address_for_testing(const void *ptr) const;
        size_t empty_slab_cache_size_for_testing() const;

    private:
        static constexpr size_t EmptySlabCacheCapacity = 10;

        void register_slab_pages_locked(SlabAllocator *slab);
        void unregister_slab_pages_locked(SlabAllocator *slab);
        SlabAllocator *make_new_slab(size_t actual_slab_size);
        void release_slab_locked(SlabAllocator *slab);
        void erase_slab_locked(SlabAllocator *slab);
        void cache_empty_slab_locked(SlabAllocator *slab);
        SlabAllocator *try_take_cached_empty_slab_locked();

        mutable std::mutex heap_mutex;
        std::deque<std::unique_ptr<SlabAllocator>> slabs;
        std::deque<SlabAllocator *> empty_slab_cache;
        std::unordered_map<uintptr_t, SlabAllocator *> slab_lookup;
        size_t offset;
        size_t slab_size;
        std::unique_ptr<ThreadLocalHeap> global_allocator;
        std::mutex global_allocator_mutex;
    };
}  // namespace cl

#endif  // CL_GLOBAL_HEAP_H
