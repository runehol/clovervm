#include "native_function.h"

#include "code_object_builder.h"
#include "thread_state.h"
#include "tuple.h"
#include "virtual_machine.h"

namespace cl
{
    static TValue<Function> make_native_function_with_target(
        VirtualMachine *vm, NativeFunctionTarget target, Bytecode call_opcode,
        uint32_t n_parameters,
        TValue<Tuple> default_parameters =
            TValue<Tuple>::unsafe_unchecked(Value::None()))
    {
        TValue<String> name =
            vm->get_or_create_interned_string_value(L"<native>");
        CodeObjectBuilder builder(vm, nullptr, nullptr, nullptr, name);
        builder.n_parameters() = n_parameters;
        builder.n_positional_parameters() = n_parameters;
        uint32_t target_idx = builder.add_native_function_target(target);
        builder.emit_call_native(0, call_opcode, uint8_t(target_idx));
        builder.emit_return(0);
        TValue<CodeObject> code =
            TValue<CodeObject>::from_oop(builder.finalize());
        if(default_parameters.as_value() != Value::None())
        {
            return vm->make_immortal_object_value<Function>(code,
                                                            default_parameters);
        }
        return vm->make_immortal_object_value<Function>(code);
    }

    TValue<Function> make_native_function(VirtualMachine *vm,
                                          NativeFunction0 function,
                                          TValue<Tuple> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed0 = function;
        return make_native_function_with_target(
            vm, target, Bytecode::CallNative0, 0, default_parameters);
    }

    TValue<Function> make_native_function(VirtualMachine *vm,
                                          NativeFunction1 function,
                                          TValue<Tuple> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed1 = function;
        return make_native_function_with_target(
            vm, target, Bytecode::CallNative1, 1, default_parameters);
    }

    TValue<Function> make_native_function(VirtualMachine *vm,
                                          NativeFunction2 function,
                                          TValue<Tuple> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed2 = function;
        return make_native_function_with_target(
            vm, target, Bytecode::CallNative2, 2, default_parameters);
    }

    TValue<Function> make_native_function(VirtualMachine *vm,
                                          NativeFunction3 function,
                                          TValue<Tuple> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed3 = function;
        return make_native_function_with_target(
            vm, target, Bytecode::CallNative3, 3, default_parameters);
    }

}  // namespace cl
