#ifndef CL_THREAD_LOCAL_HEAP_H
#define CL_THREAD_LOCAL_HEAP_H

#include "global_heap.h"
#include "heap_construction.h"
#include "value.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include <utility>
#include <vector>

namespace cl
{
    /* thread local heap, for fast lockless allocation in the common case */
    class ThreadLocalHeap
    {
    public:
        ThreadLocalHeap(GlobalHeap *_global_heap);
        ~ThreadLocalHeap();

        // allocation fast path
        char *allocate(size_t n_bytes)
        {
            if(likely(n_bytes < LargeAllocationSize))
            {
                char *result = local_allocator->allocate(n_bytes);
                if(likely(result != nullptr))
                {
                    return result;
                }
            }

            return allocate_slow(n_bytes);
        }

        void switch_to_new_slabs();
        void release_for_failed_construction(char *memory);
        void adopt_epoch_state_from(ThreadLocalHeap &child);
        template <typename Fn> void for_each_epoch_slab(Fn &&fn) const
        {
            for(SlabAllocator *allocator: epoch_slabs_since_reclamation)
            {
                fn(allocator);
            }
        }
        [[nodiscard]] std::vector<SlabAllocator *> finish_reclamation_epoch();
        void mark_valid_object(HeapObject *obj)
        {
            global_heap->mark_valid_object(obj);
        }
        size_t epoch_slab_count() const
        {
            return epoch_slabs_since_reclamation.size();
        }
        uint64_t ordinary_inactive_slabs_since_reclamation_count() const
        {
            return ordinary_inactive_slabs_since_reclamation;
        }
        uint64_t dedicated_large_bytes_since_reclamation_count() const
        {
            return dedicated_large_bytes_since_reclamation;
        }

        template <typename T, typename... Args> T *make(Args &&...args)
        {
            static_assert(std::is_base_of_v<HeapObject, T>);
            static_assert(HasObjectLayout<T>::value);
            if constexpr(T::has_dynamic_layout)
            {
                return construct_dynamic_object<T>(this,
                                                   std::forward<Args>(args)...);
            }
            else
            {
                return construct_static_object<ThreadLocalHeap, T>(
                    this, std::forward<Args>(args)...);
            }
        }

    private:
        NOINLINE char *allocate_slow(size_t n_bytes);
        void add_active_allocator_pin(SlabAllocator *allocator)
        {
            allocator->add_active_allocator_pin();
        }
        void drop_active_allocator_pin(SlabAllocator *allocator)
        {
            allocator->drop_active_allocator_pin();
        }
        void remember_epoch_slab(SlabAllocator *allocator);
        void remember_ordinary_epoch_slab(SlabAllocator *allocator);
        void remember_dedicated_epoch_slab(SlabAllocator *allocator,
                                           size_t n_bytes);
        bool forget_dedicated_epoch_slab_for_failed_construction(
            SlabAllocator *allocator);
        void drop_epoch_discovery_pins_and_release_slabs();
        bool owns_epoch_slab(SlabAllocator *allocator) const;

        GlobalHeap *global_heap;
        SlabAllocator *local_allocator;
        std::vector<SlabAllocator *> epoch_slabs_since_reclamation;
        uint64_t ordinary_inactive_slabs_since_reclamation = 0;
        uint64_t dedicated_large_bytes_since_reclamation = 0;
    };
}  // namespace cl

#endif  // CL_THREAD_LOCAL_HEAP_H
