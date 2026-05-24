#include <clovervm/native_module.h>

#include <math.h>

static clover_value math_sqrt(clover_context *ctx, clover_value value)
{
    double number;
    if(clover_float_as_double(ctx, value, &number) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    if(number < 0.0)
    {
        return clover_raise_value_error(ctx, "math domain error");
    }
    return clover_float_from_double(ctx, sqrt(number));
}

CL_NATIVE_MODULE_EXPORT clover_status clover_module_init__math(
    clover_context *ctx, clover_native_module_builder *builder)
{
    (void)ctx;
    return clover_module_add_function_1(builder, "sqrt", math_sqrt,
                                        "Return the square root of x.");
}
