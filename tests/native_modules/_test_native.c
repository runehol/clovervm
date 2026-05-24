#include <clovervm/native_module.h>

CL_NATIVE_MODULE_EXPORT clover_status
clover_module_init__test_native(clover_native_module_builder *builder)
{
    if(clover_module_add_int_constant(builder, "answer", 42) !=
       CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    return clover_module_add_string_constant(builder, "greeting",
                                             "hello \xce\xbb");
}
