#include "str.h"
#include "klass.h"

namespace cl
{
    static Value string_str(Value s)
    {
        return s;
    }

    CLKlass cl_string_klass = MAKE_KLASS(L"string", string_str);

}
