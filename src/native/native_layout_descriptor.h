#ifndef CL_NATIVE_LAYOUT_DESCRIPTOR_H
#define CL_NATIVE_LAYOUT_DESCRIPTOR_H

#include "native/native_layout_declarations.h"
#include "native/native_layout_id.h"
#include "native/native_layout_registry.h"
#include "object_model/heap_object.h"
#include "object_model/value.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace cl
{
    struct ReleaseDescriptor
    {
        ReleaseKind kind;
        uint16_t value_offset_words;
        uint32_t static_release_count;
        uint16_t count_offset_words;
        uint32_t additional_release_count;
        void (*custom_dealloc)(HeapObject *);

        static constexpr ReleaseDescriptor
        static_span(uint16_t value_offset_words, uint32_t release_count)
        {
            return ReleaseDescriptor{ReleaseKind::StaticSpan,
                                     value_offset_words,
                                     release_count,
                                     0,
                                     0,
                                     nullptr};
        }

        static constexpr ReleaseDescriptor
        dynamic_smi_span(uint16_t count_offset_words,
                         uint16_t value_offset_words,
                         uint32_t additional_release_count)
        {
            return ReleaseDescriptor{
                ReleaseKind::DynamicSmiSpan, value_offset_words,       0,
                count_offset_words,          additional_release_count, nullptr};
        }

        static constexpr ReleaseDescriptor
        dynamic_aux_span(uint16_t value_offset_words,
                         uint32_t additional_release_count)
        {
            return ReleaseDescriptor{ReleaseKind::DynamicAuxSpan,
                                     value_offset_words,
                                     0,
                                     0,
                                     additional_release_count,
                                     nullptr};
        }

        static constexpr ReleaseDescriptor
        custom(void (*custom_dealloc)(HeapObject *))
        {
            return ReleaseDescriptor{
                ReleaseKind::CustomDealloc, 0, 0, 0, 0, custom_dealloc};
        }
    };

    struct ObjectSizeDescriptor
    {
        ObjectSizeKind kind;
        size_t static_size_in_bytes;
        size_t (*custom_size_in_bytes)(const HeapObject *);

        static constexpr ObjectSizeDescriptor static_size(size_t size_in_bytes)
        {
            return ObjectSizeDescriptor{ObjectSizeKind::StaticSize,
                                        size_in_bytes, nullptr};
        }

        static constexpr ObjectSizeDescriptor
        custom(size_t (*custom_size_in_bytes)(const HeapObject *))
        {
            return ObjectSizeDescriptor{ObjectSizeKind::Custom, 0,
                                        custom_size_in_bytes};
        }
    };

    constexpr size_t native_layout_descriptor_count()
    {
        return static_cast<size_t>(NativeLayoutId::Count);
    }

    namespace native_layout_descriptor_detail
    {
        template <NativeLayoutId native_layout> struct NativeLayoutTraits;

        template <typename T> struct NativeLayoutReleaseDescriptorBuilder
        {
            static constexpr ReleaseDescriptor build()
            {
                if constexpr(T::native_release_kind == ReleaseKind::StaticSpan)
                {
                    return ReleaseDescriptor::static_span(
                        T::native_value_offset_in_words(),
                        T::native_static_release_count());
                }
                else if constexpr(T::native_release_kind ==
                                  ReleaseKind::DynamicSmiSpan)
                {
                    return ReleaseDescriptor::dynamic_smi_span(
                        T::native_value_count_offset_in_words(),
                        T::native_value_offset_in_words(),
                        T::native_additional_release_count());
                }
                else if constexpr(T::native_release_kind ==
                                  ReleaseKind::DynamicAuxSpan)
                {
                    return ReleaseDescriptor::dynamic_aux_span(
                        T::native_value_offset_in_words(),
                        T::native_additional_release_count());
                }
                else
                {
                    return ReleaseDescriptor::custom(T::native_dealloc);
                }
            }
        };

        template <typename T> struct NativeLayoutObjectSizeDescriptorBuilder
        {
            static constexpr ObjectSizeDescriptor build()
            {
                if constexpr(T::native_object_size_kind ==
                             ObjectSizeKind::StaticSize)
                {
                    return ObjectSizeDescriptor::static_size(
                        T::native_static_object_size_in_bytes());
                }
                else
                {
                    return ObjectSizeDescriptor::custom(
                        T::native_object_size_in_bytes);
                }
            }
        };

#define CL_DECLARE_NATIVE_LAYOUT(type)                                         \
    template <> struct NativeLayoutTraits<type::native_layout>                 \
    {                                                                          \
        using object_type = type;                                              \
        static constexpr NativeLayoutId native_layout = type::native_layout;   \
        static constexpr const char *cpp_name = #type;                         \
        static constexpr ReleaseDescriptor release =                           \
            NativeLayoutReleaseDescriptorBuilder<type>::build();               \
        static constexpr ObjectSizeDescriptor object_size =                    \
            NativeLayoutObjectSizeDescriptorBuilder<type>::build();            \
    }

        CL_NATIVE_LAYOUT_REGISTRY(CL_DECLARE_NATIVE_LAYOUT);

        constexpr size_t native_layout_index(NativeLayoutId native_layout)
        {
            return static_cast<size_t>(native_layout);
        }

        constexpr bool
        release_descriptor_is_valid(const ReleaseDescriptor &descriptor)
        {
            switch(descriptor.kind)
            {
                case ReleaseKind::CustomDealloc:
                    return descriptor.custom_dealloc != nullptr;
                case ReleaseKind::StaticSpan:
                case ReleaseKind::DynamicSmiSpan:
                case ReleaseKind::DynamicAuxSpan:
                    return true;
            }
            return false;
        }

        constexpr bool
        object_size_descriptor_is_valid(const ObjectSizeDescriptor &descriptor)
        {
            switch(descriptor.kind)
            {
                case ObjectSizeKind::Custom:
                    return descriptor.custom_size_in_bytes != nullptr;
                case ObjectSizeKind::StaticSize:
                    return true;
            }
            return false;
        }

        constexpr bool release_descriptor_table_is_complete(
            const std::array<ReleaseDescriptor,
                             native_layout_descriptor_count()> &descriptors)
        {
            for(size_t idx = native_layout_index(NativeLayoutId::Invalid) + 1;
                idx < native_layout_descriptor_count(); ++idx)
            {
                if(idx == native_layout_index(NativeLayoutId::TestOnly))
                {
                    continue;
                }
                if(!release_descriptor_is_valid(descriptors[idx]))
                {
                    return false;
                }
            }
            return true;
        }

        constexpr bool object_size_descriptor_table_is_complete(
            const std::array<ObjectSizeDescriptor,
                             native_layout_descriptor_count()> &descriptors)
        {
            for(size_t idx = native_layout_index(NativeLayoutId::Invalid) + 1;
                idx < native_layout_descriptor_count(); ++idx)
            {
                if(idx == native_layout_index(NativeLayoutId::TestOnly))
                {
                    continue;
                }
                if(!object_size_descriptor_is_valid(descriptors[idx]))
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

#define CL_SET_NATIVE_RELEASE_DESCRIPTOR(type)                                 \
    descriptors[native_layout_index(type::native_layout)] =                    \
        NativeLayoutTraits<type::native_layout>::release
            CL_NATIVE_LAYOUT_REGISTRY(CL_SET_NATIVE_RELEASE_DESCRIPTOR);
#undef CL_SET_NATIVE_RELEASE_DESCRIPTOR

            return descriptors;
        }

        constexpr std::array<ObjectSizeDescriptor,
                             native_layout_descriptor_count()>
        make_object_size_descriptors()
        {
            std::array<ObjectSizeDescriptor, native_layout_descriptor_count()>
                descriptors{};

#define CL_SET_NATIVE_OBJECT_SIZE_DESCRIPTOR(type)                             \
    descriptors[native_layout_index(type::native_layout)] =                    \
        NativeLayoutTraits<type::native_layout>::object_size
            CL_NATIVE_LAYOUT_REGISTRY(CL_SET_NATIVE_OBJECT_SIZE_DESCRIPTOR);
#undef CL_SET_NATIVE_OBJECT_SIZE_DESCRIPTOR

            return descriptors;
        }

        inline constexpr std::array release_descriptors =
            make_release_descriptors();
        static_assert(release_descriptor_table_is_complete(release_descriptors),
                      "Every native layout ID must have a release descriptor");

        inline constexpr std::array object_size_descriptors =
            make_object_size_descriptors();
        static_assert(
            object_size_descriptor_table_is_complete(object_size_descriptors),
            "Every native layout ID must have an object-size descriptor");
    }  // namespace native_layout_descriptor_detail

    inline const ReleaseDescriptor &
    release_descriptor_for(NativeLayoutId native_layout)
    {
        assert(native_layout != NativeLayoutId::Invalid);
        assert(native_layout_descriptor_detail::native_layout_index(
                   native_layout) < native_layout_descriptor_count());
        const ReleaseDescriptor &descriptor =
            native_layout_descriptor_detail::release_descriptors
                [native_layout_descriptor_detail::native_layout_index(
                    native_layout)];
        assert(native_layout_descriptor_detail::release_descriptor_is_valid(
            descriptor));
        return descriptor;
    }

    inline const ObjectSizeDescriptor &
    object_size_descriptor_for(NativeLayoutId native_layout)
    {
        assert(native_layout != NativeLayoutId::Invalid);
        assert(native_layout_descriptor_detail::native_layout_index(
                   native_layout) < native_layout_descriptor_count());
        const ObjectSizeDescriptor &descriptor =
            native_layout_descriptor_detail::object_size_descriptors
                [native_layout_descriptor_detail::native_layout_index(
                    native_layout)];
        assert(native_layout_descriptor_detail::object_size_descriptor_is_valid(
            descriptor));
        return descriptor;
    }

    size_t object_size_in_bytes(const HeapObject *obj);

}  // namespace cl

#endif  // CL_NATIVE_LAYOUT_DESCRIPTOR_H
