#include "native_layout_descriptor.h"

#include <cassert>

namespace cl
{
    namespace
    {
        size_t legacy_heap_layout_object_size_in_bytes(const HeapObject *obj)
        {
            assert(obj != nullptr);
            uint64_t size_in_16byte_units;
            if(layout_is_expanded(obj->layout))
            {
                size_in_16byte_units = expanded_header_for_object(obj)
                                           ->object_size_in_16byte_units;
            }
            else
            {
                size_in_16byte_units = obj->layout & object_layout_size_mask;
            }

            return size_t(size_in_16byte_units) * 16;
        }

        NativeValueSpan legacy_heap_layout_value_span(HeapObject *obj)
        {
            assert(obj != nullptr);
            uint32_t first_value_offset_in_words;
            uint64_t value_count;
            if(layout_is_expanded(obj->layout))
            {
                first_value_offset_in_words =
                    obj->layout & ~object_layout_expanded_bit;
                value_count = expanded_header_for_object(obj)->value_count;
            }
            else
            {
                first_value_offset_in_words =
                    compact_layout_value_offset_in_words(obj->layout);
                value_count = compact_layout_value_count(obj->layout);
            }

            return NativeValueSpan{reinterpret_cast<Value *>(obj) +
                                       first_value_offset_in_words,
                                   value_count};
        }

        NativeValueSpan static_value_span(HeapObject *obj,
                                          const ReleaseDescriptor &descriptor)
        {
            return NativeValueSpan{reinterpret_cast<Value *>(obj) +
                                       descriptor.value_offset_words,
                                   descriptor.static_release_count};
        }

        NativeValueSpan
        dynamic_smi_value_span(HeapObject *obj,
                               const ReleaseDescriptor &descriptor)
        {
            Value count_value = *(reinterpret_cast<Value *>(obj) +
                                  descriptor.count_offset_words);
            assert(count_value.is_smi());
            int64_t dynamic_count = count_value.get_smi();
            assert(dynamic_count >= 0);
            return NativeValueSpan{reinterpret_cast<Value *>(obj) +
                                       descriptor.value_offset_words,
                                   static_cast<uint64_t>(dynamic_count) +
                                       descriptor.additional_release_count};
        }

        NativeValueSpan
        dynamic_aux_value_span(HeapObject *obj,
                               const ReleaseDescriptor &descriptor)
        {
            return NativeValueSpan{reinterpret_cast<Value *>(obj) +
                                       descriptor.value_offset_words,
                                   obj->native_layout_aux_count_value() +
                                       descriptor.additional_release_count};
        }
    }  // namespace

    NativeValueSpan value_span_for_release(HeapObject *obj)
    {
        assert(obj != nullptr);
        const ReleaseDescriptor &descriptor =
            release_descriptor_for(obj->native_layout_id());

        switch(descriptor.kind)
        {
            case ReleaseKind::LegacyHeapLayout:
                return legacy_heap_layout_value_span(obj);
            case ReleaseKind::StaticSpan:
                return static_value_span(obj, descriptor);
            case ReleaseKind::DynamicSmiSpan:
                return dynamic_smi_value_span(obj, descriptor);
            case ReleaseKind::DynamicAuxSpan:
                return dynamic_aux_value_span(obj, descriptor);
            case ReleaseKind::Missing:
            case ReleaseKind::Custom:
                break;
        }

        assert(false && "release descriptor kind not implemented yet");
        return NativeValueSpan{nullptr, 0};
    }

    size_t object_size_in_bytes(const HeapObject *obj)
    {
        assert(obj != nullptr);
        const ObjectSizeDescriptor &descriptor =
            object_size_descriptor_for(obj->native_layout_id());

        switch(descriptor.kind)
        {
            case ObjectSizeKind::LegacyHeapLayout:
                return legacy_heap_layout_object_size_in_bytes(obj);
            case ObjectSizeKind::StaticSize:
                return descriptor.static_size_in_bytes;
            case ObjectSizeKind::Custom:
                assert(descriptor.custom_size_in_bytes != nullptr);
                return descriptor.custom_size_in_bytes(obj);
            case ObjectSizeKind::Missing:
            case ObjectSizeKind::DynamicSmiSize:
            case ObjectSizeKind::DynamicAuxSize:
                break;
        }

        assert(false && "object-size descriptor kind not implemented yet");
        return 0;
    }

}  // namespace cl
