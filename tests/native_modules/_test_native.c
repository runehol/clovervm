#include <clovervm/native_module.h>

static clover_value answer_func(clover_call_context *ctx)
{
    return clover_int64(ctx, 42);
}

static clover_value identity_func(clover_call_context *ctx, clover_value value)
{
    (void)ctx;
    return value;
}

static clover_value double_constant_func(clover_call_context *ctx)
{
    return clover_float_from_double(ctx, 1.5);
}

static clover_value float_plus_one_func(clover_call_context *ctx,
                                        clover_value value)
{
    double number;
    if(clover_float_as_double(ctx, value, &number) != CLOVER_STATUS_OK)
    {
        return clover_error(ctx);
    }
    return clover_float_from_double(ctx, number + 1.0);
}

CL_NATIVE_MODULE_EXPORT clover_status
clover_module_init__test_native(clover_native_module_builder *builder)
{
    if(clover_module_add_int_constant(builder, "answer", 42) !=
       CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_string_constant(builder, "greeting",
                                         "hello \xce\xbb") != CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_function_0(builder, "answer_func", answer_func) !=
       CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_function_1(builder, "identity_func", identity_func) !=
       CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_function_0(builder, "double_constant",
                                    double_constant_func) != CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    return clover_module_add_function_1(builder, "float_plus_one",
                                        float_plus_one_func);
}
