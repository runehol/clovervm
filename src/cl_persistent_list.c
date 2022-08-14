#include "cl_persistent_list.h"
#include "cl_klass.h"

static cl_value persistent_list_str(cl_value s)
{
    return s;
}

cl_klass cl_persistent_list_klass = MAKE_KLASS(L"persistent_list", persistent_list_str);