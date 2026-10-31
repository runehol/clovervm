#include "memory/native_layout_descriptor.h"

#include <cassert>

namespace cl
{
    namespace
    {
        template <typename T>
        bool inline_slot_capacity_is_valid(uint32_t capacity)
        {
            if constexpr(!std::is_base_of_v<Object, T>)
            {
                return false;
            }
            else if constexpr(std::is_same_v<T, Instance>)
            {
                return capacity >= T::kPredefinedInlineSlotCount &&
                       capacity <= UINT16_MAX;
            }
            else if constexpr(std::is_base_of_v<InlineSlotObject, T>)
            {
                constexpr uint32_t physical_capacity = [] {
                    if constexpr(std::is_same_v<T, ClassObject>)
                    {
                        return T::class_inline_storage_slot_count;
                    }
                    else if constexpr(std::is_same_v<T, ModuleObject>)
                    {
                        return T::module_inline_storage_slot_count;
                    }
                    else
                    {
                        return T::kInlineSlotCount;
                    }
                }();
                static_assert(T::kPredefinedInlineSlotCount <=
                              physical_capacity);
                static_assert(sizeof(T) >=
                              sizeof(InlineSlotObject) +
                                  physical_capacity * sizeof(Value));
                return capacity >= T::kPredefinedInlineSlotCount &&
                       capacity <= physical_capacity;
            }
            else
            {
                static_assert(T::kPredefinedInlineSlotCount == 0);
                return capacity == 0;
            }
        }
    }  // namespace

    bool native_inline_slot_capacity_is_valid(NativeLayoutId layout,
                                              uint32_t capacity)
    {
        switch(layout)
        {
#define CL_VALIDATE_NATIVE_INLINE_CAPACITY(type)                               \
    case type::native_layout:                                                  \
        return inline_slot_capacity_is_valid<type>(capacity)
            CL_NATIVE_LAYOUT_REGISTRY(CL_VALIDATE_NATIVE_INLINE_CAPACITY);
#undef CL_VALIDATE_NATIVE_INLINE_CAPACITY
            default:
                return false;
        }
    }

    void visit_strong_reference_slots(HeapObject *object,
                                      StrongSlotVisitor visitor)
    {
        assert(object != nullptr);
        const ReleaseDescriptor &descriptor =
            release_descriptor_for(object->native_layout_id());
        auto span = resolve_ordinary_reference_span(object, descriptor);
        assert(span.has_value() || descriptor.custom_trace_strong != nullptr);
        if(span)
        {
            visit_ordinary_reference_slots(object, *span, visitor);
        }
        if(descriptor.custom_trace_strong != nullptr)
        {
            descriptor.custom_trace_strong(object, visitor);
        }
    }

    void visit_weak_reference_units(HeapObject *object, WeakUnitVisitor visitor)
    {
        assert(object != nullptr);
        const ReleaseDescriptor &descriptor =
            release_descriptor_for(object->native_layout_id());
        if(descriptor.visit_weak_units != nullptr)
        {
            descriptor.visit_weak_units(object, visitor);
        }
    }

    size_t object_size_in_bytes(const HeapObject *obj)
    {
        assert(obj != nullptr);
        const ObjectSizeDescriptor &descriptor =
            object_size_descriptor_for(obj->native_layout_id());

        switch(descriptor.kind)
        {
            case ObjectSizeKind::StaticSize:
                return descriptor.static_size_in_bytes;
            case ObjectSizeKind::Custom:
                assert(descriptor.custom_size_in_bytes != nullptr);
                return descriptor.custom_size_in_bytes(obj);
        }

        assert(false && "object-size descriptor kind not implemented yet");
        return 0;
    }

}  // namespace cl
