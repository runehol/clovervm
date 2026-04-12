#ifndef CL_HEAP_H
#define CL_HEAP_H

#include "slab_allocator.h"
#include "typed_value.h"
#include "value.h"
#include <cstdlib>
#include <deque>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>

namespace cl
{

    static constexpr size_t DefaultSlabSize = 65536;
    static constexpr size_t LargeAllocationSize = DefaultSlabSize / 2;

    class ThreadLocalHeap;
    struct Object;

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

        void *allocate_large_object(
            size_t n_bytes);  // slow path allocation for large objects

        void *allocate_global(size_t n_bytes);

        template <typename T, typename... Args>
        T *make_global_raw(Args &&...args)
        {
            static_assert(std::is_base_of_v<Object, T>);
            return new(allocate_global(sizeof(T)))
                T(std::forward<Args>(args)...);
        }

        template <typename T, typename... Args>
        TValue<T> make_global_value(Args &&...args)
        {
            return TValue<T>::from_oop(
                make_global_raw<T>(std::forward<Args>(args)...));
        }

        template <typename T, typename... Args>
        T *make_global_sized_raw(size_t n_bytes, Args &&...args)
        {
            static_assert(std::is_base_of_v<Object, T>);
            return new(allocate_global(n_bytes)) T(std::forward<Args>(args)...);
        }

        template <typename T, typename... Args>
        TValue<T> make_global_sized_value(size_t n_bytes, Args &&...args)
        {
            return TValue<T>::from_oop(
                make_global_sized_raw<T>(n_bytes, std::forward<Args>(args)...));
        }

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
            }
            else
            {
                local_allocator = global_heap->make_new_slab();
                return local_allocator->allocate(n_bytes);
            }
        }

        template <typename T, typename... Args> T *make(Args &&...args)
        {
            static_assert(std::is_base_of_v<Object, T>);
            return new(allocate(sizeof(T))) T(std::forward<Args>(args)...);
        }

        template <typename T, typename... Args>
        T *make_sized(size_t n_bytes, Args &&...args)
        {
            static_assert(std::is_base_of_v<Object, T>);
            return new(allocate(n_bytes)) T(std::forward<Args>(args)...);
        }

    private:
        GlobalHeap *global_heap;
        SlabAllocator *local_allocator;
    };

}  // namespace cl

#endif  // CL_HEAP_H
