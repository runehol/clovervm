#include "native_function.h"

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
        TValue<CodeObject> code = vm->make_immortal_object_value<CodeObject>(
            nullptr, nullptr, nullptr, name);
        code.extract()->n_parameters = n_parameters;
        code.extract()->n_positional_parameters = n_parameters;
        uint32_t target_idx =
            code.extract()->add_native_function_target(target);
        code.extract()->emit_opcode_native_target_idx(0, call_opcode,
                                                      uint8_t(target_idx));
        code.extract()->emit_opcode(0, Bytecode::Return);
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
