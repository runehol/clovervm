#ifndef CL_VALUE_TAGS_H
#define CL_VALUE_TAGS_H

#include <cstdint>

namespace cl
{
    /*
      A Value is a 64-bit generic cell to hold any value. It holds some
      values inline, and some indirectly.

      The structure uses a 5-bit tag in the lowest bits:

          XXXXXXXXX....XXXXTTTTT

      The tag bits are divided as follows:

          bit 4 (0x10): refcounted pointer
          bit 3 (0x08): interned pointer
          bit 2 (0x04): boolean tag
          bits 1-0:    other special values

      Boolean values also use bit 5 for truthiness and cheap integer
      conversion. Other truthy inline singletons can use bit 5 the same way.

      The inline/pointer tags are:

          0x00: small integer, stored as signed two's complement in the upper
                59 bits
          0x01: None
          0x02: not-present VM sentinel
          0x03: exception VM sentinel
          0x04: False
          0x24: True
          0x25: NotImplemented
          0x26: Ellipsis
          0x08: interned pointer
          0x10: refcounted pointer
    */
    static constexpr uint64_t value_tag_bits = 5;
    static constexpr uint64_t value_tag_mask = 0x1f;
    static constexpr uint64_t value_ptr_granularity = value_tag_mask + 1;
    static constexpr uint64_t value_refcounted_ptr_tag = 0x10;
    static constexpr uint64_t value_interned_ptr_tag = 0x08;
    static constexpr uint64_t value_special_mask = 0x07;
    static constexpr uint64_t value_boolean_tag = 0x04;
    static constexpr uint64_t value_none = 0x01;
    static constexpr uint64_t value_not_present = 0x02;
    static constexpr uint64_t value_exception = 0x03;
    static constexpr uint64_t value_not_implemented = 0x25;
    static constexpr uint64_t value_ellipsis = 0x26;
    static constexpr uint64_t value_truthy_mask = 0xffffffffffffffe0ull;
    static constexpr uint64_t value_ptr_mask =
        value_refcounted_ptr_tag | value_interned_ptr_tag;
    static constexpr uint64_t value_not_smi_mask = value_tag_mask;
    static constexpr uint64_t value_not_smi_or_boolean_mask =
        value_tag_mask & ~value_boolean_tag;
    static constexpr uint64_t value_boolean_to_integer_mask =
        ~value_boolean_tag;
    static constexpr int64_t value_smi_min =
        -(int64_t{1} << (64 - value_tag_bits - 1));
    static constexpr int64_t value_smi_max =
        (int64_t{1} << (64 - value_tag_bits - 1)) - 1;

    static_assert((value_not_implemented & value_ptr_mask) == 0);
    static_assert((value_ellipsis & value_ptr_mask) == 0);
    static_assert((value_not_implemented & value_truthy_mask) != 0);
    static_assert((value_ellipsis & value_truthy_mask) != 0);
    static_assert((value_not_implemented >> value_tag_bits) != 0);
    static_assert((value_ellipsis >> value_tag_bits) != 0);
    static_assert((value_none & value_tag_mask) !=
                  (value_not_implemented & value_tag_mask));
    static_assert((value_none & value_tag_mask) !=
                  (value_ellipsis & value_tag_mask));
    static_assert((value_not_implemented & value_tag_mask) !=
                  (value_ellipsis & value_tag_mask));

}  // namespace cl

#endif  // CL_VALUE_TAGS_H
