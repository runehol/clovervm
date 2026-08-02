#ifndef CL_JIT_TAGGED_VALUE_FACTS_H
#define CL_JIT_TAGGED_VALUE_FACTS_H

#include "object_model/shape_key.h"
#include "object_model/value.h"

#include <cassert>
#include <cstdint>

namespace cl::jit
{
    enum class TaggedValueClassKind : uint8_t
    {
        MaskedEqual,
        MaskedNonZero,
    };

    class TaggedValueClass
    {
    public:
        static constexpr TaggedValueClass masked_equal(uint8_t mask,
                                                       uint8_t expected)
        {
            assert((uint64_t(mask) & ~value_tag_mask) == 0);
            assert((expected & ~mask) == 0);
            return TaggedValueClass(
                uint32_t(mask) | (uint32_t(expected) << 8) |
                (uint32_t(TaggedValueClassKind::MaskedEqual) << 16));
        }

        static constexpr TaggedValueClass masked_nonzero(uint8_t mask)
        {
            assert((uint64_t(mask) & ~value_tag_mask) == 0);
            return TaggedValueClass(
                uint32_t(mask) |
                (uint32_t(TaggedValueClassKind::MaskedNonZero) << 16));
        }

        static constexpr TaggedValueClass smi()
        {
            return masked_equal(uint8_t(value_tag_mask), 0);
        }

        static constexpr TaggedValueClass boolean()
        {
            return masked_equal(uint8_t(value_tag_mask),
                                uint8_t(value_boolean_tag));
        }

        static constexpr TaggedValueClass smi_or_boolean()
        {
            return masked_equal(uint8_t(value_not_smi_or_boolean_mask), 0);
        }

        static constexpr TaggedValueClass any_inline()
        {
            return masked_equal(uint8_t(value_ptr_mask), 0);
        }

        static constexpr TaggedValueClass pointer()
        {
            return masked_nonzero(uint8_t(value_ptr_mask));
        }

        constexpr TaggedValueClassKind kind() const
        {
            return TaggedValueClassKind((encoded_ >> 16) & 0xff);
        }
        constexpr uint8_t mask() const { return uint8_t(encoded_); }
        constexpr uint8_t expected() const { return uint8_t(encoded_ >> 8); }
        constexpr uint32_t encoded() const { return encoded_; }

        friend constexpr bool operator==(TaggedValueClass,
                                         TaggedValueClass) = default;

    private:
        explicit constexpr TaggedValueClass(uint32_t encoded)
            : encoded_(encoded)
        {
        }

        uint32_t encoded_;
    };

    constexpr uint32_t tagged_value_set_bit(uint64_t encoded_value)
    {
        return uint32_t{1} << uint32_t(encoded_value & value_tag_mask);
    }

    constexpr uint32_t valid_tagged_value_set_bits =
        tagged_value_set_bit(0) | tagged_value_set_bit(value_none) |
        tagged_value_set_bit(value_not_present) |
        tagged_value_set_bit(value_exception) |
        tagged_value_set_bit(value_boolean_tag) |
        tagged_value_set_bit(value_not_implemented) |
        tagged_value_set_bit(value_ellipsis) |
        tagged_value_set_bit(value_interned_ptr_tag) |
        tagged_value_set_bit(value_refcounted_ptr_tag);

    class TaggedValueSet
    {
    public:
        static constexpr TaggedValueSet never() { return TaggedValueSet(0); }
        static constexpr TaggedValueSet unknown()
        {
            return TaggedValueSet(valid_tagged_value_set_bits);
        }

        static constexpr TaggedValueSet pointer()
        {
            return TaggedValueSet(
                tagged_value_set_bit(value_interned_ptr_tag) |
                tagged_value_set_bit(value_refcounted_ptr_tag));
        }

        static constexpr TaggedValueSet from_inline_tag(uint8_t tag)
        {
            assert(tag <= value_tag_mask);
            uint32_t bit = uint32_t{1} << tag;
            assert((valid_tagged_value_set_bits & bit) != 0);
            return TaggedValueSet(bit);
        }

        static constexpr TaggedValueSet from_shape_key(ShapeKey key)
        {
            assert(key.is_valid());
            if(key.is_inline())
            {
                return from_inline_tag(uint8_t(key.inline_tag()));
            }
            return pointer();
        }

        static constexpr TaggedValueSet smi() { return from_inline_tag(0); }

        static constexpr TaggedValueSet boolean()
        {
            return from_inline_tag(uint8_t(value_boolean_tag));
        }

        static constexpr TaggedValueSet smi_or_boolean()
        {
            return smi().merge(boolean());
        }

        static constexpr TaggedValueSet from_class(TaggedValueClass value_class)
        {
            if(value_class.kind() == TaggedValueClassKind::MaskedEqual &&
               value_class.mask() == value_tag_mask)
            {
                uint32_t bit = uint32_t{1} << value_class.expected();
                return TaggedValueSet(bit & valid_tagged_value_set_bits);
            }

            uint32_t accepted = 0;
            for(uint32_t tag = 0; tag <= value_tag_mask; ++tag)
            {
                uint32_t bit = uint32_t{1} << tag;
                if((valid_tagged_value_set_bits & bit) == 0)
                {
                    continue;
                }

                bool matches = false;
                switch(value_class.kind())
                {
                    case TaggedValueClassKind::MaskedEqual:
                        matches = (tag & value_class.mask()) ==
                                  value_class.expected();
                        break;
                    case TaggedValueClassKind::MaskedNonZero:
                        matches = (tag & value_class.mask()) != 0;
                        break;
                }
                if(matches)
                {
                    accepted |= bit;
                }
            }
            return TaggedValueSet(accepted);
        }

        constexpr bool is_never() const { return bits_ == 0; }
        constexpr bool is_subset_of(TaggedValueSet other) const
        {
            return (bits_ & ~other.bits_) == 0;
        }
        constexpr bool is_disjoint_from(TaggedValueSet other) const
        {
            return (bits_ & other.bits_) == 0;
        }

        constexpr TaggedValueSet intersect(TaggedValueSet other) const
        {
            return TaggedValueSet(bits_ & other.bits_);
        }
        constexpr TaggedValueSet merge(TaggedValueSet other) const
        {
            return TaggedValueSet(bits_ | other.bits_);
        }

        constexpr uint32_t bits() const { return bits_; }

        friend constexpr bool operator==(TaggedValueSet,
                                         TaggedValueSet) = default;

    private:
        explicit constexpr TaggedValueSet(uint32_t bits) : bits_(bits)
        {
            assert((bits & ~valid_tagged_value_set_bits) == 0);
        }

        uint32_t bits_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_TAGGED_VALUE_FACTS_H
