#ifndef CL_CLOVER_ENTRY_H
#define CL_CLOVER_ENTRY_H

#include <cstdint>

namespace cl
{
    class CodeObject;
    class VirtualMachine;

    constexpr uint32_t MaxCloverFunctionEntryAdapterArgs = 3;

    CodeObject *
    make_clover_function_entry_adapter_code_object(VirtualMachine *vm,
                                                   uint32_t n_args);

}  // namespace cl

#endif  // CL_CLOVER_ENTRY_H
