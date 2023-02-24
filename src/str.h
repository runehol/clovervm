#ifndef CL_STRING_H
#define CL_STRING_H

#include "object.h"
#include "alloc.h"
#include "value.h"
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
        assert(count.is_smi());
        object_init(&vec->obj, &cl_string_klass, 1, 1 + count.get_smi()*sizeof(cl_wchar));
        vec->count = count;
    }

    static inline Value string_make(const cl_wchar *data, Value count)
    {
        assert(count.is_smi());
        String *s = cl_alloc<String>(sizeof(String)+count.get_smi() * sizeof(cl_wchar));
        string_init(s, data, count);
        return Value::from_oop(&s->obj);
    }

    static inline Value string_make_z(const cl_wchar *data)
    {
        return string_make(data, Value::from_smi(wcslen(data)));
    }
}




#endif //CL_STRING_H
