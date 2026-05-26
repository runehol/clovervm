#include "native_function.h"

#include "attribute_descriptor.h"
#include "class_object.h"
#include "code_object_builder.h"
#include "module_object.h"
#include "thread_state.h"
#include "tuple.h"
#include "virtual_machine.h"
#include <cassert>

namespace cl
{
    static TValue<Function> make_native_function_with_target(
        VirtualMachine *vm, TValue<String> name, NativeFunctionTarget target,
        Bytecode call_opcode, uint32_t n_parameters,
        Optional<TValue<String>> docstring, bool is_extension,
        Optional<TValue<Tuple>> default_parameters =
            Optional<TValue<Tuple>>::none())
    {
        CodeObjectBuilder builder(vm, nullptr, vm->global_builtins_module(),
                                  nullptr, name);
        builder.n_parameters() = n_parameters;
        builder.n_positional_parameters() = n_parameters;
        builder.function_signature().n_pos_or_kw_parameters = n_parameters;
        if(default_parameters.has_value())
        {
            uint32_t n_defaults =
                uint32_t(default_parameters.value().extract()->size());
            builder.function_signature().first_default_slot =
                n_parameters - n_defaults;
            assert(n_defaults < 64);
            builder.function_signature().default_presence_mask =
                (uint64_t(1) << n_defaults) - 1;
        }
        uint32_t target_idx = builder.add_native_function_target(target);
        if(is_extension)
        {
            builder.emit_call_extension(0, call_opcode, uint8_t(target_idx));
        }
        else
        {
            builder.emit_call_intrinsic(0, call_opcode, uint8_t(target_idx));
        }
        builder.emit_return_or_raise_exception(0);
        TValue<CodeObject> code =
            TValue<CodeObject>::from_oop(builder.finalize());
        if(default_parameters.has_value())
        {
            return vm->make_immortal_object_value<Function>(code, docstring,
                                                            default_parameters);
        }
        return vm->make_immortal_object_value<Function>(code, docstring);
    }

    static TValue<Function> make_intrinsic_function_with_target(
        VirtualMachine *vm, NativeFunctionTarget target, Bytecode call_opcode,
        uint32_t n_parameters,
        Optional<TValue<Tuple>> default_parameters =
            Optional<TValue<Tuple>>::none())
    {
        return make_native_function_with_target(
            vm, vm->get_or_create_interned_string_value(L"<native>"), target,
            call_opcode, n_parameters, Optional<TValue<String>>::none(), false,
            default_parameters);
    }

    static Bytecode call_intrinsic_opcode_for_arity(uint32_t n_parameters)
    {
        switch(n_parameters)
        {
            case 0:
                return Bytecode::CallIntrinsic0;
            case 1:
                return Bytecode::CallIntrinsic1;
            case 2:
                return Bytecode::CallIntrinsic2;
            case 3:
                return Bytecode::CallIntrinsic3;
            case 4:
                return Bytecode::CallIntrinsic4;
            case 5:
                return Bytecode::CallIntrinsic5;
            case 6:
                return Bytecode::CallIntrinsic6;
            case 7:
                return Bytecode::CallIntrinsic7;
            default:
                assert(false && "unsupported native function arity");
                return Bytecode::CallIntrinsic0;
        }
    }

    BuiltinIntrinsicMethod builtin_intrinsic_method(const wchar_t *name,
                                                    IntrinsicFunction0 function,
                                                    const wchar_t *doc)
    {
        NativeFunctionTarget target;
        target.fixed0 = function;
        return BuiltinIntrinsicMethod{name, target, 0, doc};
    }

    BuiltinIntrinsicMethod builtin_intrinsic_method(const wchar_t *name,
                                                    IntrinsicFunction1 function,
                                                    const wchar_t *doc)
    {
        NativeFunctionTarget target;
        target.fixed1 = function;
        return BuiltinIntrinsicMethod{name, target, 1, doc};
    }

    BuiltinIntrinsicMethod builtin_intrinsic_method(const wchar_t *name,
                                                    IntrinsicFunction2 function,
                                                    const wchar_t *doc)
    {
        NativeFunctionTarget target;
        target.fixed2 = function;
        return BuiltinIntrinsicMethod{name, target, 2, doc};
    }

    BuiltinIntrinsicMethod builtin_intrinsic_method(const wchar_t *name,
                                                    IntrinsicFunction3 function,
                                                    const wchar_t *doc)
    {
        NativeFunctionTarget target;
        target.fixed3 = function;
        return BuiltinIntrinsicMethod{name, target, 3, doc};
    }

    BuiltinIntrinsicMethod builtin_intrinsic_method(const wchar_t *name,
                                                    IntrinsicFunction4 function,
                                                    const wchar_t *doc)
    {
        NativeFunctionTarget target;
        target.fixed4 = function;
        return BuiltinIntrinsicMethod{name, target, 4, doc};
    }

    BuiltinIntrinsicMethod builtin_intrinsic_method(const wchar_t *name,
                                                    IntrinsicFunction5 function,
                                                    const wchar_t *doc)
    {
        NativeFunctionTarget target;
        target.fixed5 = function;
        return BuiltinIntrinsicMethod{name, target, 5, doc};
    }

    BuiltinIntrinsicMethod builtin_intrinsic_method(const wchar_t *name,
                                                    IntrinsicFunction6 function,
                                                    const wchar_t *doc)
    {
        NativeFunctionTarget target;
        target.fixed6 = function;
        return BuiltinIntrinsicMethod{name, target, 6, doc};
    }

    BuiltinIntrinsicMethod builtin_intrinsic_method(const wchar_t *name,
                                                    IntrinsicFunction7 function,
                                                    const wchar_t *doc)
    {
        NativeFunctionTarget target;
        target.fixed7 = function;
        return BuiltinIntrinsicMethod{name, target, 7, doc};
    }

    TValue<Function>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction0 function,
                            Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed0 = function;
        return make_intrinsic_function_with_target(
            vm, target, Bytecode::CallIntrinsic0, 0, default_parameters);
    }

    TValue<Function>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction1 function,
                            Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed1 = function;
        return make_intrinsic_function_with_target(
            vm, target, Bytecode::CallIntrinsic1, 1, default_parameters);
    }

    TValue<Function>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction2 function,
                            Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed2 = function;
        return make_intrinsic_function_with_target(
            vm, target, Bytecode::CallIntrinsic2, 2, default_parameters);
    }

    TValue<Function>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction3 function,
                            Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed3 = function;
        return make_intrinsic_function_with_target(
            vm, target, Bytecode::CallIntrinsic3, 3, default_parameters);
    }

    TValue<Function>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction4 function,
                            Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed4 = function;
        return make_intrinsic_function_with_target(
            vm, target, Bytecode::CallIntrinsic4, 4, default_parameters);
    }

    TValue<Function>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction5 function,
                            Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed5 = function;
        return make_intrinsic_function_with_target(
            vm, target, Bytecode::CallIntrinsic5, 5, default_parameters);
    }

    TValue<Function>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction6 function,
                            Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed6 = function;
        return make_intrinsic_function_with_target(
            vm, target, Bytecode::CallIntrinsic6, 6, default_parameters);
    }

    TValue<Function>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction7 function,
                            Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed7 = function;
        return make_intrinsic_function_with_target(
            vm, target, Bytecode::CallIntrinsic7, 7, default_parameters);
    }

    TValue<Function>
    make_intrinsic_function(VirtualMachine *vm,
                            const BuiltinIntrinsicMethod &method)
    {
        Optional<TValue<String>> docstring =
            method.doc == nullptr
                ? Optional<TValue<String>>::none()
                : Optional<TValue<String>>::some(
                      vm->get_or_create_interned_string_value(method.doc));
        return make_native_function_with_target(
            vm, vm->get_or_create_interned_string_value(method.name),
            method.target, call_intrinsic_opcode_for_arity(method.n_parameters),
            method.n_parameters, docstring, false);
    }

    TValue<Function> make_extension_function(VirtualMachine *vm,
                                             TValue<String> name,
                                             clover_extension_fn_0 function,
                                             Optional<TValue<String>> docstring)
    {
        NativeFunctionTarget target;
        target.extension0 = function;
        return make_native_function_with_target(
            vm, name, target, Bytecode::CallExtension0, 0, docstring, true);
    }

    TValue<Function> make_extension_function(VirtualMachine *vm,
                                             TValue<String> name,
                                             clover_extension_fn_1 function,
                                             Optional<TValue<String>> docstring)
    {
        NativeFunctionTarget target;
        target.extension1 = function;
        return make_native_function_with_target(
            vm, name, target, Bytecode::CallExtension1, 1, docstring, true);
    }

    TValue<Function> make_extension_function(VirtualMachine *vm,
                                             TValue<String> name,
                                             clover_extension_fn_2 function,
                                             Optional<TValue<String>> docstring)
    {
        NativeFunctionTarget target;
        target.extension2 = function;
        return make_native_function_with_target(
            vm, name, target, Bytecode::CallExtension2, 2, docstring, true);
    }

    TValue<Function> make_extension_function(VirtualMachine *vm,
                                             TValue<String> name,
                                             clover_extension_fn_3 function,
                                             Optional<TValue<String>> docstring)
    {
        NativeFunctionTarget target;
        target.extension3 = function;
        return make_native_function_with_target(
            vm, name, target, Bytecode::CallExtension3, 3, docstring, true);
    }

    TValue<Function> make_extension_function(VirtualMachine *vm,
                                             TValue<String> name,
                                             clover_extension_fn_4 function,
                                             Optional<TValue<String>> docstring)
    {
        NativeFunctionTarget target;
        target.extension4 = function;
        return make_native_function_with_target(
            vm, name, target, Bytecode::CallExtension4, 4, docstring, true);
    }

    TValue<Function> make_extension_function(VirtualMachine *vm,
                                             TValue<String> name,
                                             clover_extension_fn_5 function,
                                             Optional<TValue<String>> docstring)
    {
        NativeFunctionTarget target;
        target.extension5 = function;
        return make_native_function_with_target(
            vm, name, target, Bytecode::CallExtension5, 5, docstring, true);
    }

    TValue<Function> make_extension_function(VirtualMachine *vm,
                                             TValue<String> name,
                                             clover_extension_fn_6 function,
                                             Optional<TValue<String>> docstring)
    {
        NativeFunctionTarget target;
        target.extension6 = function;
        return make_native_function_with_target(
            vm, name, target, Bytecode::CallExtension6, 6, docstring, true);
    }

    TValue<Function> make_extension_function(VirtualMachine *vm,
                                             TValue<String> name,
                                             clover_extension_fn_7 function,
                                             Optional<TValue<String>> docstring)
    {
        NativeFunctionTarget target;
        target.extension7 = function;
        return make_native_function_with_target(
            vm, name, target, Bytecode::CallExtension7, 7, docstring, true);
    }

    void
    install_builtin_intrinsic_methods(VirtualMachine *vm, ClassObject *cls,
                                      const BuiltinIntrinsicMethod *methods,
                                      uint32_t method_count)
    {
        DescriptorFlags method_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        ShapeFlags class_shape_flags = cls->get_shape()->flags();
        cls->set_shape(cls->get_shape()->clone_with_flags(
            class_shape_flags & ~fixed_attribute_shape_flags()));
        for(uint32_t method_idx = 0; method_idx < method_count; ++method_idx)
        {
            const BuiltinIntrinsicMethod &method = methods[method_idx];
            bool stored = cls->define_own_property(
                vm->get_or_create_interned_string_value(method.name),
                make_intrinsic_function(vm, method).raw_value(), method_flags);
            assert(stored);
            (void)stored;
        }
        cls->set_shape(cls->get_shape()->clone_with_flags(class_shape_flags));
    }

}  // namespace cl
