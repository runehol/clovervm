#include "object_model/native_function.h"

#include "api/extension_handle.h"
#include "builtin_types/module_object.h"
#include "builtin_types/tuple.h"
#include "bytecode/code_object_builder.h"
#include "object_model/attribute_descriptor.h"
#include "object_model/class_object.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <cassert>

namespace cl
{
    enum class NativeFunctionKind
    {
        Intrinsic,
        Extension,
    };

    static bool has_varargs(NativeFunctionParameterMode parameter_mode)
    {
        return parameter_mode == NativeFunctionParameterMode::VarArgs;
    }

    static Expected<TValue<Function>> make_native_function_with_target(
        VirtualMachine *vm, TValue<String> name, NativeFunctionTarget target,
        Bytecode call_opcode, uint32_t n_parameters,
        Optional<TValue<String>> docstring, NativeFunctionKind function_kind,
        NativeFunctionParameterMode parameter_mode,
        Optional<TValue<Tuple>> default_parameters =
            Optional<TValue<Tuple>>::none(),
        TrustedHandlerResolver trusted_handler_resolver = nullptr)
    {
        CodeObjectBuilder builder(vm, nullptr, vm->global_builtins_module(),
                                  nullptr, name);
        builder.n_parameters() = n_parameters;
        builder.n_positional_parameters() =
            has_varargs(parameter_mode) ? n_parameters - 1 : n_parameters;
        builder.function_signature().n_pos_or_kw_parameters =
            builder.n_positional_parameters();
        if(has_varargs(parameter_mode))
        {
            builder.parameter_flags() |= FunctionParameterFlags::HasVarArgs;
        }
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
        uint32_t target_idx =
            CL_TRY(builder.add_native_function_target(target));
        if(function_kind == NativeFunctionKind::Extension)
        {
            if constexpr(native_handle_detail::cl_indirect_handles)
            {
                // Reserve ordinary frame cells for the extension's initial
                // handle chunk. TemporaryReg records them in the generated
                // thunk's frame layout; it does not allocate per invocation.
                CodeObjectBuilder::TemporaryReg reserved_handle_cells(
                    builder, native_handle_detail::frame_handle_cell_count);
                CL_TRY(builder.emit_call_extension(0, call_opcode,
                                                   uint8_t(target_idx)));
            }
            else
            {
                CL_TRY(builder.emit_call_extension(0, call_opcode,
                                                   uint8_t(target_idx)));
            }
        }
        else
        {
            CL_TRY(builder.emit_call_intrinsic(0, call_opcode,
                                               uint8_t(target_idx)));
        }
        CL_TRY(builder.emit_return_or_raise_exception(0));
        TValue<CodeObject> code =
            TValue<CodeObject>::from_oop(CL_TRY(builder.finalize()));
        code.extract()->trusted_handler_resolver = trusted_handler_resolver;
        if(default_parameters.has_value())
        {
            return Expected<TValue<Function>>::ok(
                vm->make_immortal_object_value<Function>(code, docstring,
                                                         default_parameters));
        }
        return Expected<TValue<Function>>::ok(
            vm->make_immortal_object_value<Function>(code, docstring));
    }

    static Expected<TValue<Function>> make_intrinsic_function_with_target(
        VirtualMachine *vm, NativeFunctionTarget target, Bytecode call_opcode,
        uint32_t n_parameters,
        Optional<TValue<Tuple>> default_parameters =
            Optional<TValue<Tuple>>::none(),
        TrustedHandlerResolver trusted_handler_resolver = nullptr)
    {
        return make_native_function_with_target(
            vm, vm->get_or_create_interned_string_value(L"<native>"), target,
            call_opcode, n_parameters, Optional<TValue<String>>::none(),
            NativeFunctionKind::Intrinsic,
            NativeFunctionParameterMode::FixedArity, default_parameters,
            trusted_handler_resolver);
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
        return BuiltinIntrinsicMethod{name, target, 0, doc,
                                      Optional<TValue<Tuple>>::none()};
    }

    BuiltinIntrinsicMethod builtin_intrinsic_method(const wchar_t *name,
                                                    IntrinsicFunction1 function,
                                                    const wchar_t *doc)
    {
        NativeFunctionTarget target;
        target.fixed1 = function;
        return BuiltinIntrinsicMethod{name, target, 1, doc,
                                      Optional<TValue<Tuple>>::none()};
    }

    BuiltinIntrinsicMethod builtin_intrinsic_method(const wchar_t *name,
                                                    IntrinsicFunction2 function,
                                                    const wchar_t *doc)
    {
        NativeFunctionTarget target;
        target.fixed2 = function;
        return BuiltinIntrinsicMethod{name, target, 2, doc,
                                      Optional<TValue<Tuple>>::none()};
    }

    BuiltinIntrinsicMethod builtin_intrinsic_method(const wchar_t *name,
                                                    IntrinsicFunction3 function,
                                                    const wchar_t *doc)
    {
        NativeFunctionTarget target;
        target.fixed3 = function;
        return BuiltinIntrinsicMethod{name, target, 3, doc,
                                      Optional<TValue<Tuple>>::none()};
    }

    BuiltinIntrinsicMethod builtin_intrinsic_method(const wchar_t *name,
                                                    IntrinsicFunction4 function,
                                                    const wchar_t *doc)
    {
        NativeFunctionTarget target;
        target.fixed4 = function;
        return BuiltinIntrinsicMethod{name, target, 4, doc,
                                      Optional<TValue<Tuple>>::none()};
    }

    BuiltinIntrinsicMethod builtin_intrinsic_method(const wchar_t *name,
                                                    IntrinsicFunction5 function,
                                                    const wchar_t *doc)
    {
        NativeFunctionTarget target;
        target.fixed5 = function;
        return BuiltinIntrinsicMethod{name, target, 5, doc,
                                      Optional<TValue<Tuple>>::none()};
    }

    BuiltinIntrinsicMethod builtin_intrinsic_method(const wchar_t *name,
                                                    IntrinsicFunction6 function,
                                                    const wchar_t *doc)
    {
        NativeFunctionTarget target;
        target.fixed6 = function;
        return BuiltinIntrinsicMethod{name, target, 6, doc,
                                      Optional<TValue<Tuple>>::none()};
    }

    BuiltinIntrinsicMethod builtin_intrinsic_method(const wchar_t *name,
                                                    IntrinsicFunction7 function,
                                                    const wchar_t *doc)
    {
        NativeFunctionTarget target;
        target.fixed7 = function;
        return BuiltinIntrinsicMethod{name, target, 7, doc,
                                      Optional<TValue<Tuple>>::none()};
    }

    BuiltinIntrinsicMethod with_defaults(BuiltinIntrinsicMethod method,
                                         TValue<Tuple> defaults)
    {
        method.default_parameters = Optional<TValue<Tuple>>::some(defaults);
        return method;
    }

    BuiltinIntrinsicMethod with_varargs(BuiltinIntrinsicMethod method)
    {
        method.parameter_mode = NativeFunctionParameterMode::VarArgs;
        return method;
    }

    BuiltinIntrinsicMethod
    with_trusted_handler_resolver(BuiltinIntrinsicMethod method,
                                  TrustedHandlerResolver resolver)
    {
        method.trusted_handler_resolver = resolver;
        return method;
    }

    Expected<TValue<Function>>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction0 function,
                            Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed0 = function;
        return make_intrinsic_function_with_target(
            vm, target, Bytecode::CallIntrinsic0, 0, default_parameters);
    }

    Expected<TValue<Function>>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction1 function,
                            Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed1 = function;
        return make_intrinsic_function_with_target(
            vm, target, Bytecode::CallIntrinsic1, 1, default_parameters);
    }

    Expected<TValue<Function>>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction2 function,
                            Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed2 = function;
        return make_intrinsic_function_with_target(
            vm, target, Bytecode::CallIntrinsic2, 2, default_parameters);
    }

    Expected<TValue<Function>>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction3 function,
                            Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed3 = function;
        return make_intrinsic_function_with_target(
            vm, target, Bytecode::CallIntrinsic3, 3, default_parameters);
    }

    Expected<TValue<Function>>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction4 function,
                            Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed4 = function;
        return make_intrinsic_function_with_target(
            vm, target, Bytecode::CallIntrinsic4, 4, default_parameters);
    }

    Expected<TValue<Function>>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction5 function,
                            Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed5 = function;
        return make_intrinsic_function_with_target(
            vm, target, Bytecode::CallIntrinsic5, 5, default_parameters);
    }

    Expected<TValue<Function>>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction6 function,
                            Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed6 = function;
        return make_intrinsic_function_with_target(
            vm, target, Bytecode::CallIntrinsic6, 6, default_parameters);
    }

    Expected<TValue<Function>>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction7 function,
                            Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed7 = function;
        return make_intrinsic_function_with_target(
            vm, target, Bytecode::CallIntrinsic7, 7, default_parameters);
    }

    Expected<TValue<Function>>
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
            method.n_parameters, docstring, NativeFunctionKind::Intrinsic,
            method.parameter_mode, method.default_parameters,
            method.trusted_handler_resolver);
    }

    Expected<TValue<Function>>
    make_extension_function(VirtualMachine *vm, TValue<String> name,
                            clover_extension_fn_0 function,
                            Optional<TValue<String>> docstring)
    {
        NativeFunctionTarget target;
        target.extension0 = function;
        return make_native_function_with_target(
            vm, name, target, Bytecode::CallExtension0, 0, docstring,
            NativeFunctionKind::Extension,
            NativeFunctionParameterMode::FixedArity);
    }

    Expected<TValue<Function>>
    make_extension_function(VirtualMachine *vm, TValue<String> name,
                            clover_extension_fn_1 function,
                            Optional<TValue<String>> docstring)
    {
        NativeFunctionTarget target;
        target.extension1 = function;
        return make_native_function_with_target(
            vm, name, target, Bytecode::CallExtension1, 1, docstring,
            NativeFunctionKind::Extension,
            NativeFunctionParameterMode::FixedArity);
    }

    Expected<TValue<Function>>
    make_extension_function(VirtualMachine *vm, TValue<String> name,
                            clover_extension_fn_2 function,
                            Optional<TValue<String>> docstring)
    {
        NativeFunctionTarget target;
        target.extension2 = function;
        return make_native_function_with_target(
            vm, name, target, Bytecode::CallExtension2, 2, docstring,
            NativeFunctionKind::Extension,
            NativeFunctionParameterMode::FixedArity);
    }

    Expected<TValue<Function>>
    make_extension_function(VirtualMachine *vm, TValue<String> name,
                            clover_extension_fn_3 function,
                            Optional<TValue<String>> docstring)
    {
        NativeFunctionTarget target;
        target.extension3 = function;
        return make_native_function_with_target(
            vm, name, target, Bytecode::CallExtension3, 3, docstring,
            NativeFunctionKind::Extension,
            NativeFunctionParameterMode::FixedArity);
    }

    Expected<TValue<Function>>
    make_extension_function(VirtualMachine *vm, TValue<String> name,
                            clover_extension_fn_4 function,
                            Optional<TValue<String>> docstring)
    {
        NativeFunctionTarget target;
        target.extension4 = function;
        return make_native_function_with_target(
            vm, name, target, Bytecode::CallExtension4, 4, docstring,
            NativeFunctionKind::Extension,
            NativeFunctionParameterMode::FixedArity);
    }

    Expected<TValue<Function>>
    make_extension_function(VirtualMachine *vm, TValue<String> name,
                            clover_extension_fn_5 function,
                            Optional<TValue<String>> docstring)
    {
        NativeFunctionTarget target;
        target.extension5 = function;
        return make_native_function_with_target(
            vm, name, target, Bytecode::CallExtension5, 5, docstring,
            NativeFunctionKind::Extension,
            NativeFunctionParameterMode::FixedArity);
    }

    Expected<TValue<Function>>
    make_extension_function(VirtualMachine *vm, TValue<String> name,
                            clover_extension_fn_6 function,
                            Optional<TValue<String>> docstring)
    {
        NativeFunctionTarget target;
        target.extension6 = function;
        return make_native_function_with_target(
            vm, name, target, Bytecode::CallExtension6, 6, docstring,
            NativeFunctionKind::Extension,
            NativeFunctionParameterMode::FixedArity);
    }

    Expected<TValue<Function>>
    make_extension_function(VirtualMachine *vm, TValue<String> name,
                            clover_extension_fn_7 function,
                            Optional<TValue<String>> docstring)
    {
        NativeFunctionTarget target;
        target.extension7 = function;
        return make_native_function_with_target(
            vm, name, target, Bytecode::CallExtension7, 7, docstring,
            NativeFunctionKind::Extension,
            NativeFunctionParameterMode::FixedArity);
    }

    Expected<void>
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
            TValue<Function> function =
                CL_TRY(make_intrinsic_function(vm, method));
            bool stored = cls->define_own_property(
                vm->get_or_create_interned_string_value(method.name),
                function.raw_value(), method_flags);
            assert(stored);
            (void)stored;
        }
        cls->set_shape(cls->get_shape()->clone_with_flags(class_shape_flags));
        return Expected<void>::ok();
    }

}  // namespace cl
