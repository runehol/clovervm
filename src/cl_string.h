#ifndef CL_STRING_H
#define CL_STRING_H

#include "cl_object.h"
#include "cl_alloc.h"
#include "cl_value.h"
#include <stdint.h>
#include <assert.h>
#include <wchar.h>

typedef wchar_t cl_wchar;

typedef struct cl_string
{
    cl_object obj;
    cl_value count;
    cl_wchar data[];

} cl_string;

static inline void string_init(cl_string *vec, const cl_wchar *data, cl_value count)
{
    assert(value_is_smi(count));
    object_init(&vec->obj, NULL, 1, 1 + value_get_smi(count)*sizeof(cl_wchar));
    vec->count = count;
}

static inline cl_value string_make(const cl_wchar *data, cl_value count)
{
    cl_string *s = (cl_string *)cl_alloc(sizeof(cl_string)+value_get_smi(count) * sizeof(cl_wchar));
    string_init(s, data, count);
    return value_make_oop(&s->obj);
}





#endif //CL_STRING_H