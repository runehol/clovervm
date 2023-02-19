#include "cl_short_vector.h"
#include "cl_klass.h"

static CLValue short_vector_str(CLValue s)
{
    return s;
}

CLKlass cl_short_vector_klass = MAKE_KLASS(L"persistent_vector", short_vector_str);