#ifndef CL_NATIVE_LAYOUT_DESCRIPTOR_H
#define CL_NATIVE_LAYOUT_DESCRIPTOR_H

#include "memory/heap_reference_slot.h"
#include "memory/native_layout_declarations.h"
#include "memory/native_layout_id.h"
#include "memory/native_layout_registry.h"
#include "object_model/heap_object.h"
#include "object_model/value.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace cl
{
    struct ReleaseDescriptor
    {
        ReleaseKind kind;
        uint16_t count_offset_words;
        uint32_t static_release_count;
        uint64_t value_offset_words;
        uint32_t additional_release_count;
        void (*custom_release)(HeapObject *);
        // Runs after all managed ownership has been released. Independent of
        // whether the references use an ordinary span or custom release.
        void (*destroy)(HeapObject *);
        // Supplemental strong slots, independent of release and destruction.
        void (*custom_trace_strong)(HeapObject *, StrongSlotVisitor);
        void (*visit_weak_units)(HeapObject *, WeakUnitVisitor);

        static constexpr ReleaseDescriptor
        static_span(uint64_t value_offset_words, uint32_t release_count)
        {
            return ReleaseDescriptor{ReleaseKind::StaticSpan,
                                     0,
                                     release_count,
                                     value_offset_words,
                                     0,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr};
        }

        static constexpr ReleaseDescriptor
        dynamic_smi_span(uint16_t count_offset_words,
                         uint64_t value_offset_words,
                         uint32_t additional_release_count)
        {
            return ReleaseDescriptor{ReleaseKind::DynamicSmiSpan,
                                     count_offset_words,
                                     0,
                                     value_offset_words,
                                     additional_release_count,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr};
        }

        static constexpr ReleaseDescriptor
        dynamic_aux_span(uint64_t value_offset_words,
                         uint32_t additional_release_count)
        {
            return ReleaseDescriptor{ReleaseKind::DynamicAuxSpan,
                                     0,
                                     0,
                                     value_offset_words,
                                     additional_release_count,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr};
        }

        static constexpr ReleaseDescriptor
        custom(void (*custom_release)(HeapObject *))
        {
            return ReleaseDescriptor{ReleaseKind::CustomRelease,
                                     0,
                                     0,
                                     0,
                                     0,
                                     custom_release,
                                     nullptr,
                                     nullptr,
                                     nullptr};
        }
    };

    static_assert(sizeof(ReleaseDescriptor) == 56);

    struct OrdinaryReferenceSpan
    {
        uint64_t value_offset_words;
        uint64_t value_count;
    };

    // The supplied object and span storage must remain valid throughout the
    // visit. Every initialized cell is visited, including immediates and nulls.
    template <typename Visitor>
    void visit_ordinary_reference_slots(HeapObject *object,
                                        OrdinaryReferenceSpan span,
                                        Visitor &&visitor)
    {
        assert(object != nullptr);
        std::byte *address = reinterpret_cast<std::byte *>(object) +
                             span.value_offset_words * sizeof(uint64_t);
        for(uint64_t idx = 0; idx < span.value_count; ++idx)
        {
            std::invoke(visitor, HeapReferenceSlot(address));
            address += sizeof(uint64_t);
        }
    }

    // Resolves one contiguous in-object span without reading its reference
    // cells. Custom handling is unsupported, distinct from a supported empty
    // span.
    inline std::optional<OrdinaryReferenceSpan>
    resolve_ordinary_reference_span(const HeapObject *object,
                                    const ReleaseDescriptor &descriptor)
    {
        assert(object != nullptr);
        switch(descriptor.kind)
        {
            case ReleaseKind::StaticSpan:
                return OrdinaryReferenceSpan{descriptor.value_offset_words,
                                             descriptor.static_release_count};
            case ReleaseKind::DynamicSmiSpan:
                {
                    Value count_value =
                        *(reinterpret_cast<const Value *>(object) +
                          descriptor.count_offset_words);
                    assert(count_value.is_smi());
                    int64_t dynamic_count = count_value.get_smi();
                    assert(dynamic_count >= 0);
                    return OrdinaryReferenceSpan{
                        descriptor.value_offset_words,
                        static_cast<uint64_t>(dynamic_count) +
                            descriptor.additional_release_count};
                }
            case ReleaseKind::DynamicAuxSpan:
                return OrdinaryReferenceSpan{
                    descriptor.value_offset_words,
                    static_cast<uint64_t>(
                        object->native_layout_aux_count_value()) +
                        descriptor.additional_release_count};
            case ReleaseKind::CustomRelease:
                return std::nullopt;
        }
        assert(false && "invalid release descriptor kind");
        __builtin_unreachable();
    }

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

    struct NativeLayoutDescriptor
    {
        ReleaseDescriptor release;
        ObjectSizeDescriptor object_size;
        // Null means preserve the complete object extent as opaque bytes.
        size_t (*custom_copy_size_in_bytes)(const HeapObject *);
    };

    static_assert(sizeof(NativeLayoutDescriptor) ==
                  sizeof(ReleaseDescriptor) + sizeof(ObjectSizeDescriptor) +
                      sizeof(size_t (*)(const HeapObject *)));

    struct NativeLayoutInfo
    {
        size_t allocated_size;
        size_t copy_size;
        // Absent for custom-only scanning, distinct from a supported empty
        // span. Supplemental custom scanners remain independent of this span.
        std::optional<OrdinaryReferenceSpan> strong_span;
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
                ReleaseDescriptor descriptor = build_release();
                if constexpr(requires { T::native_destroy; })
                {
                    descriptor.destroy = T::native_destroy;
                }
                if constexpr(requires { T::native_custom_trace_strong; })
                {
                    descriptor.custom_trace_strong =
                        T::native_custom_trace_strong;
                }
                if constexpr(requires { T::native_visit_weak_units; })
                {
                    descriptor.visit_weak_units = T::native_visit_weak_units;
                }
                return descriptor;
            }

        private:
            static constexpr ReleaseDescriptor build_release()
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
                    return ReleaseDescriptor::custom(T::native_custom_release);
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

        template <typename T>
        constexpr auto native_copy_size_function()
            -> size_t (*)(const HeapObject *)
        {
            if constexpr(requires { T::native_copy_size_in_bytes; })
            {
                return T::native_copy_size_in_bytes;
            }
            else
            {
                return nullptr;
            }
        }

#define CL_DECLARE_NATIVE_LAYOUT(type)                                         \
    template <> struct NativeLayoutTraits<type::native_layout>                 \
    {                                                                          \
        using object_type = type;                                              \
        static constexpr NativeLayoutId native_layout = type::native_layout;   \
        static constexpr const char *cpp_name = #type;                         \
        static constexpr NativeLayoutDescriptor descriptor = {                 \
            NativeLayoutReleaseDescriptorBuilder<type>::build(),               \
            NativeLayoutObjectSizeDescriptorBuilder<type>::build(),            \
            native_copy_size_function<type>()};                                \
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
                case ReleaseKind::CustomRelease:
                    return descriptor.custom_release != nullptr;
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

        constexpr bool native_layout_descriptor_is_valid(
            const NativeLayoutDescriptor &descriptor)
        {
            return release_descriptor_is_valid(descriptor.release) &&
                   object_size_descriptor_is_valid(descriptor.object_size) &&
                   (descriptor.release.kind != ReleaseKind::CustomRelease ||
                    descriptor.release.custom_trace_strong != nullptr);
        }

        constexpr bool native_layout_descriptor_table_is_complete(
            const std::array<NativeLayoutDescriptor,
                             native_layout_descriptor_count()> &descriptors)
        {
            for(size_t idx = native_layout_index(NativeLayoutId::Invalid) + 1;
                idx < native_layout_descriptor_count(); ++idx)
            {
                if(idx == native_layout_index(NativeLayoutId::TestOnly))
                {
                    continue;
                }
                if(!native_layout_descriptor_is_valid(descriptors[idx]))
                {
                    return false;
                }
            }
            return true;
        }

        constexpr std::array<NativeLayoutDescriptor,
                             native_layout_descriptor_count()>
        make_native_layout_descriptors()
        {
            std::array<NativeLayoutDescriptor, native_layout_descriptor_count()>
                descriptors{};

#define CL_SET_NATIVE_LAYOUT_DESCRIPTOR(type)                                  \
    descriptors[native_layout_index(type::native_layout)] =                    \
        NativeLayoutTraits<type::native_layout>::descriptor
            CL_NATIVE_LAYOUT_REGISTRY(CL_SET_NATIVE_LAYOUT_DESCRIPTOR);
#undef CL_SET_NATIVE_LAYOUT_DESCRIPTOR

            return descriptors;
        }

        inline constexpr std::array native_layout_descriptors =
            make_native_layout_descriptors();
        static_assert(
            native_layout_descriptor_table_is_complete(
                native_layout_descriptors),
            "Every native layout ID must support release, strong slot "
            "enumeration and object-size queries");
    }  // namespace native_layout_descriptor_detail

    inline const NativeLayoutDescriptor &
    native_layout_descriptor_for(NativeLayoutId native_layout)
    {
        assert(native_layout != NativeLayoutId::Invalid);
        assert(native_layout_descriptor_detail::native_layout_index(
                   native_layout) < native_layout_descriptor_count());
        const NativeLayoutDescriptor &descriptor =
            native_layout_descriptor_detail::native_layout_descriptors
                [native_layout_descriptor_detail::native_layout_index(
                    native_layout)];
        assert(
            native_layout_descriptor_detail::native_layout_descriptor_is_valid(
                descriptor));
        return descriptor;
    }

    size_t object_size_in_bytes(const HeapObject *obj);

    // Queries live layout state without reading payload cells. Copy size is a
    // byte-preservation bound, not movement admission or a constructed-element
    // count. Allocation size excludes allocator rounding.
    NativeLayoutInfo native_layout_info(const HeapObject *obj);

    // Enumerates this object's own strong cells, not its transitive closure.
    // Ordinary spans and supplemental custom scanning can coexist. Weak cells
    // are excluded; no reference counts or reclamation state are changed.
    void visit_strong_reference_slots(HeapObject *object,
                                      StrongSlotVisitor visitor);

    // Enumerates exact repair units, not entire owners for late rescanning.
    // Callbacks may repair early or after strong closure. A callback resetting
    // an entry can release its owned native plan's references, but must not run
    // reclamation, Python code, or a safepoint. With RC enabled, install an
    // active thread for any resulting ZCT enqueue.
    void visit_weak_reference_units(HeapObject *object,
                                    WeakUnitVisitor visitor);

}  // namespace cl

#endif  // CL_NATIVE_LAYOUT_DESCRIPTOR_H
