#ifndef CL_HEAP_OBJECT_H
#define CL_HEAP_OBJECT_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace cl
{
    struct ExpandedHeader
    {
        uint64_t object_size_in_16byte_units;
        uint64_t value_count;
    };

    struct DynamicLayoutSpec
    {
        uint64_t object_size_in_16byte_units;
        uint64_t value_count;
    };

    using HeapLayout = uint32_t;

    constexpr uint32_t object_layout_expanded_bit = 1u << 31;
    constexpr uint32_t object_layout_size_shift = 0;
    constexpr uint32_t object_layout_offset_shift = 28;
    constexpr uint32_t object_layout_count_shift = 14;
    constexpr uint32_t object_layout_offset_bits =
        object_layout_expanded_bit >> object_layout_offset_shift;
    constexpr uint32_t object_layout_count_bits =
        object_layout_offset_shift - object_layout_count_shift;
    constexpr uint32_t object_layout_size_bits =
        object_layout_count_shift - object_layout_size_shift;
    constexpr uint32_t object_layout_offset_mask =
        (1u << object_layout_offset_bits) - 1;
    constexpr uint32_t object_layout_count_mask =
        (1u << object_layout_count_bits) - 1;
    constexpr uint32_t object_layout_size_mask =
        (1u << object_layout_size_bits) - 1;

#define CL_OFFSETOF(type, member) offsetof(type, member)

    constexpr uint64_t round_up_to_16byte_units(size_t n_bytes)
    {
        return (n_bytes + 15) / 16;
    }

    constexpr bool compact_layout_fits(uint64_t object_size_in_16byte_units,
                                       uint32_t value_offset_in_words,
                                       uint64_t value_count)
    {
        return object_size_in_16byte_units <= object_layout_size_mask &&
               value_offset_in_words <= object_layout_offset_mask &&
               value_count <= object_layout_count_mask;
    }

    constexpr HeapLayout
    encode_compact_layout_unchecked(uint64_t object_size_in_16byte_units,
                                    uint32_t value_offset_in_words,
                                    uint64_t value_count)
    {
        return (value_offset_in_words << object_layout_offset_shift) |
               (uint32_t(value_count) << object_layout_count_shift) |
               uint32_t(object_size_in_16byte_units);
    }

    constexpr bool expanded_layout_fits(uint32_t value_offset_in_words)
    {
        return value_offset_in_words <= ~object_layout_expanded_bit;
    }

    constexpr HeapLayout
    encode_expanded_layout_unchecked(uint32_t value_offset_in_words)
    {
        return object_layout_expanded_bit | value_offset_in_words;
    }

    constexpr bool layout_is_expanded(HeapLayout layout)
    {
        return (layout & object_layout_expanded_bit) != 0;
    }

    constexpr uint32_t compact_layout_value_count(HeapLayout layout)
    {
        return (layout >> object_layout_count_shift) & object_layout_count_mask;
    }

    constexpr HeapLayout compact_layout_without_value_count(HeapLayout layout)
    {
        return layout &
               ~(object_layout_count_mask << object_layout_count_shift);
    }

#define CL_DECLARE_STATIC_LAYOUT_WITH_VALUES(type, first_value_member,         \
                                             value_count_expr)                 \
    static constexpr bool has_dynamic_layout = false;                          \
    static constexpr uint32_t static_value_offset_in_words()                   \
    {                                                                          \
        static_assert(                                                         \
            CL_OFFSETOF(type, first_value_member) % sizeof(uint64_t) == 0,     \
            "Value region must start on a 64-bit word boundary");              \
        return CL_OFFSETOF(type, first_value_member) / sizeof(uint64_t);       \
    }                                                                          \
    static constexpr uint64_t static_value_count()                             \
    {                                                                          \
        return value_count_expr;                                               \
    }                                                                          \
    static constexpr uint64_t static_size_in_16byte_units()                    \
    {                                                                          \
        return round_up_to_16byte_units(sizeof(type));                         \
    }                                                                          \
    static constexpr HeapLayout compact_layout()                               \
    {                                                                          \
        static_assert(compact_layout_fits(static_size_in_16byte_units(),       \
                                          static_value_offset_in_words(),      \
                                          static_value_count()),               \
                      "Compact object layout does not fit in the compact "     \
                      "header");                                               \
        return encode_compact_layout_unchecked(static_size_in_16byte_units(),  \
                                               static_value_offset_in_words(), \
                                               static_value_count());          \
    }

#define CL_DECLARE_STATIC_LAYOUT_NO_VALUES(type)                               \
    static constexpr bool has_dynamic_layout = false;                          \
    static constexpr uint32_t static_value_offset_in_words() { return 0; }     \
    static constexpr uint64_t static_value_count() { return 0; }               \
    static constexpr uint64_t static_size_in_16byte_units()                    \
    {                                                                          \
        return round_up_to_16byte_units(sizeof(type));                         \
    }                                                                          \
    static constexpr HeapLayout compact_layout()                               \
    {                                                                          \
        static_assert(compact_layout_fits(static_size_in_16byte_units(),       \
                                          static_value_offset_in_words(),      \
                                          static_value_count()),               \
                      "Compact object layout does not fit in the compact "     \
                      "header");                                               \
        return encode_compact_layout_unchecked(static_size_in_16byte_units(),  \
                                               static_value_offset_in_words(), \
                                               static_value_count());          \
    }

#define CL_DECLARE_STATIC_LAYOUT_EXTENDS_WITH_VALUES(type, base_type,          \
                                                     own_value_count_expr)     \
    static_assert(!base_type::has_dynamic_layout,                              \
                  "Static inherited layouts require a static base layout");    \
    static constexpr bool has_dynamic_layout = false;                          \
    static constexpr uint32_t static_value_offset_in_words()                   \
    {                                                                          \
        static_assert(std::is_base_of_v<base_type, type>,                      \
                      "Layout base must be a C++ base class");                 \
        return base_type::static_value_offset_in_words();                      \
    }                                                                          \
    static constexpr uint64_t static_value_count()                             \
    {                                                                          \
        return base_type::static_value_count() + own_value_count_expr;         \
    }                                                                          \
    static constexpr uint64_t static_size_in_16byte_units()                    \
    {                                                                          \
        return round_up_to_16byte_units(sizeof(type));                         \
    }                                                                          \
    static constexpr HeapLayout compact_layout()                               \
    {                                                                          \
        static_assert(compact_layout_fits(static_size_in_16byte_units(),       \
                                          static_value_offset_in_words(),      \
                                          static_value_count()),               \
                      "Compact object layout does not fit in the compact "     \
                      "header");                                               \
        return encode_compact_layout_unchecked(static_size_in_16byte_units(),  \
                                               static_value_offset_in_words(), \
                                               static_value_count());          \
    }

#define CL_DECLARE_DYNAMIC_LAYOUT_WITH_VALUES(type, first_value_member)        \
    static constexpr bool has_dynamic_layout = true;                           \
    static constexpr uint32_t static_value_offset_in_words()                   \
    {                                                                          \
        static_assert(                                                         \
            CL_OFFSETOF(type, first_value_member) % sizeof(uint64_t) == 0,     \
            "Value region must start on a 64-bit word boundary");              \
        return CL_OFFSETOF(type, first_value_member) / sizeof(uint64_t);       \
    }

#define CL_DECLARE_DYNAMIC_LAYOUT_NO_VALUES(type)                              \
    static constexpr bool has_dynamic_layout = true;                           \
    static constexpr uint32_t static_value_offset_in_words() { return 0; }

#define CL_DECLARE_DYNAMIC_LAYOUT_EXTENDS_WITH_VALUES(                         \
    type, base_type, fixed_own_value_count_expr)                               \
    static_assert(!base_type::has_dynamic_layout,                              \
                  "Dynamic inherited layouts require a static base layout");   \
    static constexpr bool has_dynamic_layout = true;                           \
    static constexpr uint32_t static_value_offset_in_words()                   \
    {                                                                          \
        static_assert(std::is_base_of_v<base_type, type>,                      \
                      "Layout base must be a C++ base class");                 \
        return base_type::static_value_offset_in_words();                      \
    }                                                                          \
    static constexpr uint64_t static_fixed_value_count()                       \
    {                                                                          \
        return base_type::static_value_count() + fixed_own_value_count_expr;   \
    }

    /*
      Base class for all VM heap records. HeapObjects have the common header
      needed for refcounting and value scanning, but are not necessarily
      Python-visible objects.
    */
    struct HeapObject
    {
        explicit HeapObject(HeapLayout _layout) : refcount(0), layout(_layout)
        {
        }

        HeapObject() : refcount(0), layout(0) {}

        int32_t refcount;
        HeapLayout layout;
    };

    inline ExpandedHeader *expanded_header_for_object(HeapObject *obj)
    {
        assert(layout_is_expanded(obj->layout));
        return reinterpret_cast<ExpandedHeader *>(
            reinterpret_cast<char *>(obj) - sizeof(ExpandedHeader));
    }

    inline const ExpandedHeader *
    expanded_header_for_object(const HeapObject *obj)
    {
        assert(layout_is_expanded(obj->layout));
        return reinterpret_cast<const ExpandedHeader *>(
            reinterpret_cast<const char *>(obj) - sizeof(ExpandedHeader));
    }

    inline void object_clear_value_ownership(HeapObject *obj)
    {
        if(layout_is_expanded(obj->layout))
        {
            expanded_header_for_object(obj)->value_count = 0;
            return;
        }

        obj->layout = compact_layout_without_value_count(obj->layout);
    }

    static_assert(sizeof(DynamicLayoutSpec) == 16);
    static_assert(sizeof(ExpandedHeader) == 16);
    static_assert(sizeof(HeapObject) == 8);
    static_assert(std::is_trivially_destructible_v<HeapObject>);

}  // namespace cl

#endif  // CL_HEAP_OBJECT_H
