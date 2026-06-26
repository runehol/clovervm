#include <clovervm/native_module.h>
#include <stdint.h>

CL_NATIVE_MODULE_EXPORT clover_status clover_module_init__test_native_bad_value(
    clover_context *ctx, clover_native_module_builder *builder)
{
    clover_handle overflowed = clover_int_from_int64(ctx, INT64_MAX);
    return clover_module_add_value(builder, "bad", overflowed);
}
