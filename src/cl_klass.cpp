#include "cl_klass.h"
#include "cl_refcount.h"

namespace cl
{
    static Value klass_str(Value v)
    {
        const CLKlass *k = (const CLKlass*)value_get_ptr(v);
        Value res = string_make_z(k->klass_name);
        cl_decref(v);
        return res;
    }


    CLKlass cl_klass_klass = MAKE_KLASS(L"class", klass_str);

}
