#include "klass.h"
#include "refcount.h"

namespace cl
{
    static Value klass_str(Value v)
    {
        const CLKlass *k = (const CLKlass*)v.get_ptr();
        Value res = string_make_z(k->klass_name);
        return res;
    }


    CLKlass cl_klass_klass = MAKE_KLASS(L"class", klass_str);

}