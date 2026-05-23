#ifndef CLOVERVM_NATIVE_MODULE_H
#define CLOVERVM_NATIVE_MODULE_H

#include <clovervm/clovervm.h>

#ifdef _WIN32
#define CL_NATIVE_MODULE_EXPORT __declspec(dllexport)
#else
#define CL_NATIVE_MODULE_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct clover_native_module_builder clover_native_module_builder;
    typedef struct clover_call_context clover_call_context;
    typedef struct clover_value clover_value;

#ifdef __cplusplus
}
#endif

#endif  // CLOVERVM_NATIVE_MODULE_H
