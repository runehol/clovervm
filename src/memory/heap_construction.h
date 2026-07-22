#ifndef CL_HEAP_CONSTRUCTION_H
#define CL_HEAP_CONSTRUCTION_H

#include "memory/native_layout_declarations.h"
#include "memory/slab_allocator.h"
#include "object_model/value.h"
#include <memory>
#include <type_traits>
#include <utility>

namespace cl
{
    struct HeapAllocation
    {
        char *memory;
        SlabAllocator *slab;
    };

    template <typename T>
    concept HasHeapNativeLayoutIdConcept = requires { T::native_layout; };

    template <typename T>
    struct HasHeapNativeLayoutId
        : std::bool_constant<HasHeapNativeLayoutIdConcept<T>>
    {
    };

    template <typename T>
    concept HasNativeObjectSizeConcept =
        requires { T::native_object_size_kind; };

    template <typename T>
    struct HasNativeObjectSize
        : std::bool_constant<HasNativeObjectSizeConcept<T>>
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
        HeapAllocation allocation = heap->allocate(object_size_in_bytes);
        T *obj = std::construct_at(reinterpret_cast<T *>(allocation.memory),
                                   std::forward<Args>(args)...);
        assert(obj->HeapObject::native_layout_id() == T::native_layout);
        allocation.slab->mark_valid_object(obj);
        return obj;
    }
}  // namespace cl

#endif  // CL_HEAP_CONSTRUCTION_H
