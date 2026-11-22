#include "builtin_types/property.h"
#include "builtin_types/module_object.h"
#include "builtin_types/tuple.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"

namespace cl
{
    static Expected<TValue<Property>> property_receiver(Value self)
    {
        return TValue<Property>::from_value_or_raise(
            self, L"TypeError",
            L"property method requires a property receiver");
    }

    [[nodiscard]] Value allocate_property(ThreadState *thread, Value cls_value)
    {
        auto *cls = try_native_cast<ClassObject>(cls_value);
        if(cls == nullptr ||
           cls->instance_native_layout_id() != NativeLayoutId::Property)
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"property.__new__ expects a property subclass");
        }
        return thread->make_internal_value<Property>(cls).raw_value();
    }

    [[nodiscard]] static Expected<void> write_property_doc(Property *property,
                                                           Value doc)
    {
        if(!CL_TRY(
               property->set_own_property(interned_string(L"__doc__"), doc)))
        {
            return Expected<void>::raise_exception(
                L"SystemError", L"could not write property __doc__ slot");
        }
        return Expected<void>::ok();
    }

    [[nodiscard]] static Value reset_accessors(ThreadState *, Value self,
                                               Value fget, Value fset,
                                               Value fdel)
    {
        Property *property = CL_TRY(property_receiver(self)).extract();
        property->fget = fget;
        property->fset = fset;
        property->fdel = fdel;
        CL_TRY(write_property_doc(property, Value::None()));
        property->doc_from_getter = Value::False();
        return Value::None();
    }

    [[nodiscard]] static Value set_doc(ThreadState *, Value self, Value doc,
                                       Value from_getter)
    {
        Property *property = CL_TRY(property_receiver(self)).extract();
        CL_TRY(write_property_doc(property, doc));
        property->doc_from_getter = from_getter;
        return Value::None();
    }

    template <Member<Value> Property::*Accessor>
    [[nodiscard]] static Value read_accessor(ThreadState *, Value self)
    {
        Property *property = CL_TRY(property_receiver(self)).extract();
        return (property->*Accessor).raw_value();
    }

    [[nodiscard]] static Value copy_state(ThreadState *thread, Value self)
    {
        auto property = CL_TRY(property_receiver(self));
        auto result = thread->make_object_value<Tuple>(5);
        Property *object = property.extract();
        result.extract()->initialize_item_unchecked(
            0, Value::from_oop(object->get_shape()->get_class()));
        result.extract()->initialize_item_unchecked(1,
                                                    object->fget.raw_value());
        result.extract()->initialize_item_unchecked(2,
                                                    object->fset.raw_value());
        result.extract()->initialize_item_unchecked(3,
                                                    object->fdel.raw_value());
        Value doc = object->doc.raw_value();
        if(doc.is_not_present() ||
           object->doc_from_getter.raw_value() == Value::True())
        {
            doc = Value::None();
        }
        result.extract()->initialize_item_unchecked(4, doc);
        return result.raw_value();
    }

    BuiltinClassDefinition make_property_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId layouts[] = {NativeLayoutId::Property};
        ClassObject *cls = ClassObject::make_builtin_class<Property>(
            vm->get_or_create_interned_string_value(L"property"),
            Property::kPredefinedInlineSlotCount, nullptr, 0,
            vm->object_class(), SubclassPolicy::Allowed);
        constexpr DescriptorFlags accessor_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        BuiltinInstanceShapeBuilder(cls,
                                    BuiltinInstanceShapeDefaults::DunderClass,
                                    Property::kPredefinedInlineSlotCount)
            .add_slot(interned_string(L"fget"), Property::kGetSlot,
                      accessor_flags)
            .add_slot(interned_string(L"fset"), Property::kSetSlot,
                      accessor_flags)
            .add_slot(interned_string(L"fdel"), Property::kDeleteSlot,
                      accessor_flags)
            .add_slot(L"__doc__", Property::kDocSlot)
            .reserve_slot(Property::kDocFromGetterSlot)
            .install(shape_flag(ShapeFlag::DisallowAttributeAddDelete));
        return builtin_class_definition(cls, layouts,
                                        BuiltinsVisibility::Public);
    }

    void install_property_helpers(VirtualMachine *vm, ModuleObject *module)
    {
        const BuiltinIntrinsicMethod helpers[] = {
            builtin_intrinsic_method(L"_clover_property_allocate",
                                     allocate_property),
            builtin_intrinsic_method(L"_clover_property_reset",
                                     reset_accessors),
            builtin_intrinsic_method(L"_clover_property_set_doc", set_doc),
            builtin_intrinsic_method(L"_clover_property_getter_slot",
                                     read_accessor<&Property::fget>),
            builtin_intrinsic_method(L"_clover_property_setter_slot",
                                     read_accessor<&Property::fset>),
            builtin_intrinsic_method(L"_clover_property_deleter_slot",
                                     read_accessor<&Property::fdel>),
            builtin_intrinsic_method(L"_clover_property_copy_state",
                                     copy_state),
        };
        for(const BuiltinIntrinsicMethod &helper: helpers)
        {
            auto function = unwrap_bootstrap_expected(
                vm, make_intrinsic_function(vm, helper),
                "creating a property helper");
            unwrap_bootstrap_expected(
                vm,
                module->set_own_property(interned_string(helper.name),
                                         function.raw_value()),
                "installing a property helper");
        }
    }

    void install_property_python_methods(VirtualMachine *vm,
                                         ModuleObject *module)
    {
        constexpr std::pair<const wchar_t *, const wchar_t *> methods[] = {
            {L"__new__", L"_clover_property_new"},
            {L"__init__", L"_clover_property_init"},
            {L"__get__", L"_clover_property_get"},
            {L"__set__", L"_clover_property_set"},
            {L"__delete__", L"_clover_property_delete"},
            {L"getter", L"_clover_property_getter"},
            {L"setter", L"_clover_property_setter"},
            {L"deleter", L"_clover_property_deleter"},
        };
        ClassObject *cls =
            vm->class_for_native_layout(NativeLayoutId::Property);
        constexpr DescriptorFlags flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        for(const auto &[name, helper]: methods)
        {
            Value function = module->get_own_property(interned_string(helper));
            if(!has_native_layout<Function>(function) ||
               !cls->define_own_property(interned_string(name), function,
                                         flags))
            {
                fatal("failed to install a property method");
            }
        }
    }
}  // namespace cl
