#ifndef CL_NATIVE_LAYOUT_DESCRIPTOR_H
#define CL_NATIVE_LAYOUT_DESCRIPTOR_H

#include "heap_object.h"
#include "native_layout_id.h"
#include "value.h"

#include <array>
#include <cassert>
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

    enum class ObjectSizeKind : uint8_t
    {
        Missing,
        LegacyHeapLayout,
        StaticSize,
        DynamicSmiSize,
        DynamicAuxSize,
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

    struct ObjectSizeDescriptor
    {
        ObjectSizeKind kind;
        uint32_t static_size_in_16byte_units;
        uint16_t count_offset_words;
        uint32_t element_size_in_bytes;
        size_t (*custom_size_in_bytes)(const HeapObject *);

        static constexpr ObjectSizeDescriptor missing()
        {
            return ObjectSizeDescriptor{ObjectSizeKind::Missing, 0, 0, 0,
                                        nullptr};
        }

        static constexpr ObjectSizeDescriptor legacy_heap_layout()
        {
            return ObjectSizeDescriptor{ObjectSizeKind::LegacyHeapLayout, 0, 0,
                                        0, nullptr};
        }
    };

    constexpr size_t native_layout_descriptor_count()
    {
        return static_cast<size_t>(NativeLayoutId::Count);
    }

    namespace native_layout_descriptor_detail
    {
        constexpr size_t native_layout_index(NativeLayoutId native_layout)
        {
            return static_cast<size_t>(native_layout);
        }

        constexpr ReleaseDescriptor legacy_release_descriptor()
        {
            return ReleaseDescriptor::legacy_heap_layout();
        }

        constexpr ObjectSizeDescriptor legacy_object_size_descriptor()
        {
            return ObjectSizeDescriptor::legacy_heap_layout();
        }

        template <typename Descriptor, typename Kind>
        constexpr bool descriptor_table_is_complete(
            const std::array<Descriptor, native_layout_descriptor_count()>
                &descriptors,
            Kind missing_kind)
        {
            for(size_t idx = 0; idx < native_layout_descriptor_count(); ++idx)
            {
                if(descriptors[idx].kind == missing_kind)
                {
                    return false;
                }
            }
            return true;
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

        constexpr std::array<ObjectSizeDescriptor,
                             native_layout_descriptor_count()>
        make_object_size_descriptors()
        {
            std::array<ObjectSizeDescriptor, native_layout_descriptor_count()>
                descriptors{};
            descriptors[native_layout_index(NativeLayoutId::Invalid)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(NativeLayoutId::String)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(NativeLayoutId::List)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(NativeLayoutId::Tuple)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(NativeLayoutId::Dict)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(NativeLayoutId::Function)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(NativeLayoutId::RangeIterator)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(NativeLayoutId::TupleIterator)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(NativeLayoutId::ListIterator)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(NativeLayoutId::CodeObject)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(NativeLayoutId::ClassObject)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(NativeLayoutId::Exception)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(NativeLayoutId::StopIteration)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(NativeLayoutId::Instance)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(NativeLayoutId::Scope)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(NativeLayoutId::Shape)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(NativeLayoutId::ValidityCell)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(NativeLayoutId::OverflowSlots)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(NativeLayoutId::RawArrayBacking)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(
                NativeLayoutId::ValueArrayBacking)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(
                NativeLayoutId::HeapPtrArrayBacking)] =
                legacy_object_size_descriptor();
            descriptors[native_layout_index(NativeLayoutId::TestOnly)] =
                legacy_object_size_descriptor();
            return descriptors;
        }

        inline constexpr std::array release_descriptors =
            make_release_descriptors();
        static_assert(descriptor_table_is_complete(release_descriptors,
                                                   ReleaseKind::Missing),
                      "Every native layout ID must have a release descriptor");

        inline constexpr std::array object_size_descriptors =
            make_object_size_descriptors();
        static_assert(
            descriptor_table_is_complete(object_size_descriptors,
                                         ObjectSizeKind::Missing),
            "Every native layout ID must have an object-size descriptor");
    }  // namespace native_layout_descriptor_detail

    inline const ReleaseDescriptor &
    release_descriptor_for(NativeLayoutId native_layout)
    {
        assert(native_layout != NativeLayoutId::Invalid);
        assert(native_layout_descriptor_detail::native_layout_index(
                   native_layout) < native_layout_descriptor_count());
        return native_layout_descriptor_detail::release_descriptors
            [native_layout_descriptor_detail::native_layout_index(
                native_layout)];
    }

    inline const ObjectSizeDescriptor &
    object_size_descriptor_for(NativeLayoutId native_layout)
    {
        assert(native_layout != NativeLayoutId::Invalid);
        assert(native_layout_descriptor_detail::native_layout_index(
                   native_layout) < native_layout_descriptor_count());
        return native_layout_descriptor_detail::object_size_descriptors
            [native_layout_descriptor_detail::native_layout_index(
                native_layout)];
    }

    NativeValueSpan value_span_for_release(HeapObject *obj);

}  // namespace cl

#endif  // CL_NATIVE_LAYOUT_DESCRIPTOR_H
