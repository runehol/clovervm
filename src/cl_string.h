#ifndef CL_STRING_H
#define CL_STRING_H

#include "cl_object.h"
#include "cl_alloc.h"
#include "cl_value.h"
#include <stdint.h>
#include <assert.h>
#include <wchar.h>


namespace cl
{
    typedef wchar_t cl_wchar;

    typedef struct String
    {
        Object obj;
        Value count;
        cl_wchar data[];

    } String;

    extern struct CLKlass cl_string_klass;


    static inline void string_init(String *vec, const cl_wchar *data, Value count)
    {
        assert(value_is_smi(count));
        object_init(&vec->obj, &cl_string_klass, 1, 1 + value_get_smi(count)*sizeof(cl_wchar));
        vec->count = count;
    }

    static inline Value string_make(const cl_wchar *data, Value count)
    {
        String *s = cl_alloc<String>(sizeof(String)+value_get_smi(count) * sizeof(cl_wchar));
        string_init(s, data, count);
        return value_make_oop(&s->obj);
    }

    static inline Value string_make_z(const cl_wchar *data)
    {
        return string_make(data, value_make_smi(wcslen(data)));
    }
}




#endif //CL_STRING_H
