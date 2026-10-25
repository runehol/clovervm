#include "builtin_types/bool.h"

#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "bytecode/code_object_builder.h"
#include "compiler/scope.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "object_model/native_function.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <cassert>
#include <cstdint>
#include <iterator>

namespace cl
{
    [[nodiscard]] static Value validate_bool_new_class(ThreadState *thread,
                                                       Value cls)
    {
        if(cls != Value::from_oop(thread->get_machine()->bool_class()))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"bool.__new__ expects the bool class");
        }
        return cls;
    }

    static Expected<TValue<Function>> make_bool_new_function(VirtualMachine *vm)
    {
        Scope *scope = vm->make_immortal_internal_raw<Scope>(nullptr);
        CodeObjectBuilder code(
            vm, nullptr, vm->global_builtins_module(), scope,
            vm->get_or_create_interned_string_value(L"bool.__new__"));
        code.configure_positional_function(2);  // cls, object
        code.function_signature().n_posonly_parameters = 2;
        code.function_signature().n_pos_or_kw_parameters = 0;
        code.function_signature().first_default_slot = 1;
        code.function_signature().default_presence_mask = 1;

        TValue<Function> validator =
            CL_TRY(make_intrinsic_function(vm, validate_bool_new_class));
        uint8_t validator_idx =
            CL_TRY(code.allocate_constant(validator.raw_value()));
        {
            CodeObjectBuilder::TemporaryReg callable(code);
            CL_TRY(code.emit_lda_constant(0, validator_idx));
            CL_TRY(code.emit_star(0, callable));
            {
                CodeObjectBuilder::TemporaryReg args(
                    code, 1, RegisterAlignment::CallFrame);
                CL_TRY(code.emit_mov(0, args, 0));
                // Reject invalid receivers before invoking truth callbacks.
                CL_TRY(code.emit_call_positional(0, callable, args, 1));
            }
            CL_TRY(code.emit_ldar(0, 1));
            // Use ordinary managed truth dispatch and its rooted continuation.
            CL_TRY(code.emit_to_bool(0));
            CL_TRY(code.emit_return(0));
        }
        TValue<Tuple> defaults = vm->make_immortal_object_value<Tuple>(1);
        defaults.extract()->initialize_item_unchecked(0, Value::False());
        return code.finalize_immortal_function(
            vm, Optional<TValue<String>>::none(),
            Optional<TValue<Tuple>>::some(defaults));
    }

    static Value native_bool_str(ThreadState *thread, Value self)
    {
        if(!self.is_bool())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"bool.__str__ expects a bool receiver");
        }
        return active_thread()
            ->make_object_value<String>(self == Value::True() ? L"True"
                                                              : L"False")
            .raw_value();
    }

    BuiltinClassDefinition make_bool_class(VirtualMachine *vm,
                                           ClassObject *int_class)
    {
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"bool"), 0, nullptr, 0,
            int_class, NativeLayoutId::Invalid, SubclassPolicy::Disallowed,
            immutable_shape_flags());
        return builtin_class_definition(cls, BuiltinsVisibility::Public);
    }

    void install_bool_class_methods(VirtualMachine *vm)
    {
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__str__", native_bool_str,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_bool_str,
                                     L"Return repr(self)."),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->bool_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");

        TValue<Function> new_function = unwrap_bootstrap_expected(
            vm, make_bool_new_function(vm), "creating bool.__new__");
        bool stored = vm->bool_class()->define_own_property(
            vm->get_or_create_interned_string_value(L"__new__"),
            new_function.raw_value(),
            descriptor_flag(DescriptorFlag::ReadOnly) |
                descriptor_flag(DescriptorFlag::StableSlot));
        assert(stored);
        (void)stored;
    }

}  // namespace cl
