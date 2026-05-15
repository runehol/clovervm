#include "native_layout_descriptor.h"

#include <cassert>

namespace cl
{
    namespace
    {
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
            case ReleaseKind::Missing:
            case ReleaseKind::DynamicSmiSpan:
            case ReleaseKind::DynamicAuxSpan:
            case ReleaseKind::Custom:
                break;
        }

        assert(false && "release descriptor kind not implemented yet");
        return NativeValueSpan{nullptr, 0};
    }

}  // namespace cl
