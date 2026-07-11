#include <clovervm/native_module.h>
#include <stdint.h>

static clover_handle answer_func(clover_context *ctx)
{
    return clover_int_from_int64(ctx, 42);
}

static clover_handle identity_func(clover_context *ctx, clover_handle value)
{
    (void)ctx;
    return value;
}

static clover_handle is_identical_func(clover_context *ctx, clover_handle left,
                                       clover_handle right)
{
    bool identical;
    if(clover_is(ctx, left, right, &identical) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_int_from_int64(ctx, identical ? 1 : 0);
}

static clover_handle double_constant_func(clover_context *ctx)
{
    return clover_float_from_double(ctx, 1.5);
}

static clover_handle float_plus_one_func(clover_context *ctx,
                                         clover_handle value)
{
    double number;
    if(clover_float_as_double(ctx, value, &number) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_float_from_double(ctx, number + 1.0);
}

static clover_handle tuple2_func(clover_context *ctx, clover_handle arg0,
                                 clover_handle arg1)
{
    return clover_tuple_from_pair(ctx, arg0, arg1);
}

static clover_handle tuple3_func(clover_context *ctx, clover_handle arg0,
                                 clover_handle arg1, clover_handle arg2)
{
    clover_handle items[] = {arg0, arg1, arg2};
    return clover_tuple_from_array(ctx, items, 3);
}

static clover_handle empty_tuple_func(clover_context *ctx)
{
    return clover_tuple_from_array(ctx, 0, 0);
}

static clover_handle overflow_handles_func(clover_context *ctx)
{
    clover_handle result = clover_none(ctx);
    for(int idx = 0; idx < 70; ++idx)
    {
        result = clover_float_from_double(ctx, (double)idx + 0.5);
    }
    return result;
}

static clover_handle bad_tuple_func(clover_context *ctx)
{
    return clover_tuple_from_array(ctx, 0, 1);
}

static clover_handle propagated_error_consumers_func(clover_context *ctx)
{
    clover_handle error = clover_int_from_int64(ctx, INT64_MAX);

    size_t tuple_size = 42;
    if(clover_tuple_size(ctx, error, &tuple_size) != CLOVER_STATUS_ERROR ||
       tuple_size != 0)
    {
        return clover_raise_value_error(ctx,
                                        "clover_tuple_size contract failure");
    }

    clover_handle tuple_item = error;
    if(clover_tuple_get_item(ctx, error, 0, &tuple_item) != CLOVER_STATUS_ERROR)
    {
        return clover_raise_value_error(
            ctx, "clover_tuple_get_item contract failure");
    }
    bool item_is_none = false;
    if(clover_is(ctx, tuple_item, clover_none(ctx), &item_is_none) !=
           CLOVER_STATUS_OK ||
       !item_is_none)
    {
        return clover_raise_value_error(
            ctx, "clover_tuple_get_item output contract failure");
    }

    size_t string_size = 42;
    if(clover_string_as_utf8(ctx, error, 0, 0, &string_size) !=
           CLOVER_STATUS_ERROR ||
       string_size != 0)
    {
        return clover_raise_value_error(
            ctx, "clover_string_as_utf8 contract failure");
    }

    double float_value = 42.0;
    if(clover_float_as_double(ctx, error, &float_value) !=
           CLOVER_STATUS_ERROR ||
       float_value != 0.0)
    {
        return clover_raise_value_error(
            ctx, "clover_float_as_double contract failure");
    }

    int64_t int_value = 42;
    if(clover_int_as_int64(ctx, error, &int_value) != CLOVER_STATUS_ERROR ||
       int_value != 0)
    {
        return clover_raise_value_error(ctx,
                                        "clover_int_as_int64 contract failure");
    }

    bool identical = true;
    if(clover_is(ctx, error, error, &identical) != CLOVER_STATUS_ERROR ||
       identical)
    {
        return clover_raise_value_error(ctx, "clover_is contract failure");
    }

    return error;
}

static clover_status read_double(clover_context *ctx, clover_handle value,
                                 double *out)
{
    return clover_float_as_double(ctx, value, out);
}

static clover_handle sum2_func(clover_context *ctx, clover_handle arg0,
                               clover_handle arg1)
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

static clover_handle sum3_func(clover_context *ctx, clover_handle arg0,
                               clover_handle arg1, clover_handle arg2)
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

static clover_handle sum4_func(clover_context *ctx, clover_handle arg0,
                               clover_handle arg1, clover_handle arg2,
                               clover_handle arg3)
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

static clover_handle sum5_func(clover_context *ctx, clover_handle arg0,
                               clover_handle arg1, clover_handle arg2,
                               clover_handle arg3, clover_handle arg4)
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

static clover_handle sum6_func(clover_context *ctx, clover_handle arg0,
                               clover_handle arg1, clover_handle arg2,
                               clover_handle arg3, clover_handle arg4,
                               clover_handle arg5)
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

static clover_handle sum7_func(clover_context *ctx, clover_handle arg0,
                               clover_handle arg1, clover_handle arg2,
                               clover_handle arg3, clover_handle arg4,
                               clover_handle arg5, clover_handle arg6)
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

static clover_handle dict_new_func(clover_context *ctx)
{
    return clover_dict_new(ctx);
}

static clover_handle dict_check_func(clover_context *ctx, clover_handle value)
{
    bool is_dict;
    bool is_exact;
    if(clover_dict_check(ctx, value, &is_dict) != CLOVER_STATUS_OK ||
       clover_dict_check_exact(ctx, value, &is_exact) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_tuple_from_pair(ctx,
                                  clover_int_from_int64(ctx, is_dict ? 1 : 0),
                                  clover_int_from_int64(ctx, is_exact ? 1 : 0));
}

static clover_handle dict_size_func(clover_context *ctx, clover_handle dict)
{
    size_t size;
    if(clover_dict_size(ctx, dict, &size) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_int_from_int64(ctx, (int64_t)size);
}

static clover_handle dict_clear_func(clover_context *ctx, clover_handle dict)
{
    if(clover_dict_clear(ctx, dict) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_none(ctx);
}

static clover_handle dict_copy_func(clover_context *ctx, clover_handle dict)
{
    return clover_dict_copy(ctx, dict);
}

static clover_handle dict_set_item_func(clover_context *ctx, clover_handle dict,
                                        clover_handle key, clover_handle value)
{
    if(clover_dict_set_item(ctx, dict, key, value) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_none(ctx);
}

static clover_handle dict_del_item_func(clover_context *ctx, clover_handle dict,
                                        clover_handle key)
{
    if(clover_dict_del_item(ctx, dict, key) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_none(ctx);
}

static clover_handle dict_contains_func(clover_context *ctx, clover_handle dict,
                                        clover_handle key)
{
    bool contains;
    if(clover_dict_contains(ctx, dict, key, &contains) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_int_from_int64(ctx, contains ? 1 : 0);
}

static clover_handle dict_get_item_func(clover_context *ctx, clover_handle dict,
                                        clover_handle key)
{
    bool found;
    clover_handle value;
    if(clover_dict_get_item(ctx, dict, key, &found, &value) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_tuple_from_pair(
        ctx, clover_int_from_int64(ctx, found ? 1 : 0), value);
}

static clover_handle dict_set_default_func(clover_context *ctx,
                                           clover_handle dict,
                                           clover_handle key,
                                           clover_handle default_value)
{
    bool was_present;
    clover_handle value;
    if(clover_dict_set_default(ctx, dict, key, default_value, &was_present,
                               &value) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_tuple_from_pair(
        ctx, clover_int_from_int64(ctx, was_present ? 1 : 0), value);
}

static clover_handle dict_pop_func(clover_context *ctx, clover_handle dict,
                                   clover_handle key)
{
    bool found;
    clover_handle value;
    if(clover_dict_pop(ctx, dict, key, &found, &value) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_tuple_from_pair(
        ctx, clover_int_from_int64(ctx, found ? 1 : 0), value);
}

static clover_handle dict_set_item_string_func(clover_context *ctx,
                                               clover_handle dict,
                                               clover_handle value)
{
    if(clover_dict_set_item_string(ctx, dict, "key", value) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_none(ctx);
}

static clover_handle dict_get_item_string_func(clover_context *ctx,
                                               clover_handle dict)
{
    bool found;
    clover_handle value;
    if(clover_dict_get_item_string(ctx, dict, "key", &found, &value) !=
       CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_tuple_from_pair(
        ctx, clover_int_from_int64(ctx, found ? 1 : 0), value);
}

static clover_handle dict_contains_string_func(clover_context *ctx,
                                               clover_handle dict)
{
    bool contains;
    if(clover_dict_contains_string(ctx, dict, "key", &contains) !=
       CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_int_from_int64(ctx, contains ? 1 : 0);
}

static clover_handle dict_del_item_string_func(clover_context *ctx,
                                               clover_handle dict)
{
    if(clover_dict_del_item_string(ctx, dict, "key") != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_none(ctx);
}

static clover_handle dict_pop_string_func(clover_context *ctx,
                                          clover_handle dict)
{
    bool found;
    clover_handle value;
    if(clover_dict_pop_string(ctx, dict, "key", &found, &value) !=
       CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_tuple_from_pair(
        ctx, clover_int_from_int64(ctx, found ? 1 : 0), value);
}

static clover_handle dict_keys_func(clover_context *ctx, clover_handle dict)
{
    return clover_dict_keys(ctx, dict);
}

static clover_handle dict_values_func(clover_context *ctx, clover_handle dict)
{
    return clover_dict_values(ctx, dict);
}

static clover_handle dict_items_func(clover_context *ctx, clover_handle dict)
{
    return clover_dict_items(ctx, dict);
}

static clover_handle dict_next_func(clover_context *ctx, clover_handle dict,
                                    clover_handle position_value)
{
    int64_t position_int;
    if(clover_int_as_int64(ctx, position_value, &position_int) !=
           CLOVER_STATUS_OK ||
       position_int < 0)
    {
        return clover_propagate_error(ctx);
    }
    size_t position = (size_t)position_int;
    bool found;
    clover_handle key;
    clover_handle value;
    if(clover_dict_next(ctx, dict, &position, &found, &key, &value) !=
       CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    clover_handle result[] = {clover_int_from_int64(ctx, found ? 1 : 0),
                              clover_int_from_int64(ctx, (int64_t)position),
                              key, value};
    return clover_tuple_from_array(ctx, result, 4);
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
    if(clover_module_add_value(builder, "init_handle_0",
                               clover_int_from_int64(ctx, 0)) !=
           CLOVER_STATUS_OK ||
       clover_module_add_value(builder, "init_handle_1",
                               clover_int_from_int64(ctx, 1)) !=
           CLOVER_STATUS_OK ||
       clover_module_add_value(builder, "init_handle_2",
                               clover_int_from_int64(ctx, 2)) !=
           CLOVER_STATUS_OK ||
       clover_module_add_value(builder, "init_handle_3",
                               clover_int_from_int64(ctx, 3)) !=
           CLOVER_STATUS_OK ||
       clover_module_add_value(builder, "overflow_init_value",
                               clover_float_from_double(ctx, 4.5)) !=
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
    if(clover_module_add_function_2(
           builder, "is_identical", is_identical_func,
           "Return 1 when the arguments are identical.") != CLOVER_STATUS_OK)
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
    if(clover_module_add_function_0(
           builder, "overflow_handles", overflow_handles_func,
           "Return a value after overflowing indirect handle storage.") !=
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
    if(clover_module_add_function_0(
           builder, "propagated_error_consumers",
           propagated_error_consumers_func,
           "Exercise value consumers with a propagated error handle.") !=
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
    if(clover_module_add_function_7(builder, "sum7", sum7_func,
                                    "Return the argument sum.") !=
       CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(clover_module_add_function_0(builder, "dict_new", dict_new_func, 0) !=
           CLOVER_STATUS_OK ||
       clover_module_add_function_1(builder, "dict_check", dict_check_func,
                                    0) != CLOVER_STATUS_OK ||
       clover_module_add_function_1(builder, "dict_size", dict_size_func, 0) !=
           CLOVER_STATUS_OK ||
       clover_module_add_function_1(builder, "dict_clear", dict_clear_func,
                                    0) != CLOVER_STATUS_OK ||
       clover_module_add_function_1(builder, "dict_copy", dict_copy_func, 0) !=
           CLOVER_STATUS_OK ||
       clover_module_add_function_3(builder, "dict_set_item",
                                    dict_set_item_func,
                                    0) != CLOVER_STATUS_OK ||
       clover_module_add_function_2(builder, "dict_del_item",
                                    dict_del_item_func,
                                    0) != CLOVER_STATUS_OK ||
       clover_module_add_function_2(builder, "dict_contains",
                                    dict_contains_func,
                                    0) != CLOVER_STATUS_OK ||
       clover_module_add_function_2(builder, "dict_get_item",
                                    dict_get_item_func,
                                    0) != CLOVER_STATUS_OK ||
       clover_module_add_function_3(builder, "dict_set_default",
                                    dict_set_default_func,
                                    0) != CLOVER_STATUS_OK ||
       clover_module_add_function_2(builder, "dict_pop", dict_pop_func, 0) !=
           CLOVER_STATUS_OK ||
       clover_module_add_function_2(builder, "dict_set_item_string",
                                    dict_set_item_string_func,
                                    0) != CLOVER_STATUS_OK ||
       clover_module_add_function_1(builder, "dict_get_item_string",
                                    dict_get_item_string_func,
                                    0) != CLOVER_STATUS_OK ||
       clover_module_add_function_1(builder, "dict_contains_string",
                                    dict_contains_string_func,
                                    0) != CLOVER_STATUS_OK ||
       clover_module_add_function_1(builder, "dict_del_item_string",
                                    dict_del_item_string_func,
                                    0) != CLOVER_STATUS_OK ||
       clover_module_add_function_1(builder, "dict_pop_string",
                                    dict_pop_string_func,
                                    0) != CLOVER_STATUS_OK ||
       clover_module_add_function_1(builder, "dict_keys", dict_keys_func, 0) !=
           CLOVER_STATUS_OK ||
       clover_module_add_function_1(builder, "dict_values", dict_values_func,
                                    0) != CLOVER_STATUS_OK ||
       clover_module_add_function_1(builder, "dict_items", dict_items_func,
                                    0) != CLOVER_STATUS_OK ||
       clover_module_add_function_2(builder, "dict_next", dict_next_func, 0) !=
           CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    return CLOVER_STATUS_OK;
}
