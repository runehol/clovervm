#ifndef CL_CLOVER_ENTRY_H
#define CL_CLOVER_ENTRY_H

#include "typed_value.h"
#include <cstdint>

namespace cl
{
    struct CodeObject;
    class VirtualMachine;

    constexpr uint32_t MaxCloverFunctionEntryAdapterArgs = 3;

    TValue<CodeObject>
    make_startup_wrapper_code_object(CodeObject *entry_code_object);

    CodeObject *
    make_clover_function_entry_adapter_code_object(VirtualMachine *vm,
                                                   uint32_t n_args);

}  // namespace cl

#endif  // CL_CLOVER_ENTRY_H
