#ifndef CL_VALUE_H
#define CL_VALUE_H

#include <stdint.h>
#include <assert.h>
#include <stdbool.h>

struct cl_object;



typedef struct cl_value
{
    int64_t v;
} cl_value;

static inline bool value_is_smi(cl_value val)
{
    return (val.v &0x1) == 0;
}
static inline bool value_is_oop(cl_value val)
{
    return (val.v &0x1) == 1;
}

static inline int64_t value_get_smi(cl_value val)
{
    assert(value_is_smi(val));
    return val.v >> 1;
}

static inline cl_value value_make_smi(int64_t v)
{
    return (cl_value){.v = v<<1};

}

static inline cl_value value_make_oop(cl_object *obj)
{
    return (cl_value){.v = (int64_t)(obj) + 1};
}

static inline struct CJObject *value_get_oop(cl_value val)
{
    assert(value_is_oop(val));
    return (struct CJObject *)(val.v - 1);
}

extern cl_value cl_nil;
extern cl_value cl_true;
extern cl_value cl_false;


#endif //CL_VALUE_H