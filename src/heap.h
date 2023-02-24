#ifndef CL_HEAP_H
#define CL_HEAP_H

#include <cstdlib>
#include <deque>
#include "value.h"
#include <mutex>
#include <tuple>
#include "slab_allocator.h"

namespace cl
{

    static constexpr size_t DefaultSlabSize = 65536;
    static constexpr size_t LargeAllocationSize = DefaultSlabSize/2;

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

        static GlobalHeap immortal_heap(size_t slab_size = DefaultSlabSize)
        {
            return GlobalHeap(value_immportal_ptr_tag, slab_size);
        }

        static GlobalHeap interned_heap(size_t slab_size = DefaultSlabSize)
        {
            return GlobalHeap(value_immportal_ptr_tag|value_interned_ptr_tag, slab_size);
        }

        void *allocate_large_object(size_t n_bytes); // slow path allocation for large objects

        void *allocate_global(size_t n_bytes);

        SlabAllocator *make_new_slab();


        SlabAllocator *get_active_slab();

    private:
        SlabAllocator *make_new_slab(size_t actual_slab_size);

        std::mutex heap_mutex;
        std::deque<std::unique_ptr<SlabAllocator>> slabs;
        size_t offset;
        size_t slab_size;
        std::unique_ptr<ThreadLocalHeap> global_allocator;
        std::mutex global_allocator_mutex;
    };

    /* thread local heap, for fast lockless allocation in the common case */
    class ThreadLocalHeap
    {
    public:
        ThreadLocalHeap(GlobalHeap *_global_heap);

        // allocation fast path
        void *allocate(size_t n_bytes)
        {
            void *result = local_allocator->allocate(n_bytes);
            if(likely(result != nullptr))
            {
                return result;
            }

            if(n_bytes >= LargeAllocationSize)
            {
                return global_heap->allocate_large_object(n_bytes);
            } else {
                local_allocator = global_heap->make_new_slab();
                return local_allocator->allocate(n_bytes);
            }
        }

    private:
        GlobalHeap *global_heap;
        SlabAllocator *local_allocator;
    };

}

#endif //CL_HEAP_H
