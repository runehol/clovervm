#ifndef CL_NATIVE_EXTENSION_LOADER_H
#define CL_NATIVE_EXTENSION_LOADER_H

#include "object_model/value.h"

namespace cl
{
    class ModuleObject;
    class ThreadState;
    struct ModuleSpec;

    [[nodiscard]] Value exec_native_extension_module(ThreadState *thread,
                                                     const ModuleSpec &spec,
                                                     ModuleObject *module);

}  // namespace cl

#endif  // CL_NATIVE_EXTENSION_LOADER_H
