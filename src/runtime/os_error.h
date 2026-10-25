#ifndef CL_OS_ERROR_H
#define CL_OS_ERROR_H

#include "runtime/exception_object.h"

namespace cl
{
    class Dict;
    [[nodiscard]] Expected<TValue<Dict>>
    make_os_error_errno_map(ThreadState *thread);
    BuiltinClassDefinition make_os_error_class(VirtualMachine *vm,
                                               const wchar_t *name,
                                               ClassObject *base,
                                               bool register_layout = false);
    void install_os_error_methods(VirtualMachine *vm);
    [[nodiscard]] TValue<OSErrorObject>
    allocate_default_os_error(ThreadState *thread, TValue<ClassObject> cls);
    [[nodiscard]] Value prepare_os_error_new(ThreadState *thread, Value cls,
                                             Value args, Value kwargs);
    [[nodiscard]] Value finish_os_error_new(ThreadState *thread, Value self,
                                            Value args);
    [[nodiscard]] Value os_error_args_to_string(ThreadState *thread,
                                                TValue<OSErrorObject> self);
}  // namespace cl

#endif  // CL_OS_ERROR_H
