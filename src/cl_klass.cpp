#include "cl_klass.h"
#include "cl_refcount.h"

namespace cl
{
    static CLValue klass_str(CLValue v)
    {
        const CLKlass *k = (const CLKlass*)value_get_ptr(v);
        CLValue res = string_make_z(k->klass_name);
        cl_decref(v);
        return res;
    }


    CLKlass cl_klass_klass = MAKE_KLASS(L"class", klass_str);

}
