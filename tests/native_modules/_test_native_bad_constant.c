#include <clovervm/native_module.h>

CL_NATIVE_MODULE_EXPORT clover_status
clover_module_init__test_native_bad_constant(
    clover_context *ctx, clover_native_module_builder *builder)
{
    (void)ctx;
    return clover_module_add_int_constant(builder, "", 1);
}
