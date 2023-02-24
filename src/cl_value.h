#ifndef CL_VALUE_H
#define CL_VALUE_H

#include <stdint.h>
#include <assert.h>
#include <stdbool.h>

struct CLObject;

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

static constexpr uint64_t cl_tag_bits = 5;
static constexpr uint64_t cl_tag_mask = 0x1f;
static constexpr uint64_t cl_refcounted_ptr_tag = 0x10;
static constexpr uint64_t cl_permanent_ptr_tag  = 0x08;
static constexpr uint64_t cl_interned_ptr_tag   = 0x04;
static constexpr uint64_t cl_special_tag        = 0x02;
static constexpr uint64_t cl_truthy_mask          = 0xffffffffffffffe1ull;
static constexpr uint64_t cl_ptr_mask = cl_refcounted_ptr_tag|cl_permanent_ptr_tag|cl_interned_ptr_tag;
static constexpr uint64_t cl_not_smi_mask = cl_tag_mask;


typedef union CLValue
{
    long long v;
    struct CLObject *ptr;

    bool operator==(CLValue o) const
    {
        return v == o.v;
    }
} CLValue;

static inline bool value_is_smi(CLValue val)
{
    return (val.v &cl_tag_mask) == 0;
}



static inline bool value_is_ptr(CLValue val)
{
    return (val.v &cl_ptr_mask) != 0;
}

static inline bool value_is_refcounted_ptr(CLValue val)
{
    return (val.v &cl_ptr_mask) != 0;
}

static inline int64_t value_get_smi(CLValue val)
{
    assert(value_is_smi(val));
    return val.v >> cl_tag_bits;
}

static inline bool value_is_smi8(CLValue val)
{
    if(!value_is_smi(val)) return false;

    int64_t v = value_get_smi(val);
    return v >= -128 && v <= 127;
}


static inline CLValue value_make_smi(int64_t v)
{
    return (CLValue){.v = v<<cl_tag_bits};

}

static inline CLValue value_make_oop(CLObject *obj)
{
    return (CLValue){.ptr = obj};
}

static inline struct CLObject *value_get_ptr(CLValue val)
{
    assert(value_is_ptr(val));
    return val.ptr;
}

static inline bool value_is_truthy(CLValue val)
{
    return (val.v&cl_truthy_mask) != 0;
}
static inline bool value_is_falsy(CLValue val)
{
    return (val.v&cl_truthy_mask) == 0;
}

static constexpr CLValue cl_None = (CLValue){.v=0x42};
static constexpr CLValue cl_False = (CLValue){.v=0x22};
static constexpr CLValue cl_True = (CLValue){.v=0x23};
static constexpr CLValue cl_exception_marker = (CLValue){.v=0x82}; // special value to return if an exception has been thrown.
static constexpr CLValue cl_not_found_marker = (CLValue){.v=0x102}; // special value to mark deleted/not present entries in scopes.


#endif //CL_VALUE_H
