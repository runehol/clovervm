#ifndef CL_CONSTRUCTOR_THUNK_H
#define CL_CONSTRUCTOR_THUNK_H

#include "function.h"
#include "typed_value.h"
#include "value.h"

namespace cl
{
    class ClassObject;

    TValue<Function> make_constructor_thunk_function(ClassObject *cls,
                                                     Value init);
}  // namespace cl

#endif  // CL_CONSTRUCTOR_THUNK_H
