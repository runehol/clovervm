#ifndef CL_KLASS_H
#define CL_KLASS_H

#include "str.h"

namespace cl
{
    typedef Value (*arity_one_function)(Value);

    extern Klass cl_klass_klass;  // the klass object of all klasses.

    struct Klass : public Object
    {
        static constexpr NativeLayoutId native_layout_id =
            NativeLayoutId::Klass;

        constexpr Klass(const cl_wchar *_klass_name, arity_one_function str_fun)
            : Object(native_layout_id, &cl_klass_klass, compact_layout()),
              klass_name(_klass_name), str(str_fun)
        {
        }

        const cl_wchar *klass_name;
        arity_one_function str;

        CL_DECLARE_STATIC_LAYOUT_NO_VALUES(Klass);
    };

}  // namespace cl

#endif  // CL_KLASS_H
