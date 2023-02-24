#ifndef CL_VALUE_H
#define CL_VALUE_H

#include <stdint.h>
#include <assert.h>
#include <stdbool.h>

namespace cl
{
    struct Object;

/*
  A cl_value is a 64-bit generic cell to hold any value. It holds some of them inline, and some of them indirect.

  The structure is as follows, with a 5 bit tag in the lowest bits
  XXXXXXXXX....XXXXTTTTT

  The tag is divided as follows:
  Bit 4 (16): Refcounted pointer
  Bit 3 (8): Permanent (non-refcounted) pointer
  Bit 2 (4): Interned pointer
  Bit 1 (2): Special value
  Bit 0 (1): Special truthy value

  The tags then are as follows:
  00000: small integer, with the value is stored as signed two complement in the upper 59 bits.
  00011: special truthy value. Only one known: 0x23: True
  00010: special falsy value. Two known: 0x22: False, 0x42: None
  00100: interned pointer.
  01000: permanent pointer
  10000: refcounted pointer



*/

    static constexpr uint64_t value_tag_bits = 5;
    static constexpr uint64_t value_tag_mask = 0x1f;
    static constexpr uint64_t value_ptr_granularity = value_tag_mask+1;
    static constexpr uint64_t value_refcounted_ptr_tag = 0x10;
    static constexpr uint64_t value_immportal_ptr_tag  = 0x08;
    static constexpr uint64_t value_interned_ptr_tag   = 0x04;
    static constexpr uint64_t value_special_tag        = 0x02;
    static constexpr uint64_t value_truthy_mask          = 0xffffffffffffffe1ull;
    static constexpr uint64_t value_ptr_mask = value_refcounted_ptr_tag|value_immportal_ptr_tag|value_interned_ptr_tag;
    static constexpr uint64_t value_not_smi_mask = value_tag_mask;



    struct Value
    {
        static inline Value from_smi(int64_t v)
        {
            Value val;
            val.as.integer = v<<value_tag_bits;
            return val;

        }

        static inline Value from_oop(Object *obj)
        {
            Value val;
            val.as.ptr = obj;
            return val;
        }

        static inline Value None()
        {
            Value val;
            val.as.integer = 0x42;
            return val;
        }

        static inline Value False()
        {
            Value val;
            val.as.integer = 0x22;
            return val;
        }

        static inline Value True()
        {
            Value val;
            val.as.integer = 0x23;
            return val;
        }

        static inline Value exception_marker()
        {
            Value val;
            val.as.integer = 0x82; // special value to return if an exception has been thrown.
            return val;
        }

        static inline Value not_present_marker(int32_t next_idx=-1)
        {
            Value val;
            val.as.integer = (int64_t(next_idx) << 32) | 0x102; // special value to mark deleted/not present entries in scopes.
            return val;
        }

        union {
            long long integer;
            struct Object *ptr;
        } as;

        bool operator==(Value o) const
        {
            return as.integer == o.as.integer;
        }

        bool is_smi() const
        {
            return (as.integer & value_tag_mask) == 0;
        }



        bool is_ptr() const
        {
            return (as.integer &value_ptr_mask) != 0;
        }

        bool is_refcounted_ptr() const
        {
            return (as.integer &value_ptr_mask) != 0;
        }

        int64_t get_smi() const
        {
            assert(is_smi());
            return as.integer >> value_tag_bits;
        }

        bool is_smi8() const
        {
            if(!is_smi()) return false;

            int64_t v = get_smi();
            return v >= -128 && v <= 127;
        }



        Object *get_ptr() const
        {
            assert(is_ptr());
            return as.ptr;
        }

        bool value_is_truthy() const
        {
            return (as.integer&value_truthy_mask) != 0;
        }
        bool is_falsy() const
        {
            return (as.integer&value_truthy_mask) == 0;
        }

    };




}

#endif //CL_VALUE_H
