#ifndef CL_INT_H
#define CL_INT_H

#include "object_model/builtin_class_registry.h"

namespace cl
{
    class VirtualMachine;

    BuiltinClassDefinition make_int_class(VirtualMachine *vm);
    void install_int_class_methods(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_INT_H
