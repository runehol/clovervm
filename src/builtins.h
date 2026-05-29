#ifndef CL_BUILTINS_H
#define CL_BUILTINS_H

#include "typed_value.h"

namespace cl
{
    class VirtualMachine;

    Expected<void> install_builtin_function_bindings(VirtualMachine *vm);
}  // namespace cl

#endif  // CL_BUILTINS_H
