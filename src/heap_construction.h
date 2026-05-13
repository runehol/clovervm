#ifndef CL_HEAP_CONSTRUCTION_H
#define CL_HEAP_CONSTRUCTION_H

#include "value.h"
#include <new>
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

    template <typename Heap, typename T, typename... Args>
    T *construct_static_object(Heap *heap, Args &&...args)
    {
        static_assert(std::is_base_of_v<HeapObject, T>);

        char *memory = heap->allocate(sizeof(T));
        try
        {
            return new(memory) T(std::forward<Args>(args)...);
        }
        catch(...)
        {
            heap->drop_reclaim_blocker_for_failed_construction(memory);
            throw;
        }
    }

    template <typename T, typename Heap, typename... Args>
    T *construct_dynamic_object(Heap *heap, Args &&...args)
    {
        static_assert(std::is_base_of_v<HeapObject, T>);
        static_assert(HasObjectLayout<T>::value && T::has_dynamic_layout);

        DynamicLayoutSpec spec = T::layout_spec_for(args...);
        uint32_t value_offset_in_words = T::static_value_offset_in_words();
        assert(expanded_layout_fits(value_offset_in_words));

        size_t object_size_in_bytes =
            size_t(spec.object_size_in_16byte_units) * 16;
        if(compact_layout_fits(spec.object_size_in_16byte_units,
                               value_offset_in_words, spec.value_count))
        {
            HeapLayout layout = encode_compact_layout_unchecked(
                spec.object_size_in_16byte_units, value_offset_in_words,
                spec.value_count);
            char *memory = heap->allocate(object_size_in_bytes);
            try
            {
                return new(memory) T(layout, std::forward<Args>(args)...);
            }
            catch(...)
            {
                heap->drop_reclaim_blocker_for_failed_construction(memory);
                throw;
            }
        }

        size_t allocation_size_in_bytes =
            sizeof(ExpandedHeader) + object_size_in_bytes;
        char *memory = heap->allocate(allocation_size_in_bytes);
        try
        {
            ExpandedHeader *header = reinterpret_cast<ExpandedHeader *>(memory);
            header->object_size_in_16byte_units =
                spec.object_size_in_16byte_units;
            header->value_count = spec.value_count;

            HeapLayout layout =
                encode_expanded_layout_unchecked(value_offset_in_words);
            return new(memory + sizeof(ExpandedHeader))
                T(layout, std::forward<Args>(args)...);
        }
        catch(...)
        {
            heap->drop_reclaim_blocker_for_failed_construction(memory);
            throw;
        }
    }
}  // namespace cl

#endif  // CL_HEAP_CONSTRUCTION_H
