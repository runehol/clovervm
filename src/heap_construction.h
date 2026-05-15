#ifndef CL_HEAP_CONSTRUCTION_H
#define CL_HEAP_CONSTRUCTION_H

#include "native_layout_declarations.h"
#include "value.h"
#include <new>
#include <type_traits>
#include <utility>

namespace cl
{
    template <typename T, typename = void>
    struct HasHeapNativeLayoutId : std::false_type
    {
    };

    template <typename T>
    struct HasHeapNativeLayoutId<T, std::void_t<decltype(T::native_layout)>>
        : std::true_type
    {
    };

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
        static_assert(HasHeapNativeLayoutId<T>::value);
        static_assert(T::native_object_size_kind == ObjectSizeKind::StaticSize);

        char *memory = heap->allocate(sizeof(T));
        T *obj = new(memory) T(std::forward<Args>(args)...);
        assert(obj->HeapObject::native_layout_id() == T::native_layout);
        heap->mark_valid_object(obj);
        return obj;
    }

    template <typename T, typename Heap, typename... Args>
    T *construct_dynamic_object(Heap *heap, Args &&...args)
    {
        static_assert(std::is_base_of_v<HeapObject, T>);
        static_assert(HasHeapNativeLayoutId<T>::value);
        static_assert(HasObjectLayout<T>::value && T::has_dynamic_layout);
        static_assert(T::native_object_size_kind == ObjectSizeKind::Custom);

        size_t object_size_in_bytes = T::size_for(args...);
        char *memory = heap->allocate(object_size_in_bytes);
        T *obj = new(memory) T(std::forward<Args>(args)...);
        assert(obj->HeapObject::native_layout_id() == T::native_layout);
        heap->mark_valid_object(obj);
        return obj;
    }
}  // namespace cl

#endif  // CL_HEAP_CONSTRUCTION_H
