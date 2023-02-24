#include "klass.h"
#include "refcount.h"

namespace cl
{
    static Value klass_str(Value v)
    {
        const Klass *k = (const Klass*)v.get_ptr();
        Value res = make_interned_string(k->klass_name);
        return res;
    }

    Klass cl_klass_klass(L"class", klass_str);

}
