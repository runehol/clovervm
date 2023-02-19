#ifndef CL_STRING_H
#define CL_STRING_H

#include "cl_object.h"
#include "cl_alloc.h"
#include "cl_value.h"
#include <stdint.h>
#include <assert.h>
#include <wchar.h>

typedef wchar_t cl_wchar;

typedef struct CLString
{
    CLObject obj;
    CLValue count;
    cl_wchar data[];

} CLString;

extern struct CLKlass cl_string_klass;


static inline void string_init(CLString *vec, const cl_wchar *data, CLValue count)
{
    assert(value_is_smi(count));
    object_init(&vec->obj, &cl_string_klass, 1, 1 + value_get_smi(count)*sizeof(cl_wchar));
    vec->count = count;
}

static inline CLValue string_make(const cl_wchar *data, CLValue count)
{
    CLString *s = (CLString *)cl_alloc(sizeof(CLString)+value_get_smi(count) * sizeof(cl_wchar));
    string_init(s, data, count);
    return value_make_oop(&s->obj);
}

static inline CLValue string_make_z(const cl_wchar *data)
{
    return string_make(data, value_make_smi(wcslen(data)));
}





#endif //CL_STRING_H