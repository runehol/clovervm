#ifndef CL_KLASS_H
#define CL_KLASS_H

#include "cl_string.h"
typedef cl_value (*cl_arity_one_function)(cl_value);

typedef struct cl_klass
{
    struct cl_object obj;
    const cl_wchar *klass_name;
    cl_arity_one_function str;

} cl_klass;

#define MAKE_KLASS(name, str_fun) \
    (cl_klass){.obj=(cl_object){.klass=&cl_klass_klass, .refcount=9999, .n_cells=0, .size_in_cells=2}, \
     .klass_name=(name), \
     .str=(str_fun), \
    } \

extern cl_klass cl_klass_klass; // the klass object of all klasses.



#endif //CL_KLASS_H