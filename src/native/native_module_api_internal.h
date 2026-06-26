#ifndef CL_NATIVE_MODULE_API_INTERNAL_H
#define CL_NATIVE_MODULE_API_INTERNAL_H

#include "object_model/value.h"
#include <clovervm/native_module.h>
#include <cstring>

namespace cl
{
    class ModuleObject;
    class ThreadState;

    inline clover_handle wrap_clover_handle(Value value)
    {
        static_assert(sizeof(clover_handle) == sizeof(value.as.integer));
        clover_handle result;
        std::memcpy(&result, &value.as.integer, sizeof(result));
        return result;
    }

    inline Value unwrap_clover_handle(clover_handle value)
    {
        Value result;
        static_assert(sizeof(value) == sizeof(result.as.integer));
        std::memcpy(&result.as.integer, &value, sizeof(value));
        return result;
    }
}  // namespace cl

struct clover_context
{
    cl::ThreadState *thread;
};

struct clover_native_module_builder
{
    cl::ThreadState *thread;
    cl::ModuleObject *module;
};

#endif  // CL_NATIVE_MODULE_API_INTERNAL_H
