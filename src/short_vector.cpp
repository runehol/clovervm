#include "short_vector.h"
#include "klass.h"

namespace cl
{
    static Value short_vector_str(Value s)
    {
        return s;
    }

    Klass cl_short_vector_klass(L"persistent_vector", short_vector_str);
}
