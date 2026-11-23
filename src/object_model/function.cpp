#include "object_model/function.h"
#include "builtin_types/str.h"
#include "object_model/attribute_descriptor.h"
#include "object_model/bound_method.h"
#include "object_model/cell.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "object_model/shape.h"
#include "runtime/fatal.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <iterator>

namespace cl
{
    void Function::validate_closure(uint32_t n_freevars,
                                    Optional<TValue<Tuple>> closure)
    {
        if(n_freevars == 0 || !closure.has_value() ||
           closure.value().extract()->size() != n_freevars)
        {
            fatal("Function closure does not match the free-variable count");
        }
        const Tuple *cells = closure.value().extract();
        for(size_t index = 0; index < cells->size(); ++index)
        {
            if(!has_native_layout<Cell>(cells->item_unchecked(index)))
            {
                fatal("Function closure contains a non-cell value");
            }
        }
    }

    static void install_function_instance_root_shape(ClassObject *cls)
    {
        BuiltinInstanceShapeBuilder(
            cls, BuiltinInstanceShapeDefaults::DunderClassAndDict,
            Function::kInlineSlotCount)
            .add_slot(interned_string(L"__code__"), Function::kCodeObjectSlot,
                      descriptor_flag(DescriptorFlag::ReadOnly) |
                          descriptor_flag(DescriptorFlag::StableSlot))
            .reserve_slot(Function::kDefaultParametersSlot)
            .add_slot(L"__doc__", Function::kDocstringSlot)
            .reserve_slot(Function::kClosureSlot)
            .install(shape_flag(ShapeFlag::DisallowAttributeAddDelete));
    }

    BuiltinClassDefinition make_function_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Function};
        ClassObject *cls = ClassObject::make_builtin_class<Function>(
            vm->get_or_create_interned_string_value(L"function"),
            Function::kInlineSlotCount, nullptr, 0, vm->object_class(),
            SubclassPolicy::Disallowed);
        install_function_instance_root_shape(cls);
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Internal);
    }

    [[nodiscard]] static Value function_get(ThreadState *thread, Value self,
                                            Value obj, Value owner)
    {
        if(!has_native_layout<Function>(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"function.__get__ expects a function receiver");
        }
        if(!obj.is_none())
        {
            return make_bound_method(thread, self, obj).raw_value();
        }
        if(owner.is_none())
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"__get__(None, None) is invalid");
        }
        return self;
    }

    void install_function_class_methods(VirtualMachine *vm)
    {
        Owned<TValue<Tuple>> defaults(
            active_thread()->make_object_value<Tuple>(1));
        defaults.extract()->initialize_item_unchecked(0, Value::None());
        const BuiltinIntrinsicMethod getter = with_defaults(
            builtin_intrinsic_method(L"__get__", function_get,
                                     L"Return a bound method for an instance."),
            defaults.value());
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(
                vm, vm->class_for_native_layout(NativeLayoutId::Function),
                &getter, 1),
            "installing function descriptor getter");
    }
}  // namespace cl
