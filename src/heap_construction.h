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
    struct HasNativeObjectSize : std::false_type
    {
    };

    template <typename T>
    struct HasNativeObjectSize<
        T, std::void_t<decltype(T::native_object_size_kind)>> : std::true_type
    {
    };

    template <typename T, typename... Args>
    size_t allocation_size_for(Args &&...args)
    {
        static_assert(std::is_base_of_v<HeapObject, T>);
        static_assert(HasHeapNativeLayoutId<T>::value);
        static_assert(HasNativeObjectSize<T>::value);

        if constexpr(T::native_object_size_kind == ObjectSizeKind::StaticSize)
        {
            return sizeof(T);
        }
        else
        {
            return T::size_for(std::forward<Args>(args)...);
        }
    }

    template <typename T, typename Heap, typename... Args>
    T *construct_object(Heap *heap, Args &&...args)
    {
        size_t object_size_in_bytes =
            allocation_size_for<T>(std::forward<Args>(args)...);
        char *memory = heap->allocate(object_size_in_bytes);
        T *obj = new(memory) T(std::forward<Args>(args)...);
        assert(obj->HeapObject::native_layout_id() == T::native_layout);
        heap->mark_valid_object(obj);
        return obj;
    }
}  // namespace cl

#endif  // CL_HEAP_CONSTRUCTION_H
