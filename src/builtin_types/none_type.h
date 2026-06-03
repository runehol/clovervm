#ifndef CL_NONE_TYPE_H
#define CL_NONE_TYPE_H

#include "object_model/builtin_class_registry.h"

namespace cl
{
    class VirtualMachine;

    BuiltinClassDefinition make_none_type_class(VirtualMachine *vm);
    void install_none_type_class_methods(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_NONE_TYPE_H
