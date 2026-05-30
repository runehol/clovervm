#ifndef CL_CONSTRUCTOR_THUNK_H
#define CL_CONSTRUCTOR_THUNK_H

#include "function.h"
#include "typed_value.h"

namespace cl
{
    class ClassObject;

    Expected<TValue<Function>>
    make_init_only_constructor_thunk_function(ClassObject *cls,
                                              Optional<TValue<Function>> init);
}  // namespace cl

#endif  // CL_CONSTRUCTOR_THUNK_H
