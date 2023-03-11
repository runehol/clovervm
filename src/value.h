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
  Bit 3 (8): Interned pointer
  Bit 2 (4): Boolean. Also sets bit 5 for truthiness, for easy conversion and comparison to int
  Bit 1-0: Other special values

  The tags then are as follows:
  00000: small integer, with the value is stored as signed two complement in the upper 59 bits.
  00011: special truthy value. Only one known: 0x23: True
  00010: special falsy value. Two known: 0x22: False, 0x42: None
  01000: interned pointer
  10000: refcounted pointer



*/
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)


    static constexpr uint64_t value_tag_bits = 5;
    static constexpr uint64_t value_tag_mask = 0x1f;
    static constexpr uint64_t value_ptr_granularity = value_tag_mask+1;
    static constexpr uint64_t value_refcounted_ptr_tag = 0x10;
    static constexpr uint64_t value_interned_ptr_tag   = 0x08;
    static constexpr uint64_t value_special_mask       = 0x07;
    static constexpr uint64_t value_boolean_tag        = 0x04;
    static constexpr uint64_t value_none               = 0x01;
    static constexpr uint64_t value_not_present        = 0x02;
    static constexpr uint64_t value_exception          = 0x03;
    static constexpr uint64_t value_truthy_mask        = 0xffffffffffffffe0ull;
    static constexpr uint64_t value_ptr_mask = value_refcounted_ptr_tag|value_interned_ptr_tag;
    static constexpr uint64_t value_not_smi_mask = value_tag_mask;
    static constexpr uint64_t value_not_smi_or_boolean_mask = value_tag_mask & ~value_boolean_tag;
    static constexpr uint64_t value_boolean_to_integer_mask = ~value_boolean_tag;



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
            val.as.integer = value_none;
            return val;
        }

        static inline Value False()
        {
            Value val;
            val.as.integer = value_boolean_tag;
            return val;
        }

        static inline Value True()
        {
            Value val;
            val.as.integer = value_boolean_tag | (1<<value_tag_bits);
            return val;
        }

        static inline Value exception_marker()
        {
            Value val;
            val.as.integer = value_exception; // special value to return if an exception has been thrown.
            return val;
        }

        static inline Value not_present(int32_t next_idx=-1)
        {
            Value val;
            val.as.integer = (int64_t(next_idx) << 32) | value_not_present; // special value to mark deleted/not present entries in scopes.
            return val;
        }

        bool is_not_present() const
        {
            uint32_t v = as.integer;
            return v == value_not_present;
        }

        int32_t get_not_present_index() const
        {
            assert(is_not_present());
            return as.integer >> 32;
        }

        union {
            long long integer;
            struct Object *ptr;
        } as;

        bool operator==(Value o) const
        {
            return as.integer == o.as.integer;
        }
        bool operator!=( Value o) const
        {
            return as.integer != o.as.integer;
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

        bool is_truthy() const
        {
            assert(!is_ptr());
            return (as.integer&value_truthy_mask) != 0;
        }
        bool is_falsy() const
        {
            assert(!is_ptr());
            return (as.integer&value_truthy_mask) == 0;
        }

    };




}

#endif //CL_VALUE_H
