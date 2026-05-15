#include "native_layout_descriptor.h"

#include <array>
#include <cassert>

namespace cl
{
    namespace
    {
        constexpr size_t native_layout_index(NativeLayoutId native_layout)
        {
            return static_cast<size_t>(native_layout);
        }

        constexpr ReleaseDescriptor legacy_release_descriptor()
        {
            return ReleaseDescriptor::legacy_heap_layout();
        }

        constexpr std::array<ReleaseDescriptor,
                             native_layout_descriptor_count()>
        make_release_descriptors()
        {
            std::array<ReleaseDescriptor, native_layout_descriptor_count()>
                descriptors{};
            descriptors[native_layout_index(NativeLayoutId::Invalid)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(NativeLayoutId::String)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(NativeLayoutId::List)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(NativeLayoutId::Tuple)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(NativeLayoutId::Dict)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(NativeLayoutId::Function)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(NativeLayoutId::RangeIterator)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(NativeLayoutId::TupleIterator)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(NativeLayoutId::ListIterator)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(NativeLayoutId::CodeObject)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(NativeLayoutId::ClassObject)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(NativeLayoutId::Exception)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(NativeLayoutId::StopIteration)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(NativeLayoutId::Instance)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(NativeLayoutId::Scope)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(NativeLayoutId::Shape)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(NativeLayoutId::ValidityCell)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(NativeLayoutId::OverflowSlots)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(NativeLayoutId::RawArrayBacking)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(
                NativeLayoutId::ValueArrayBacking)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(
                NativeLayoutId::HeapPtrArrayBacking)] =
                legacy_release_descriptor();
            descriptors[native_layout_index(NativeLayoutId::TestOnly)] =
                legacy_release_descriptor();
            return descriptors;
        }

        constexpr bool release_descriptor_table_is_complete(
            const std::array<ReleaseDescriptor,
                             native_layout_descriptor_count()> &descriptors)
        {
            for(size_t idx = 0; idx < native_layout_descriptor_count(); ++idx)
            {
                if(descriptors[idx].kind == ReleaseKind::Missing)
                {
                    return false;
                }
            }
            return true;
        }

        constexpr std::array release_descriptors = make_release_descriptors();
        static_assert(release_descriptor_table_is_complete(release_descriptors),
                      "Every native layout ID must have a release descriptor");

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
    }  // namespace

    const ReleaseDescriptor &
    release_descriptor_for(NativeLayoutId native_layout)
    {
        assert(native_layout != NativeLayoutId::Invalid);
        assert(native_layout_index(native_layout) <
               native_layout_descriptor_count());
        return release_descriptors[native_layout_index(native_layout)];
    }

    NativeValueSpan value_span_for_release(HeapObject *obj)
    {
        assert(obj != nullptr);
        const ReleaseDescriptor &descriptor =
            release_descriptor_for(obj->native_layout_id());

        switch(descriptor.kind)
        {
            case ReleaseKind::LegacyHeapLayout:
                return legacy_heap_layout_value_span(obj);
            case ReleaseKind::Missing:
            case ReleaseKind::StaticSpan:
            case ReleaseKind::DynamicSmiSpan:
            case ReleaseKind::DynamicAuxSpan:
            case ReleaseKind::Custom:
                break;
        }

        assert(false && "release descriptor kind not implemented yet");
        return NativeValueSpan{nullptr, 0};
    }

}  // namespace cl
