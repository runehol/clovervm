#ifndef CL_KLASS_H
#define CL_KLASS_H

#include "str.h"

namespace cl
{
    typedef Value (*arity_one_function)(Value);

    extern Klass cl_klass_klass; // the klass object of all klasses.

    struct Klass : public Object
    {
        constexpr Klass(const cl_wchar *_klass_name, arity_one_function str_fun)
            : Object(&cl_klass_klass, 0, 2),
              klass_name(_klass_name),
              str(str_fun)
        {}

        const cl_wchar *klass_name;
        arity_one_function str;

    };





}

#endif //CL_KLASS_H
