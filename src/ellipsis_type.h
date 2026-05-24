#ifndef CL_ELLIPSIS_TYPE_H
#define CL_ELLIPSIS_TYPE_H

#include "builtin_class_registry.h"

namespace cl
{
    class VirtualMachine;

    BuiltinClassDefinition make_ellipsis_type_class(VirtualMachine *vm);
    void install_ellipsis_type_class_methods(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_ELLIPSIS_TYPE_H
