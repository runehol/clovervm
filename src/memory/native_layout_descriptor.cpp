#include "memory/native_layout_descriptor.h"

#include <cassert>

namespace cl
{
    namespace
    {
        size_t resolve_object_size(const HeapObject *obj,
                                   const ObjectSizeDescriptor &descriptor)
        {
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
            native_layout_descriptor_for(object->native_layout_id()).release;
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
            native_layout_descriptor_for(object->native_layout_id()).release;
        if(descriptor.visit_weak_units != nullptr)
        {
            descriptor.visit_weak_units(object, visitor);
        }
    }

    size_t object_size_in_bytes(const HeapObject *obj)
    {
        assert(obj != nullptr);
        const ObjectSizeDescriptor &descriptor =
            native_layout_descriptor_for(obj->native_layout_id()).object_size;

        return resolve_object_size(obj, descriptor);
    }

    NativeLayoutInfo native_layout_info(const HeapObject *obj)
    {
        assert(obj != nullptr);
        const NativeLayoutDescriptor &descriptor =
            native_layout_descriptor_for(obj->native_layout_id());
        size_t allocated_size =
            resolve_object_size(obj, descriptor.object_size);
        size_t copy_size = descriptor.custom_copy_size_in_bytes != nullptr
                               ? descriptor.custom_copy_size_in_bytes(obj)
                               : allocated_size;
        assert(copy_size <= allocated_size);
        return NativeLayoutInfo{
            allocated_size, copy_size,
            resolve_ordinary_reference_span(obj, descriptor.release)};
    }

}  // namespace cl
