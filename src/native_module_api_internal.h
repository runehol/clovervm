#ifndef CL_NATIVE_MODULE_API_INTERNAL_H
#define CL_NATIVE_MODULE_API_INTERNAL_H

#include "value.h"
#include <clovervm/native_module.h>
#include <cstring>

namespace cl
{
    class ModuleObject;
    class ThreadState;

    inline clover_value wrap_clover_value(Value value)
    {
        static_assert(sizeof(clover_value) == sizeof(value.as.integer));
        clover_value result;
        std::memcpy(&result, &value.as.integer, sizeof(result));
        return result;
    }

    inline Value unwrap_clover_value(clover_value value)
    {
        Value result;
        static_assert(sizeof(value) == sizeof(result.as.integer));
        std::memcpy(&result.as.integer, &value, sizeof(value));
        return result;
    }
}  // namespace cl

struct clover_call_context
{
    cl::ThreadState *thread;
};

struct clover_native_module_builder
{
    cl::ThreadState *thread;
    cl::ModuleObject *module;
};

#endif  // CL_NATIVE_MODULE_API_INTERNAL_H
