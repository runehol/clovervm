#include "cl_short_vector.h"
#include "cl_klass.h"

namespace cl
{
    static Value short_vector_str(Value s)
    {
        return s;
    }

    CLKlass cl_short_vector_klass = MAKE_KLASS(L"persistent_vector", short_vector_str);
}
