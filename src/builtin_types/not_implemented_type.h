#ifndef CL_NOT_IMPLEMENTED_TYPE_H
#define CL_NOT_IMPLEMENTED_TYPE_H

#include "object_model/builtin_class_registry.h"

namespace cl
{
    class VirtualMachine;

    BuiltinClassDefinition make_not_implemented_type_class(VirtualMachine *vm);
    void install_not_implemented_type_class_methods(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_NOT_IMPLEMENTED_TYPE_H
