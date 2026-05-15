#ifndef CL_NATIVE_LAYOUT_DESCRIPTOR_H
#define CL_NATIVE_LAYOUT_DESCRIPTOR_H

#include "heap_object.h"
#include "native_layout_id.h"
#include "value.h"

#include <cstddef>
#include <cstdint>

namespace cl
{
    enum class ReleaseKind : uint8_t
    {
        Missing,
        LegacyHeapLayout,
        StaticSpan,
        DynamicSmiSpan,
        DynamicAuxSpan,
        Custom,
    };

    struct NativeValueSpan
    {
        Value *slots;
        uint64_t count;
    };

    struct ReleaseDescriptor
    {
        ReleaseKind kind;
        uint16_t value_offset_words;
        uint32_t static_release_count;
        uint16_t count_offset_words;
        uint32_t additional_value_count;
        void (*custom_release)(HeapObject *);

        static constexpr ReleaseDescriptor missing()
        {
            return ReleaseDescriptor{ReleaseKind::Missing, 0, 0, 0, 0, nullptr};
        }

        static constexpr ReleaseDescriptor legacy_heap_layout()
        {
            return ReleaseDescriptor{
                ReleaseKind::LegacyHeapLayout, 0, 0, 0, 0, nullptr};
        }
    };

    constexpr size_t native_layout_descriptor_count()
    {
        return static_cast<size_t>(NativeLayoutId::Count);
    }

    const ReleaseDescriptor &
    release_descriptor_for(NativeLayoutId native_layout);

    NativeValueSpan value_span_for_release(HeapObject *obj);

}  // namespace cl

#endif  // CL_NATIVE_LAYOUT_DESCRIPTOR_H
