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

    /*
     * clover_value is an opaque value handle. Extension modules must not
     * inspect or construct the value directly; use clover_* helpers instead.
     */
    typedef uintptr_t clover_value;

    /*
     * The call context is valid only for the duration of the extension function
     * call that received it.
     */
    typedef clover_value (*clover_extension_fn_0)(clover_call_context *ctx);
    typedef clover_value (*clover_extension_fn_1)(clover_call_context *ctx,
                                                  clover_value arg0);

    /*
     * All const char * strings in the native module API are UTF-8 encoded
     * unless a function documents a narrower temporary restriction.
     */
    CL_EXPORT clover_status clover_module_add_int_constant(
        clover_native_module_builder *builder, const char *name, int64_t value);
    CL_EXPORT clover_status
    clover_module_add_string_constant(clover_native_module_builder *builder,
                                      const char *name, const char *utf8_value);
    CL_EXPORT clover_status clover_module_add_function_0(
        clover_native_module_builder *builder, const char *name,
        clover_extension_fn_0 function, const char *docstring);
    CL_EXPORT clover_status clover_module_add_function_1(
        clover_native_module_builder *builder, const char *name,
        clover_extension_fn_1 function, const char *docstring);

    CL_EXPORT clover_value clover_none(clover_call_context *ctx);
    CL_EXPORT clover_value clover_int64(clover_call_context *ctx,
                                        int64_t value);
    CL_EXPORT clover_value clover_float_from_double(clover_call_context *ctx,
                                                    double value);
    CL_EXPORT clover_status clover_float_as_double(clover_call_context *ctx,
                                                   clover_value value,
                                                   double *out);
    CL_EXPORT clover_value clover_raise_value_error(clover_call_context *ctx,
                                                    const char *utf8_message);
    CL_EXPORT clover_value clover_error(clover_call_context *ctx);

#ifdef __cplusplus
}
#endif

#endif  // CLOVERVM_NATIVE_MODULE_H
