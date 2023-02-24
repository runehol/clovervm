#include "str.h"
#include "klass.h"

namespace cl
{
    static Value string_str(Value s)
    {
        return s;
    }

    Klass cl_string_klass(L"string", string_str);

}
