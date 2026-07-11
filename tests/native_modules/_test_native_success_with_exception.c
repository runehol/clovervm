#include <clovervm/native_module.h>
#include <stdint.h>

CL_NATIVE_MODULE_EXPORT clover_status
clover_module_init__test_native_success_with_exception(
    clover_context *ctx, clover_native_module_builder *builder)
{
    (void)builder;
    (void)clover_int_from_int64(ctx, INT64_MAX);
    return CLOVER_STATUS_OK;
}
