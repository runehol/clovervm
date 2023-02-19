#ifndef CL_KLASS_H
#define CL_KLASS_H

#include "cl_string.h"
typedef CLValue (*cl_arity_one_function)(CLValue);

typedef struct CLKlass
{
    struct CLObject obj;
    const cl_wchar *klass_name;
    cl_arity_one_function str;

} CLKlass;

#define MAKE_KLASS(name, str_fun) \
    (CLKlass){.obj=(CLObject){.klass=&cl_klass_klass, .refcount=9999, .n_cells=0, .size_in_cells=2}, \
     .klass_name=(name), \
     .str=(str_fun), \
    } \

extern CLKlass cl_klass_klass; // the klass object of all klasses.



#endif //CL_KLASS_H