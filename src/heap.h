#ifndef CL_HEAP_H
#define CL_HEAP_H

#include "slab_allocator.h"
#include "typed_value.h"
#include "value.h"
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>

namespace cl
{
    template <typename T, typename = void>
    struct HasObjectLayout : std::false_type
    {
    };

    template <typename T>
    struct HasObjectLayout<
        T, std::void_t<decltype(T::has_dynamic_layout),
                       decltype(T::static_value_offset_in_words)>>
        : std::true_type
    {
    };

    template <typename T, typename AllocateFn, typename... Args>
    T *construct_dynamic_object(AllocateFn &&allocate_fn, Args &&...args)
    {
        static_assert(std::is_base_of_v<Object, T>);
        static_assert(HasObjectLayout<T>::value && T::has_dynamic_layout);

        DynamicLayoutSpec spec = T::layout_spec_for(args...);
        uint32_t value_offset_in_words = T::static_value_offset_in_words();
        assert(expanded_layout_fits(value_offset_in_words));

        size_t object_size_in_bytes =
            size_t(spec.object_size_in_16byte_units) * 16;
        if(compact_layout_fits(spec.object_size_in_16byte_units,
                               value_offset_in_words, spec.value_count))
        {
            T *result = new(allocate_fn(object_size_in_bytes))
                T(std::forward<Args>(args)...);
            result->layout = encode_compact_layout_unchecked(
                spec.object_size_in_16byte_units, value_offset_in_words,
                spec.value_count);
            return result;
        }

        size_t allocation_size_in_bytes =
            sizeof(ExpandedHeader) + object_size_in_bytes;
        char *allocation =
            reinterpret_cast<char *>(allocate_fn(allocation_size_in_bytes));
        ExpandedHeader *header = reinterpret_cast<ExpandedHeader *>(allocation);
        header->object_size_in_16byte_units = spec.object_size_in_16byte_units;
        header->value_count = spec.value_count;

        T *result = new(allocation + sizeof(ExpandedHeader))
            T(std::forward<Args>(args)...);
        result->layout =
            encode_expanded_layout_unchecked(value_offset_in_words);
        return result;
    }

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
            if constexpr(HasObjectLayout<T>::value && T::has_dynamic_layout)
            {
                return construct_dynamic_object<T>(
                    [this](size_t n_bytes) { return allocate_global(n_bytes); },
                    std::forward<Args>(args)...);
            }
            else
            {
                return new(allocate_global(sizeof(T)))
                    T(std::forward<Args>(args)...);
            }
        }

        template <typename T, typename... Args>
        TValue<T> make_global_value(Args &&...args)
        {
            return TValue<T>::from_oop(
                make_global_raw<T>(std::forward<Args>(args)...));
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
            static_assert(HasObjectLayout<T>::value);
            if constexpr(T::has_dynamic_layout)
            {
                return construct_dynamic_object<T>(
                    [this](size_t n_bytes) { return allocate(n_bytes); },
                    std::forward<Args>(args)...);
            }
            else
            {
                return new(allocate(sizeof(T))) T(std::forward<Args>(args)...);
            }
        }

    private:
        GlobalHeap *global_heap;
        SlabAllocator *local_allocator;
    };

}  // namespace cl

#endif  // CL_HEAP_H
