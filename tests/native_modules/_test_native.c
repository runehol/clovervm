#include <clovervm/native_module.h>

static clover_value answer_func(clover_context *ctx)
{
    return clover_int_from_int64(ctx, 42);
}

static clover_value identity_func(clover_context *ctx, clover_value value)
{
    (void)ctx;
    return value;
}

static clover_value double_constant_func(clover_context *ctx)
{
    return clover_float_from_double(ctx, 1.5);
}

static clover_value float_plus_one_func(clover_context *ctx, clover_value value)
{
    double number;
    if(clover_float_as_double(ctx, value, &number) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_float_from_double(ctx, number + 1.0);
}

static clover_value tuple2_func(clover_context *ctx, clover_value arg0,
                                clover_value arg1)
{
    return clover_tuple_from_pair(ctx, arg0, arg1);
}

static clover_value tuple3_func(clover_context *ctx, clover_value arg0,
                                clover_value arg1, clover_value arg2)
{
    clover_value items[] = {arg0, arg1, arg2};
    return clover_tuple_from_array(ctx, items, 3);
}

static clover_value empty_tuple_func(clover_context *ctx)
{
    return clover_tuple_from_array(ctx, 0, 0);
}

static clover_value bad_tuple_func(clover_context *ctx)
{
    return clover_tuple_from_array(ctx, 0, 1);
}

static clover_status read_double(clover_context *ctx, clover_value value,
                                 double *out)
{
    return clover_float_as_double(ctx, value, out);
}

static clover_value sum2_func(clover_context *ctx, clover_value arg0,
                              clover_value arg1)
{
    double v0;
    double v1;
    if(read_double(ctx, arg0, &v0) != CLOVER_STATUS_OK ||
       read_double(ctx, arg1, &v1) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_float_from_double(ctx, v0 + v1);
}

static clover_value sum3_func(clover_context *ctx, clover_value arg0,
                              clover_value arg1, clover_value arg2)
{
    double v0;
    double v1;
    double v2;
    if(read_double(ctx, arg0, &v0) != CLOVER_STATUS_OK ||
       read_double(ctx, arg1, &v1) != CLOVER_STATUS_OK ||
       read_double(ctx, arg2, &v2) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_float_from_double(ctx, v0 + v1 + v2);
}

static clover_value sum4_func(clover_context *ctx, clover_value arg0,
                              clover_value arg1, clover_value arg2,
                              clover_value arg3)
{
    double v0;
    double v1;
    double v2;
    double v3;
    if(read_double(ctx, arg0, &v0) != CLOVER_STATUS_OK ||
       read_double(ctx, arg1, &v1) != CLOVER_STATUS_OK ||
       read_double(ctx, arg2, &v2) != CLOVER_STATUS_OK ||
       read_double(ctx, arg3, &v3) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_float_from_double(ctx, v0 + v1 + v2 + v3);
}

static clover_value sum5_func(clover_context *ctx, clover_value arg0,
                              clover_value arg1, clover_value arg2,
                              clover_value arg3, clover_value arg4)
{
    double v0;
    double v1;
    double v2;
    double v3;
    double v4;
    if(read_double(ctx, arg0, &v0) != CLOVER_STATUS_OK ||
       read_double(ctx, arg1, &v1) != CLOVER_STATUS_OK ||
       read_double(ctx, arg2, &v2) != CLOVER_STATUS_OK ||
       read_double(ctx, arg3, &v3) != CLOVER_STATUS_OK ||
       read_double(ctx, arg4, &v4) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_float_from_double(ctx, v0 + v1 + v2 + v3 + v4);
}

static clover_value sum6_func(clover_context *ctx, clover_value arg0,
                              clover_value arg1, clover_value arg2,
                              clover_value arg3, clover_value arg4,
                              clover_value arg5)
{
    double v0;
    double v1;
    double v2;
    double v3;
    double v4;
    double v5;
    if(read_double(ctx, arg0, &v0) != CLOVER_STATUS_OK ||
       read_double(ctx, arg1, &v1) != CLOVER_STATUS_OK ||
       read_double(ctx, arg2, &v2) != CLOVER_STATUS_OK ||
       read_double(ctx, arg3, &v3) != CLOVER_STATUS_OK ||
       read_double(ctx, arg4, &v4) != CLOVER_STATUS_OK ||
       read_double(ctx, arg5, &v5) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_float_from_double(ctx, v0 + v1 + v2 + v3 + v4 + v5);
}

static clover_value sum7_func(clover_context *ctx, clover_value arg0,
                              clover_value arg1, clover_value arg2,
                              clover_value arg3, clover_value arg4,
                              clover_value arg5, clover_value arg6)
{
    double v0;
    double v1;
    double v2;
    double v3;
    double v4;
    double v5;
    double v6;
    if(read_double(ctx, arg0, &v0) != CLOVER_STATUS_OK ||
       read_double(ctx, arg1, &v1) != CLOVER_STATUS_OK ||
       read_double(ctx, arg2, &v2) != CLOVER_STATUS_OK ||
       read_double(ctx, arg3, &v3) != CLOVER_STATUS_OK ||
       read_double(ctx, arg4, &v4) != CLOVER_STATUS_OK ||
       read_double(ctx, arg5, &v5) != CLOVER_STATUS_OK ||
       read_double(ctx, arg6, &v6) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_float_from_double(ctx, v0 + v1 + v2 + v3 + v4 + v5 + v6);
}

CL_NATIVE_MODULE_EXPORT clover_status clover_module_init__test_native(
    clover_context *ctx, clover_native_module_builder *builder)
{
    if(clover_module_add_value(builder, "answer",
                               clover_int_from_int64(ctx, 42)) !=
       CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_value(
           builder, "greeting",
           clover_string_from_utf8(ctx, "hello \xce\xbb")) != CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_value(builder, "nothing", clover_none(ctx)) !=
       CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_function_0(builder, "answer_func", answer_func,
                                    "Return 42.") != CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_function_1(builder, "identity_func", identity_func,
                                    "Return the argument.") != CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_function_0(builder, "double_constant",
                                    double_constant_func,
                                    "Return 1.5.") != CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_function_1(
           builder, "float_plus_one", float_plus_one_func,
           "Return float(value) + 1.0.") != CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_function_2(builder, "tuple2", tuple2_func,
                                    "Return a two-item tuple.") !=
       CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_function_3(builder, "tuple3", tuple3_func,
                                    "Return a three-item tuple.") !=
       CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_function_0(builder, "empty_tuple", empty_tuple_func,
                                    "Return an empty tuple.") !=
       CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_function_0(builder, "bad_tuple", bad_tuple_func,
                                    "Try to construct an invalid tuple.") !=
       CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_function_2(builder, "sum2", sum2_func,
                                    "Return the argument sum.") !=
       CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_function_3(builder, "sum3", sum3_func,
                                    "Return the argument sum.") !=
       CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_function_4(builder, "sum4", sum4_func,
                                    "Return the argument sum.") !=
       CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_function_5(builder, "sum5", sum5_func,
                                    "Return the argument sum.") !=
       CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_function_6(builder, "sum6", sum6_func,
                                    "Return the argument sum.") !=
       CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    return clover_module_add_function_7(builder, "sum7", sum7_func,
                                        "Return the argument sum.");
}
