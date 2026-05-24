#ifndef CLOVERVM_NATIVE_MODULE_H
#define CLOVERVM_NATIVE_MODULE_H

#include <clovervm/clovervm.h>

#ifdef _WIN32
#define CL_NATIVE_MODULE_EXPORT __declspec(dllexport)
#else
#define CL_NATIVE_MODULE_EXPORT __attribute__((visibility("default")))
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct clover_native_module_builder clover_native_module_builder;
    typedef struct clover_call_context clover_call_context;
    typedef struct clover_value clover_value;

    /*
     * All const char * strings in the native module API are UTF-8 encoded
     * unless a function documents a narrower temporary restriction.
     */
    CL_EXPORT clover_status clover_module_add_int_constant(
        clover_native_module_builder *builder, const char *name, int64_t value);

#ifdef __cplusplus
}
#endif

#endif  // CLOVERVM_NATIVE_MODULE_H
