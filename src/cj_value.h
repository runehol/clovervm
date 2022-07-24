#ifndef CJ_VALUE_H
#define CJ_VALUE_H

#include <stdint.h>
#include <assert.h>
#include <stdbool.h>

struct cj_object;



typedef union cj_value
{
    int64_t smi_value;
    struct cj_object *ptr_value;

} cj_value;

static inline bool value_is_smi(cj_value val)
{
    return (val.smi_value &0x1) == 0;
}
static inline bool value_is_oop(cj_value val)
{
    return (val.smi_value &0x1) == 1;
}

static inline int64_t value_get_smi(cj_value val)
{
    assert(value_is_smi(val));
    return val.smi_value >> 1;
}

static inline struct CJObject *value_get_oop(cj_value val)
{
    assert(value_is_oop(val));
    return (struct CJObject *)(val.smi_value - 1);
}

extern cj_value cj_nil;
extern cj_value cj_true;
extern cj_value cj_false;


#endif //CJ_VALUE_H