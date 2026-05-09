#ifndef CL_BOOL_H
#define CL_BOOL_H

#include "builtin_class_registry.h"

namespace cl
{
    class ClassObject;
    class VirtualMachine;

    BuiltinClassDefinition make_bool_class(VirtualMachine *vm,
                                           ClassObject *int_class);

}  // namespace cl

#endif  // CL_BOOL_H
