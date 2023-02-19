#include "cl_persistent_list.h"
#include "cl_klass.h"

static CLValue persistent_list_str(CLValue s)
{
    return s;
}

CLKlass cl_persistent_list_klass = MAKE_KLASS(L"persistent_list", persistent_list_str);