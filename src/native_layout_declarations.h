#ifndef CL_NATIVE_LAYOUT_DECLARATIONS_H
#define CL_NATIVE_LAYOUT_DECLARATIONS_H

#include "heap_object.h"

#include <cstddef>
#include <cstdint>

namespace cl
{
    enum class ReleaseKind : uint8_t
    {
        Missing,
        StaticSpan,
        DynamicSmiSpan,
        DynamicAuxSpan,
        CustomDealloc,
    };

    enum class ObjectSizeKind : uint8_t
    {
        Missing,
        StaticSize,
        Custom,
    };

#define CL_DECLARE_EMPTY_VALUE_SPAN(type)                                      \
    static constexpr ReleaseKind native_release_kind =                         \
        ReleaseKind::StaticSpan;                                               \
    static constexpr uint32_t native_value_offset_in_words() { return 0; }     \
    static constexpr uint64_t native_static_release_count() { return 0; }

#define CL_DECLARE_STATIC_VALUE_SPAN(type, first_value_member, count_expr)     \
    static constexpr ReleaseKind native_release_kind =                         \
        ReleaseKind::StaticSpan;                                               \
    static constexpr uint32_t native_value_offset_in_words()                   \
    {                                                                          \
        static_assert(                                                         \
            CL_OFFSETOF(type, first_value_member) % sizeof(uint64_t) == 0,     \
            "Native value span must start on a word boundary");                \
        return CL_OFFSETOF(type, first_value_member) / sizeof(uint64_t);       \
    }                                                                          \
    static constexpr uint64_t native_static_release_count()                    \
    {                                                                          \
        return count_expr;                                                     \
    }

#define CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(type, base_type, own_count_expr)  \
    static_assert(base_type::native_release_kind == ReleaseKind::StaticSpan,   \
                  "Static inherited native value spans require a static "      \
                  "base span");                                                \
    static constexpr ReleaseKind native_release_kind =                         \
        ReleaseKind::StaticSpan;                                               \
    static constexpr uint32_t native_value_offset_in_words()                   \
    {                                                                          \
        return base_type::native_value_offset_in_words();                      \
    }                                                                          \
    static constexpr uint64_t native_static_release_count()                    \
    {                                                                          \
        return base_type::native_static_release_count() + own_count_expr;      \
    }

#define CL_DECLARE_DYNAMIC_SMI_VALUE_SPAN(                                     \
    type, count_member, first_value_member, additional_release_count_expr)     \
    static constexpr ReleaseKind native_release_kind =                         \
        ReleaseKind::DynamicSmiSpan;                                           \
    static constexpr uint32_t native_value_count_offset_in_words()             \
    {                                                                          \
        static_assert(CL_OFFSETOF(type, count_member) % sizeof(uint64_t) == 0, \
                      "Native dynamic value count must be word aligned");      \
        return CL_OFFSETOF(type, count_member) / sizeof(uint64_t);             \
    }                                                                          \
    static constexpr uint32_t native_value_offset_in_words()                   \
    {                                                                          \
        static_assert(                                                         \
            CL_OFFSETOF(type, first_value_member) % sizeof(uint64_t) == 0,     \
            "Native value span must start on a word boundary");                \
        return CL_OFFSETOF(type, first_value_member) / sizeof(uint64_t);       \
    }                                                                          \
    static constexpr uint64_t native_additional_release_count()                \
    {                                                                          \
        return additional_release_count_expr;                                  \
    }

#define CL_DECLARE_DYNAMIC_SMI_VALUE_SPAN_EXTENDS(                             \
    type, base_type, count_member, own_additional_release_count_expr)          \
    static_assert(base_type::native_release_kind == ReleaseKind::StaticSpan,   \
                  "Dynamic inherited native value spans require a static "     \
                  "base span");                                                \
    static constexpr ReleaseKind native_release_kind =                         \
        ReleaseKind::DynamicSmiSpan;                                           \
    static constexpr uint32_t native_value_count_offset_in_words()             \
    {                                                                          \
        static_assert(CL_OFFSETOF(type, count_member) % sizeof(uint64_t) == 0, \
                      "Native dynamic value count must be word aligned");      \
        return CL_OFFSETOF(type, count_member) / sizeof(uint64_t);             \
    }                                                                          \
    static constexpr uint32_t native_value_offset_in_words()                   \
    {                                                                          \
        return base_type::native_value_offset_in_words();                      \
    }                                                                          \
    static constexpr uint64_t native_additional_release_count()                \
    {                                                                          \
        return base_type::native_static_release_count() +                      \
               own_additional_release_count_expr;                              \
    }

#define CL_DECLARE_DYNAMIC_AUX_VALUE_SPAN(type, first_value_member,            \
                                          additional_release_count_expr)       \
    static constexpr ReleaseKind native_release_kind =                         \
        ReleaseKind::DynamicAuxSpan;                                           \
    static constexpr uint32_t native_value_offset_in_words()                   \
    {                                                                          \
        static_assert(                                                         \
            CL_OFFSETOF(type, first_value_member) % sizeof(uint64_t) == 0,     \
            "Native value span must start on a word boundary");                \
        return CL_OFFSETOF(type, first_value_member) / sizeof(uint64_t);       \
    }                                                                          \
    static constexpr uint64_t native_additional_release_count()                \
    {                                                                          \
        return additional_release_count_expr;                                  \
    }

#define CL_DECLARE_DYNAMIC_AUX_VALUE_SPAN_EXTENDS(                             \
    type, base_type, own_additional_release_count_expr)                        \
    static_assert(base_type::native_release_kind == ReleaseKind::StaticSpan,   \
                  "Dynamic inherited native value spans require a static "     \
                  "base span");                                                \
    static constexpr ReleaseKind native_release_kind =                         \
        ReleaseKind::DynamicAuxSpan;                                           \
    static constexpr uint32_t native_value_offset_in_words()                   \
    {                                                                          \
        return base_type::native_value_offset_in_words();                      \
    }                                                                          \
    static constexpr uint64_t native_additional_release_count()                \
    {                                                                          \
        return base_type::native_static_release_count() +                      \
               own_additional_release_count_expr;                              \
    }

#define CL_DECLARE_CUSTOM_DEALLOC(type, function)                              \
    static constexpr ReleaseKind native_release_kind =                         \
        ReleaseKind::CustomDealloc;                                            \
    static constexpr auto native_dealloc = function;

#define CL_DECLARE_STATIC_OBJECT_SIZE(type)                                    \
    static constexpr ObjectSizeKind native_object_size_kind =                  \
        ObjectSizeKind::StaticSize;                                            \
    static constexpr size_t native_static_object_size_in_bytes()               \
    {                                                                          \
        return sizeof(type);                                                   \
    }

#define CL_DECLARE_CUSTOM_OBJECT_SIZE(type, function)                          \
    static constexpr ObjectSizeKind native_object_size_kind =                  \
        ObjectSizeKind::Custom;                                                \
    static size_t native_object_size_in_bytes(const HeapObject *obj)           \
    {                                                                          \
        return function(static_cast<const type *>(obj));                       \
    }

}  // namespace cl

#endif  // CL_NATIVE_LAYOUT_DECLARATIONS_H
