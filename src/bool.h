#ifndef CL_BOOL_H
#define CL_BOOL_H

#include "builtin_class_registry.h"

namespace cl
{
    class ClassObject;
    class VirtualMachine;

    BuiltinClassDefinition make_bool_class(VirtualMachine *vm,
                                           ClassObject *int_class);
    void install_bool_class_methods(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_BOOL_H
