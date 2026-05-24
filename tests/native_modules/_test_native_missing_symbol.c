#include <clovervm/native_module.h>

CL_NATIVE_MODULE_EXPORT clover_status clover_module_init_wrong_name(
    clover_context *ctx, clover_native_module_builder *builder)
{
    (void)ctx;
    (void)builder;
    return CLOVER_STATUS_OK;
}
