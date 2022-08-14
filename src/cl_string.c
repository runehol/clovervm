#include "cl_string.h"
#include "cl_klass.h"

static cl_value string_str(cl_value s)
{
    return s;
}

cl_klass cl_string_klass = MAKE_KLASS(L"string", string_str);