#include "cl_short_vector.h"
#include "cl_klass.h"

static cl_value short_vector_str(cl_value s)
{
    return s;
}

cl_klass cl_short_vector_klass = MAKE_KLASS(L"persistent_vector", short_vector_str);