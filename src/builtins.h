#ifndef CL_BUILTINS_H
#define CL_BUILTINS_H

namespace cl
{
    class VirtualMachine;

    void install_builtin_function_bindings(VirtualMachine *vm);
}  // namespace cl

#endif  // CL_BUILTINS_H
