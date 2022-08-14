#include "cl_klass.h"
#include "cl_refcount.h"

static cl_value klass_str(cl_value v)
{
    const cl_klass *k = (const cl_klass*)value_get_ptr(v);
    cl_value res = string_make_z(k->klass_name);
    cl_decref(v);
    return res;
}


cl_klass cl_klass_klass = MAKE_KLASS(L"class", klass_str);
