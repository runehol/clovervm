#ifndef CL_KLASS_H
#define CL_KLASS_H

#include "cl_string.h"
typedef cl_value (*cl_arity_one_function)(cl_value);

typedef struct cl_klass
{
    struct cl_klass *klass;
    const cl_wchar *klass_name;
    cl_arity_one_function str;

} cl_klass;

extern cl_klass cl_klass_klass; // the klass object of all klasses.



#endif //CL_KLASS_H