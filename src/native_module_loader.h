#ifndef CL_NATIVE_MODULE_LOADER_H
#define CL_NATIVE_MODULE_LOADER_H

#include "value.h"

namespace cl
{
    class ModuleObject;
    class ThreadState;
    struct ModuleSpec;

    [[nodiscard]] Value exec_native_extension_module(ThreadState *thread,
                                                     const ModuleSpec &spec,
                                                     ModuleObject *module);

}  // namespace cl

#endif  // CL_NATIVE_MODULE_LOADER_H
