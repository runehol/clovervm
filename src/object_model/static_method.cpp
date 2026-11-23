#include "object_model/static_method.h"

#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <iterator>

namespace cl
{
    [[nodiscard]] static Value static_method_new(ThreadState *thread, Value cls,
                                                 Value wrapped)
    {
        if(cls != Value::from_oop(thread->class_for_native_layout(
                      NativeLayoutId::StaticMethod)))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError",
                L"staticmethod.__new__ expects staticmethod as cls");
        }
        return thread->make_object_value<StaticMethod>(wrapped).raw_value();
    }

    [[nodiscard]] static Value
    static_method_get(ThreadState *thread, Value self, Value obj, Value owner)
    {
        auto method = CL_TRY(TValue<StaticMethod>::from_value_or_raise(
            self, L"TypeError",
            L"staticmethod.__get__ expects a staticmethod receiver"));
        if(obj.is_none() && owner.is_none())
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"__get__(None, None) is invalid");
        }
        return method.extract()->wrapped();
    }

    BuiltinClassDefinition make_static_method_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId layouts[] = {
            NativeLayoutId::StaticMethod};
        auto *cls = ClassObject::make_builtin_class<StaticMethod>(
            vm->get_or_create_interned_string_value(L"staticmethod"),
            StaticMethod::kInlineSlotCount, nullptr, 0, vm->object_class(),
            SubclassPolicy::Disallowed, immutable_shape_flags());
        constexpr DescriptorFlags flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        BuiltinInstanceShapeBuilder(cls,
                                    BuiltinInstanceShapeDefaults::DunderClass,
                                    StaticMethod::kInlineSlotCount)
            .add_slot(interned_string(L"__func__"), StaticMethod::kWrappedSlot,
                      flags)
            .install(immutable_shape_flags());
        return builtin_class_definition(cls, layouts,
                                        BuiltinsVisibility::Public);
    }

    void install_static_method_class_methods(VirtualMachine *vm)
    {
        Owned<TValue<Tuple>> defaults(
            active_thread()->make_object_value<Tuple>(1));
        defaults.extract()->initialize_item_unchecked(0, Value::None());
        const BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__new__", static_method_new,
                                     L"Create a static method descriptor."),
            with_defaults(
                builtin_intrinsic_method(L"__get__", static_method_get,
                                         L"Return the wrapped value."),
                defaults.value()),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(
                vm, vm->class_for_native_layout(NativeLayoutId::StaticMethod),
                methods, std::size(methods)),
            "installing staticmethod intrinsics");
    }
}  // namespace cl
