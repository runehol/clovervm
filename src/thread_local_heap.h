#ifndef CL_THREAD_LOCAL_HEAP_H
#define CL_THREAD_LOCAL_HEAP_H

#include "global_heap.h"
#include "heap_construction.h"
#include "value.h"
#include <cassert>
#include <cstdlib>
#include <type_traits>
#include <utility>

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
            if(n_bytes >= LargeAllocationSize)
            {
                return global_heap->allocate_large_object(n_bytes);
            }

            char *result = local_allocator->allocate(n_bytes);
            if(likely(result != nullptr))
            {
                local_allocator->add_reclaim_blocker();
                return result;
            }

            SlabAllocator *old_allocator = local_allocator;
            SlabAllocator *new_allocator = global_heap->make_new_slab();
            local_allocator = new_allocator;
            add_allocator_reclaim_blocker();
            drop_allocator_reclaim_blocker(old_allocator);
            char *memory = local_allocator->allocate(n_bytes);
            assert(memory != nullptr);
            local_allocator->add_reclaim_blocker();
            return memory;
        }

        void drop_reclaim_blocker_for_failed_construction(char *memory);

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
        void add_allocator_reclaim_blocker();
        void drop_allocator_reclaim_blocker();
        void drop_allocator_reclaim_blocker(SlabAllocator *allocator);

        GlobalHeap *global_heap;
        SlabAllocator *local_allocator;
    };
}  // namespace cl

#endif  // CL_THREAD_LOCAL_HEAP_H
