#ifndef CL_NATIVE_MODULE_API_INTERNAL_H
#define CL_NATIVE_MODULE_API_INTERNAL_H

#include "native/native_handle.h"
#include <clovervm/native_module.h>

namespace cl
{
    class ModuleObject;
}  // namespace cl

struct clover_native_module_builder
{
    cl::ThreadState *thread;
    cl::ModuleObject *module;
};

#endif  // CL_NATIVE_MODULE_API_INTERNAL_H
