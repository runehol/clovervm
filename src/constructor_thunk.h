#ifndef CL_CONSTRUCTOR_THUNK_H
#define CL_CONSTRUCTOR_THUNK_H

#include "function.h"
#include "value_state.h"

namespace cl
{
    class ClassObject;

    TValue<Function>
    make_constructor_thunk_function(ClassObject *cls,
                                    Optional<TValue2<Function>> init);
}  // namespace cl

#endif  // CL_CONSTRUCTOR_THUNK_H
