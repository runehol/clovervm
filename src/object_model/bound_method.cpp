#include "object_model/bound_method.h"

#include "builtin_types/module_object.h"
#include "builtin_types/string_builder.h"
#include "object_model/attr.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "object_model/native_function.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <iterator>
#include <utility>

namespace cl
{
    TValue<BoundMethod> make_bound_method(ThreadState *thread, Value callable,
                                          Value receiver)
    {
        return thread->make_object_value<BoundMethod>(callable, receiver);
    }

    [[nodiscard]] static Value native_method_new(ThreadState *thread, Value cls,
                                                 Value callable, Value receiver)
    {
        if(cls != Value::from_oop(thread->class_for_native_layout(
                      NativeLayoutId::BoundMethod)))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"method.__new__ expects method as cls");
        }
        if(!is_callable(callable))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"first argument must be callable");
        }
        if(receiver.is_none())
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"instance must not be None");
        }
        return make_bound_method(thread, callable, receiver).raw_value();
    }

    [[nodiscard]] static Value native_method_repr(ThreadState *, Value self)
    {
        auto method = CL_TRY(TValue<BoundMethod>::from_value_or_raise(
            self, L"TypeError", L"method.__repr__ expects a method receiver"));
        StringBuilder builder;
        builder.append_c_str(L"<bound method ");
        if(auto *function =
               try_native_cast<Function>(method.extract()->callable()))
        {
            builder.append_string(
                function->code_object.extract()->name.value());
        }
        else
        {
            builder.append_char(L'?');
        }
        builder.append_c_str(L" of ");
        CL_PROPAGATE_EXCEPTION(
            builder.append_repr(method.extract()->receiver()));
        builder.append_char(L'>');
        return builder.finish();
    }

    BuiltinClassDefinition make_bound_method_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId layouts[] = {
            NativeLayoutId::BoundMethod};
        auto *cls = ClassObject::make_builtin_class<BoundMethod>(
            vm->get_or_create_interned_string_value(L"method"),
            BoundMethod::kInlineSlotCount, nullptr, 0, vm->object_class(),
            SubclassPolicy::Disallowed, immutable_shape_flags());
        constexpr DescriptorFlags flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        BuiltinInstanceShapeBuilder(cls,
                                    BuiltinInstanceShapeDefaults::DunderClass,
                                    BoundMethod::kInlineSlotCount)
            .add_slot(interned_string(L"__func__"), BoundMethod::kCallableSlot,
                      flags)
            .add_slot(interned_string(L"__self__"), BoundMethod::kReceiverSlot,
                      flags)
            .install(immutable_shape_flags());
        return builtin_class_definition(cls, layouts,
                                        BuiltinsVisibility::Internal);
    }

    void install_bound_method_class_methods(VirtualMachine *vm,
                                            ModuleObject *implementation_module)
    {
        auto *cls = vm->class_for_native_layout(NativeLayoutId::BoundMethod);
        const BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__new__", native_method_new,
                                     L"Create a bound method."),
            builtin_intrinsic_method(L"__repr__", native_method_repr,
                                     L"Return repr(self)."),
        };
        unwrap_bootstrap_expected(vm,
                                  install_builtin_intrinsic_methods(
                                      vm, cls, methods, std::size(methods)),
                                  "installing method intrinsics");

        const std::pair<const wchar_t *, const wchar_t *> python_methods[] = {
            {L"__call__", L"_clover_method_call"},
            {L"__eq__", L"_clover_method_eq"},
        };
        constexpr DescriptorFlags flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        for(const auto &[name, helper]: python_methods)
        {
            Value function = implementation_module->get_own_property(
                interned_string(helper));
            if(!has_native_layout<Function>(function) ||
               !cls->define_own_property(interned_string(name), function,
                                         flags))
            {
                fatal("failed to install a method type hook");
            }
        }
    }
}  // namespace cl
