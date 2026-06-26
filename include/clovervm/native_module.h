#ifndef CLOVERVM_NATIVE_MODULE_H
#define CLOVERVM_NATIVE_MODULE_H

#include <clovervm/clovervm.h>

#ifdef _WIN32
#define CL_NATIVE_MODULE_EXPORT __declspec(dllexport)
#else
#define CL_NATIVE_MODULE_EXPORT __attribute__((visibility("default")))
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct clover_native_module_builder clover_native_module_builder;
    typedef struct clover_context clover_context;

    /*
     * clover_handle is an opaque value handle. Extension modules must not
     * inspect or construct the value directly; use clover_* helpers instead.
     */
    typedef uintptr_t clover_handle;

    /*
     * The context is valid only for the duration of the API entry point that
     * received it.
     */
    typedef clover_handle (*clover_extension_fn_0)(clover_context *ctx);
    typedef clover_handle (*clover_extension_fn_1)(clover_context *ctx,
                                                   clover_handle arg0);
    typedef clover_handle (*clover_extension_fn_2)(clover_context *ctx,
                                                   clover_handle arg0,
                                                   clover_handle arg1);
    typedef clover_handle (*clover_extension_fn_3)(clover_context *ctx,
                                                   clover_handle arg0,
                                                   clover_handle arg1,
                                                   clover_handle arg2);
    typedef clover_handle (*clover_extension_fn_4)(clover_context *ctx,
                                                   clover_handle arg0,
                                                   clover_handle arg1,
                                                   clover_handle arg2,
                                                   clover_handle arg3);
    typedef clover_handle (*clover_extension_fn_5)(
        clover_context *ctx, clover_handle arg0, clover_handle arg1,
        clover_handle arg2, clover_handle arg3, clover_handle arg4);
    typedef clover_handle (*clover_extension_fn_6)(
        clover_context *ctx, clover_handle arg0, clover_handle arg1,
        clover_handle arg2, clover_handle arg3, clover_handle arg4,
        clover_handle arg5);
    typedef clover_handle (*clover_extension_fn_7)(
        clover_context *ctx, clover_handle arg0, clover_handle arg1,
        clover_handle arg2, clover_handle arg3, clover_handle arg4,
        clover_handle arg5, clover_handle arg6);

    /*
     * All const char * strings in the native module API are UTF-8 encoded
     * unless a function documents a narrower temporary restriction.
     */
    CL_EXPORT clover_status
    clover_module_add_value(clover_native_module_builder *builder,
                            const char *name, clover_handle value);
    CL_EXPORT clover_status clover_module_add_function_0(
        clover_native_module_builder *builder, const char *name,
        clover_extension_fn_0 function, const char *docstring);
    CL_EXPORT clover_status clover_module_add_function_1(
        clover_native_module_builder *builder, const char *name,
        clover_extension_fn_1 function, const char *docstring);
    CL_EXPORT clover_status clover_module_add_function_2(
        clover_native_module_builder *builder, const char *name,
        clover_extension_fn_2 function, const char *docstring);
    CL_EXPORT clover_status clover_module_add_function_3(
        clover_native_module_builder *builder, const char *name,
        clover_extension_fn_3 function, const char *docstring);
    CL_EXPORT clover_status clover_module_add_function_4(
        clover_native_module_builder *builder, const char *name,
        clover_extension_fn_4 function, const char *docstring);
    CL_EXPORT clover_status clover_module_add_function_5(
        clover_native_module_builder *builder, const char *name,
        clover_extension_fn_5 function, const char *docstring);
    CL_EXPORT clover_status clover_module_add_function_6(
        clover_native_module_builder *builder, const char *name,
        clover_extension_fn_6 function, const char *docstring);
    CL_EXPORT clover_status clover_module_add_function_7(
        clover_native_module_builder *builder, const char *name,
        clover_extension_fn_7 function, const char *docstring);

    CL_EXPORT clover_handle clover_none(clover_context *ctx);
    CL_EXPORT clover_handle clover_int_from_int64(clover_context *ctx,
                                                  int64_t value);
    CL_EXPORT clover_handle clover_float_from_double(clover_context *ctx,
                                                     double value);
    CL_EXPORT clover_handle clover_string_from_utf8(clover_context *ctx,
                                                    const char *utf8_value);
    CL_EXPORT clover_handle clover_tuple_from_array(clover_context *ctx,
                                                    const clover_handle *items,
                                                    size_t count);
    CL_EXPORT clover_handle clover_tuple_from_pair(clover_context *ctx,
                                                   clover_handle item0,
                                                   clover_handle item1);
    CL_EXPORT clover_status clover_tuple_size(clover_context *ctx,
                                              clover_handle value, size_t *out);
    CL_EXPORT clover_status clover_tuple_get_item(clover_context *ctx,
                                                  clover_handle value,
                                                  size_t index,
                                                  clover_handle *out);
    CL_EXPORT clover_status clover_string_as_utf8(clover_context *ctx,
                                                  clover_handle value,
                                                  char *out,
                                                  size_t out_capacity,
                                                  size_t *out_size);
    CL_EXPORT clover_status clover_float_as_double(clover_context *ctx,
                                                   clover_handle value,
                                                   double *out);
    CL_EXPORT clover_status clover_int_as_int64(clover_context *ctx,
                                                clover_handle value,
                                                int64_t *out);
    CL_EXPORT clover_status clover_is(clover_context *ctx, clover_handle left,
                                      clover_handle right, bool *out);
    CL_EXPORT clover_handle
    clover_raise_overflow_error(clover_context *ctx, const char *utf8_message);
    CL_EXPORT clover_handle clover_raise_value_error(clover_context *ctx,
                                                     const char *utf8_message);
    CL_EXPORT clover_handle clover_propagate_error(clover_context *ctx);

#ifdef __cplusplus
}
#endif

#endif  // CLOVERVM_NATIVE_MODULE_H
