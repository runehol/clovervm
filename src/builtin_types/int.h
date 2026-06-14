#ifndef CL_INT_H
#define CL_INT_H

#include "builtin_types/bigint.h"
#include "object_model/builtin_class_registry.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"
#include <string_view>

namespace cl
{
    class ThreadState;
    class VirtualMachine;

    enum class IntToSmiStatus
    {
        Converted,
        NotInt
    };

    bool is_intlike_value(Value value);
    bool is_exact_int_value(Value value);
    [[nodiscard]] Expected<IntToSmiStatus>
    try_intlike_value_to_smi(Value value, TValue<SMI> *out);
    [[nodiscard]] Expected<IntToSmiStatus>
    try_exact_int_value_to_smi(Value value, TValue<SMI> *out);
    ConstBigIntView intlike_value_bigint_view(Value value,
                                              SmiBigInt *smi_storage);
    ConstBigIntView exact_int_value_bigint_view(Value value,
                                                SmiBigInt *smi_storage);
    [[nodiscard]] Expected<Value> parse_int_string_view(ThreadState *thread,
                                                        std::wstring_view text);

    BuiltinClassDefinition make_int_class(VirtualMachine *vm);
    void install_int_class_methods(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_INT_H
