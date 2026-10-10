#include <clovervm/native_module.h>
#include <stdint.h>

CL_NATIVE_MODULE_EXPORT clover_status
clover_module_init__test_native_success_with_exception(
    clover_context *ctx, clover_native_module_builder *builder)
{
    (void)builder;
    (void)clover_raise_overflow_error(ctx, "test native overflow");
    return CLOVER_STATUS_OK;
}
