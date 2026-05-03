#ifndef CL_STARTUP_WRAPPER_H
#define CL_STARTUP_WRAPPER_H

#include "typed_value.h"

namespace cl
{
    struct CodeObject;

    TValue<CodeObject>
    make_startup_wrapper_code_object(CodeObject *entry_code_object);

}  // namespace cl

#endif  // CL_STARTUP_WRAPPER_H
