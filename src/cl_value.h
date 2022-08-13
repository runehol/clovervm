#ifndef CL_VALUE_H
#define CL_VALUE_H

#include <stdint.h>
#include <assert.h>
#include <stdbool.h>

struct cl_object;

/*
    A cl_value is a 64-bit generic cell to hold any value. It holds some of them inline, and some of them indirect.

    The structure is as follows, with a 5 bit tag in the lowest bits
    XXXXXXXXX....XXXXTTTTT

    The tag is divided as follows:
    Bit 4 (16): Refcounted pointer
    Bit 3 (8): Permanent (non-refcounted) pointer
    Bit 2 (4): Interned pointer
    Bit 1 (2): Special value
    Bit 0 (1): Falsy value

    The tags then are as follows:
    00000: small integer, with the value is stored as signed two complements in the upper 59 bits.
    00010: special truthy value. Only one known: 0x22: true
    00011: special falsy value. Two known: 0x23: false, 0x43: nil
    00100: interned pointer
    01000: permanent pointer
    10000: refcounted pointer



*/

static const uint64_t cl_tag_bits = 5;
static const uint64_t cl_tag_mask = 0x1f;
static const uint64_t cl_refcounted_ptr_tag = 0x10;
static const uint64_t cl_permanent_ptr_tag  = 0x08;
static const uint64_t cl_interned_ptr_tag   = 0x04;
static const uint64_t cl_special_tag        = 0x02;
static const uint64_t cl_falsy_tag          = 0x01;
static const uint64_t cl_ptr_mask = cl_refcounted_ptr_tag|cl_permanent_ptr_tag|cl_interned_ptr_tag;

typedef union cl_value
{
    int64_t v;
    struct cl_object *ptr;
} cl_value;

static inline bool value_is_smi(cl_value val)
{
    return (val.v &cl_tag_mask) == 0;
}
static inline bool value_is_ptr(cl_value val)
{
    return (val.v &cl_ptr_mask) != 0;
}

static inline int64_t value_get_smi(cl_value val)
{
    assert(value_is_smi(val));
    return val.v >> cl_tag_bits;
}

static inline cl_value value_make_smi(int64_t v)
{
    return (cl_value){.v = v<<cl_tag_bits};

}

static inline cl_value value_make_oop(cl_object *obj)
{
    return (cl_value){.ptr = obj};
}

static inline struct cl_object *value_get_ptr(cl_value val)
{
    assert(value_is_ptr(val));
    return val.ptr;
}

static inline bool value_is_truthy(cl_value val)
{
    return (val.v&cl_falsy_tag) == 0;
}
static inline bool value_is_falsy(cl_value val)
{
    return (val.v&cl_falsy_tag) != 0;
}

static const cl_value cl_nil = (cl_value){.v=0x43};
static const cl_value cl_true = (cl_value){.v=0x22};
static const cl_value cl_false = (cl_value){.v=0x23};
static const cl_value cl_exception_marker = (cl_value){.v=0x82}; // special value to return if an exception has been thrown.


#endif //CL_VALUE_H