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
        Optional<TValue<String>> docstring,
        Optional<TValue<Tuple>> default_parameters =
            Optional<TValue<Tuple>>::none())
    {
        CodeObjectBuilder builder(vm, nullptr, vm->global_builtins_module(),
                                  nullptr, name);
        builder.n_parameters() = n_parameters;
        builder.n_positional_parameters() = n_parameters;
        uint32_t target_idx = builder.add_native_function_target(target);
        builder.emit_call_native(0, call_opcode, uint8_t(target_idx));
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

    static TValue<Function> make_native_function_with_target(
        VirtualMachine *vm, NativeFunctionTarget target, Bytecode call_opcode,
        uint32_t n_parameters,
        Optional<TValue<Tuple>> default_parameters =
            Optional<TValue<Tuple>>::none())
    {
        return make_native_function_with_target(
            vm, vm->get_or_create_interned_string_value(L"<native>"), target,
            call_opcode, n_parameters, Optional<TValue<String>>::none(),
            default_parameters);
    }

    static Bytecode call_native_opcode_for_arity(uint32_t n_parameters)
    {
        switch(n_parameters)
        {
            case 0:
                return Bytecode::CallNative0;
            case 1:
                return Bytecode::CallNative1;
            case 2:
                return Bytecode::CallNative2;
            case 3:
                return Bytecode::CallNative3;
            default:
                assert(false && "unsupported native function arity");
                return Bytecode::CallNative0;
        }
    }

    BuiltinNativeMethod builtin_native_method(const wchar_t *name,
                                              NativeFunction0 function,
                                              const wchar_t *doc)
    {
        NativeFunctionTarget target;
        target.fixed0 = function;
        return BuiltinNativeMethod{name, target, 0, doc};
    }

    BuiltinNativeMethod builtin_native_method(const wchar_t *name,
                                              NativeFunction1 function,
                                              const wchar_t *doc)
    {
        NativeFunctionTarget target;
        target.fixed1 = function;
        return BuiltinNativeMethod{name, target, 1, doc};
    }

    BuiltinNativeMethod builtin_native_method(const wchar_t *name,
                                              NativeFunction2 function,
                                              const wchar_t *doc)
    {
        NativeFunctionTarget target;
        target.fixed2 = function;
        return BuiltinNativeMethod{name, target, 2, doc};
    }

    BuiltinNativeMethod builtin_native_method(const wchar_t *name,
                                              NativeFunction3 function,
                                              const wchar_t *doc)
    {
        NativeFunctionTarget target;
        target.fixed3 = function;
        return BuiltinNativeMethod{name, target, 3, doc};
    }

    TValue<Function>
    make_native_function(VirtualMachine *vm, NativeFunction0 function,
                         Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed0 = function;
        return make_native_function_with_target(
            vm, target, Bytecode::CallNative0, 0, default_parameters);
    }

    TValue<Function>
    make_native_function(VirtualMachine *vm, NativeFunction1 function,
                         Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed1 = function;
        return make_native_function_with_target(
            vm, target, Bytecode::CallNative1, 1, default_parameters);
    }

    TValue<Function>
    make_native_function(VirtualMachine *vm, NativeFunction2 function,
                         Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed2 = function;
        return make_native_function_with_target(
            vm, target, Bytecode::CallNative2, 2, default_parameters);
    }

    TValue<Function>
    make_native_function(VirtualMachine *vm, NativeFunction3 function,
                         Optional<TValue<Tuple>> default_parameters)
    {
        NativeFunctionTarget target;
        target.fixed3 = function;
        return make_native_function_with_target(
            vm, target, Bytecode::CallNative3, 3, default_parameters);
    }

    TValue<Function> make_native_function(VirtualMachine *vm,
                                          const BuiltinNativeMethod &method)
    {
        Optional<TValue<String>> docstring =
            method.doc == nullptr
                ? Optional<TValue<String>>::none()
                : Optional<TValue<String>>::some(
                      vm->get_or_create_interned_string_value(method.doc));
        return make_native_function_with_target(
            vm, vm->get_or_create_interned_string_value(method.name),
            method.target, call_native_opcode_for_arity(method.n_parameters),
            method.n_parameters, docstring);
    }

    void install_builtin_native_methods(VirtualMachine *vm, ClassObject *cls,
                                        const BuiltinNativeMethod *methods,
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
            const BuiltinNativeMethod &method = methods[method_idx];
            bool stored = cls->define_own_property(
                vm->get_or_create_interned_string_value(method.name),
                make_native_function(vm, method).raw_value(), method_flags);
            assert(stored);
            (void)stored;
        }
        cls->set_shape(cls->get_shape()->clone_with_flags(class_shape_flags));
    }

}  // namespace cl
