#ifndef CL_INT_H
#define CL_INT_H

#include "builtin_types/bigint.h"
#include "object_model/builtin_class_registry.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"
#include "util/compiler.h"
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
    // These historical exact-int helpers exclude bool but accept heap int
    // subclasses. Callers needing exact class identity must check the class.
    bool is_exact_int_value(Value value);
    [[nodiscard]] NOINLINE Expected<TValue<SMI>>
    try_intlike_value_to_smi_slow(Value value,
                                  const wchar_t *type_error_message);
    [[nodiscard]] inline Expected<TValue<SMI>> try_intlike_value_to_smi(
        Value value, const wchar_t *type_error_message = L"expected an integer")
    {
        if(value.is_smi())
        {
            return Expected<TValue<SMI>>::ok(
                TValue<SMI>::from_value_assumed(value));
        }
        return try_intlike_value_to_smi_slow(value, type_error_message);
    }
    [[nodiscard]] Expected<IntToSmiStatus>
    try_exact_int_value_to_smi(Value value, TValue<SMI> *out);
    ConstBigIntView intlike_value_bigint_view(Value value,
                                              SmiViewStorage *smi_storage);
    ConstBigIntView exact_int_value_bigint_view(Value value,
                                                SmiViewStorage *smi_storage);
    [[nodiscard]] Expected<Value> parse_int_string_view(ThreadState *thread,
                                                        std::wstring_view text);

    BuiltinClassDefinition make_int_class(VirtualMachine *vm);
    void install_int_class_methods(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_INT_H
