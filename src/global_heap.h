#ifndef CL_GLOBAL_HEAP_H
#define CL_GLOBAL_HEAP_H

#include "heap_construction.h"
#include "slab_allocator.h"
#include "typed_value.h"
#include "value.h"
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace cl
{
    static constexpr uintptr_t SlabLookupGranuleShift = 12;
    static constexpr size_t SlabLookupGranuleSize = size_t(1)
                                                    << SlabLookupGranuleShift;
    static constexpr size_t DefaultSlabSize = 65536;
    static constexpr size_t LargeAllocationSize = DefaultSlabSize / 2;

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

        char *allocate_large_object(
            size_t n_bytes);  // slow path allocation for large objects

        char *allocate(size_t n_bytes) { return allocate_global(n_bytes); }
        char *allocate_global(size_t n_bytes);
        void drop_reclaim_blocker_for_failed_construction(char *memory);

        template <typename T, typename... Args>
        T *make_global_internal_raw(Args &&...args)
        {
            static_assert(std::is_base_of_v<HeapObject, T>);
            if constexpr(HasObjectLayout<T>::value && T::has_dynamic_layout)
            {
                return construct_dynamic_object<T>(this,
                                                   std::forward<Args>(args)...);
            }
            else
            {
                return construct_static_object<GlobalHeap, T>(
                    this, std::forward<Args>(args)...);
            }
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

    private:
        friend class SlabAllocator;

        void register_slab_pages_locked(SlabAllocator *slab);
        void unregister_slab_pages_locked(SlabAllocator *slab);
        SlabAllocator *make_new_slab(size_t actual_slab_size);
        void reclaim_slab(SlabAllocator *slab);

        std::mutex heap_mutex;
        std::deque<std::unique_ptr<SlabAllocator>> slabs;
        std::unordered_map<uintptr_t, SlabAllocator *> slab_lookup;
        size_t offset;
        size_t slab_size;
        std::unique_ptr<ThreadLocalHeap> global_allocator;
        std::mutex global_allocator_mutex;
    };
}  // namespace cl

#endif  // CL_GLOBAL_HEAP_H
