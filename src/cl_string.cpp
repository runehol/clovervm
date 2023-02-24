#include "cl_string.h"
#include "cl_klass.h"

namespace cl
{
    static CLValue string_str(CLValue s)
    {
        return s;
    }

    CLKlass cl_string_klass = MAKE_KLASS(L"string", string_str);

}
