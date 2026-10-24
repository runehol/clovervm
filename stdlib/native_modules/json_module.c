/* Private cursor primitives for json.py. Encoding policy stays in Python. */
#include <clovervm/native_module.h>

#include <stdint.h>

static clover_handle json_is_exact_dict(clover_context *ctx,
                                        clover_handle value)
{
    bool exact;
    if(clover_dict_check_exact(ctx, value, &exact) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_bool_from_bool(ctx, exact);
}

static clover_handle json_dict_next(clover_context *ctx, clover_handle dict,
                                    clover_handle cursor)
{
    int64_t cursor_int;
    if(clover_int_as_int64(ctx, cursor, &cursor_int) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    if(cursor_int < 0)
    {
        return clover_raise_value_error(ctx, "dict cursor must be nonnegative");
    }
    size_t position = (size_t)cursor_int;
    bool found;
    clover_handle key;
    clover_handle value;
    if(clover_dict_next(ctx, dict, &position, &found, &key, &value) !=
       CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    if(!found)
    {
        return clover_none(ctx);
    }
    clover_handle fields[] = {clover_int_from_uint64(ctx, (uint64_t)position),
                              key, value};
    return clover_tuple_from_array(ctx, fields, 3);
}

CL_NATIVE_MODULE_EXPORT clover_status clover_module_init__json(
    clover_context *ctx, clover_native_module_builder *builder)
{
    (void)ctx;
    if(clover_module_add_function_1(
           builder, "is_exact_dict", json_is_exact_dict,
           "Return True for an exact dict, else False.") != CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    return clover_module_add_function_2(
        builder, "dict_next", json_dict_next,
        "Return (next_cursor, key, value), or None at the current end.");
}
