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
    static constexpr uint64_t value_refcounted_ptr_tag = 0x10;
    static constexpr uint64_t value_immportal_ptr_tag  = 0x08;
    static constexpr uint64_t value_interned_ptr_tag   = 0x04;
    static constexpr uint64_t value_special_tag        = 0x02;
    static constexpr uint64_t value_truthy_mask          = 0xffffffffffffffe1ull;
    static constexpr uint64_t value_ptr_mask = value_refcounted_ptr_tag|value_immportal_ptr_tag|value_interned_ptr_tag;
    static constexpr uint64_t value_not_smi_mask = value_tag_mask;


    union Value
    {
        long long v;
        struct Object *ptr;

        bool operator==(Value o) const
        {
            return v == o.v;
        }
    };

    static inline bool value_is_smi(Value val)
    {
        return (val.v &value_tag_mask) == 0;
    }



    static inline bool value_is_ptr(Value val)
    {
        return (val.v &value_ptr_mask) != 0;
    }

    static inline bool value_is_refcounted_ptr(Value val)
    {
        return (val.v &value_ptr_mask) != 0;
    }

    static inline int64_t value_get_smi(Value val)
    {
        assert(value_is_smi(val));
        return val.v >> value_tag_bits;
    }

    static inline bool value_is_smi8(Value val)
    {
        if(!value_is_smi(val)) return false;

        int64_t v = value_get_smi(val);
        return v >= -128 && v <= 127;
    }


    static inline Value value_make_smi(int64_t v)
    {
        return (Value){.v = v<<value_tag_bits};

    }

    static inline Value value_make_oop(Object *obj)
    {
        return (Value){.ptr = obj};
    }

    static inline struct Object *value_get_ptr(Value val)
    {
        assert(value_is_ptr(val));
        return val.ptr;
    }

    static inline bool value_is_truthy(Value val)
    {
        return (val.v&value_truthy_mask) != 0;
    }
    static inline bool value_is_falsy(Value val)
    {
        return (val.v&value_truthy_mask) == 0;
    }

    static constexpr Value cl_None = (Value){.v=0x42};
    static constexpr Value cl_False = (Value){.v=0x22};
    static constexpr Value cl_True = (Value){.v=0x23};
    static constexpr Value cl_exception_marker = (Value){.v=0x82}; // special value to return if an exception has been thrown.
    static constexpr Value cl_not_present_marker = (Value){.v=0x102}; // special value to mark deleted/not present entries in scopes.

}

#endif //CL_VALUE_H
