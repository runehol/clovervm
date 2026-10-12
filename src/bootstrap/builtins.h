#ifndef CL_BUILTINS_H
#define CL_BUILTINS_H

#include "object_model/typed_value.h"

namespace cl
{
    class VirtualMachine;
    class ModuleObject;

    Expected<void>
    install_builtin_function_bindings(VirtualMachine *vm,
                                      ModuleObject *implementation_module);
}  // namespace cl

#endif  // CL_BUILTINS_H
