#include "object_model/attr.h"

#include "builtin_types/module_object.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "object_model/attribute_descriptor.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "object_model/slot_dict.h"
#include "runtime/fatal.h"
#include "runtime/protocol_helper_functions.h"
#include "runtime/runtime_helpers.h"
#include "runtime/thread_state.h"

namespace cl
{
    class DescriptorProtocol
    {
    public:
        Value get;
        Value set;
        Value delete_;

        bool has_get() const { return !get.is_not_present(); }
        bool has_set_or_delete() const
        {
            return !set.is_not_present() || !delete_.is_not_present();
        }
    };

    static void invalidate_lookup_cells_for_class_target(Object *object)
    {
        Shape *shape = object->get_shape();
        if(shape != nullptr && shape->has_flag(ShapeFlag::IsClassObject))
        {
            native_cast<ClassObject>(object)
                ->invalidate_lookup_validity_cells_for_contents_change();
        }
    }

    static AttributeReadPlanKind
    attribute_read_plan_kind_for_path(AttributeReadPlanPath path, Value value)
    {
        if(path == AttributeReadPlanPath::InstanceClassChain &&
           has_native_layout<Function>(value))
        {
            // Class-chain method plans load the defining class slot before
            // binding the function to the current receiver.
            return AttributeReadPlanKind::BindFunctionReceiver;
        }

        // Ordinary lookup hits are cached as storage loads, including hits
        // on the metaclass chain.
        return AttributeReadPlanKind::ReceiverSlot;
    }

    static AttributeCacheBlockers
    attribute_cache_blockers_for_class_value(Value value)
    {
        if(!value.is_ptr())
        {
            return attribute_cache_blocker(AttributeCacheBlocker::None);
        }

        Object *object = value.get_ptr<Object>();
        Shape *type_shape = object->get_shape()->get_class()->get_shape();
        if(type_shape->has_flag(ShapeFlag::IsImmutable))
        {
            return attribute_cache_blocker(AttributeCacheBlocker::None);
        }

        return attribute_cache_blocker(
            AttributeCacheBlocker::MutableDescriptorType);
    }

    static AttributeReadDescriptor
    with_plan_kind(AttributeReadDescriptor descriptor,
                   AttributeReadPlanKind kind,
                   AttributeCacheBlocker blocker = AttributeCacheBlocker::None)
    {
        descriptor.plan.kind = kind;
        if(blocker != AttributeCacheBlocker::None)
        {
            descriptor.cache_blockers =
                attribute_cache_blockers(descriptor.cache_blockers, blocker);
        }
        return descriptor;
    }

    static AttributeReadDescriptor
    with_cache_blockers(AttributeReadDescriptor descriptor,
                        AttributeCacheBlockers blockers)
    {
        descriptor.cache_blockers |= blockers;
        return descriptor;
    }

    static AttributeReadDescriptor
    uncacheable_inline_receiver_descriptor(AttributeReadDescriptor descriptor)
    {
        descriptor.lookup_validity_cell = nullptr;
        descriptor.cache_blockers = attribute_cache_blockers(
            descriptor.cache_blockers, AttributeCacheBlocker::InlineReceiver);
        return descriptor;
    }

    static AttributeReadDescriptor shape_class_value_descriptor(Value receiver)
    {
        Value class_value = Value::from_oop(
            active_thread()->shape_of_value(receiver)->get_class());
        return AttributeReadDescriptor::found(
            AttributeReadPlan::constant(class_value), class_value);
    }

    static AttributeReadDescriptor special_read_descriptor(Value receiver,
                                                           DescriptorInfo info)
    {
        assert(info.has_flag(DescriptorFlag::SpecialRead));
        switch(info.special_kind())
        {
            case DescriptorSpecialKind::ShapeClass:
                return shape_class_value_descriptor(receiver);
            case DescriptorSpecialKind::SlotDict:
                {
                    assert(receiver.is_ptr());
                    Object *object = receiver.get_ptr<Object>();
                    Value slotdict =
                        make_object_value<SlotDict>(object).raw_value();
                    return AttributeReadDescriptor::found(
                        AttributeReadPlan::constant(slotdict), slotdict,
                        attribute_cache_blocker(
                            AttributeCacheBlocker::UnsupportedDescriptorKind));
                }
            case DescriptorSpecialKind::ModuleBuiltins:
            case DescriptorSpecialKind::None:
                break;
        }
        __builtin_unreachable();
    }

    static bool can_change_class(const Object *object)
    {
        NativeLayoutId layout = object->native_layout_id();
        return !object->get_shape()->has_flag(ShapeFlag::IsImmutable) &&
               (layout == NativeLayoutId::Instance ||
                layout == NativeLayoutId::ModuleObject);
    }

    static bool descriptor_type_has_stable_protocol(ClassObject *type)
    {
        // The receiver's validity cell cannot see changes in the descriptor
        // MRO.
        return type->get_shape()->has_flag(ShapeFlag::IsImmutable) &&
               type->get_or_create_mro_shape_and_contents_validity_cell()
                       ->dependency_mutability() ==
                   ValidityCellDependencyMutability::Immutable;
    }

    static Expected<bool> store_attr_change_class(Value receiver, Value value)
    {
        assert(receiver.is_ptr());
        Object *object = receiver.get_ptr<Object>();
        if(!can_change_class(object))
        {
            return Expected<bool>::raise_exception(
                L"TypeError",
                L"__class__ assignment only supported for mutable types or "
                L"ModuleType subclasses");
        }
        if(!has_native_layout<ClassObject>(value))
        {
            return Expected<bool>::raise_exception(
                L"TypeError", L"__class__ assignment requires a class object");
        }
        TValue<ClassObject> new_class =
            TValue<ClassObject>::from_value_assumed(value);
        if(new_class.extract()->instance_native_layout_id() !=
           object->native_layout_id())
        {
            return Expected<bool>::raise_exception(
                L"TypeError",
                L"__class__ assignment only supported for mutable types or "
                L"ModuleType subclasses");
        }

        object->set_shape(object->get_shape()->clone_with_class(new_class));
        return Expected<bool>::ok(true);
    }

    static Value
    class_chain_shape_class_receiver(const ClassObject *class_object,
                                     AttributeReadPlanPath path,
                                     AttributeBindingContext binding)
    {
        if(path == AttributeReadPlanPath::ClassObjectChain)
        {
            return Value::from_oop(const_cast<ClassObject *>(class_object));
        }
        return binding.self;
    }

    static AttributeReadDescriptor
    lookup_inline_shape_attribute_read_descriptor(Value receiver,
                                                  TValue<String> name)
    {
        DescriptorLookup lookup =
            active_thread()
                ->shape_of_value(receiver)
                ->lookup_descriptor_including_latent(name);
        if(!lookup.is_present())
        {
            return AttributeReadDescriptor::not_found();
        }
        if(lookup.info.has_flag(DescriptorFlag::SpecialRead))
        {
            return special_read_descriptor(receiver, lookup.info);
        }
        return AttributeReadDescriptor::not_found();
    }

    static AttributeCacheBlockers
    superseded_class_read_descriptor_cache_blockers(
        const AttributeReadDescriptor &descriptor)
    {
        if(!descriptor.is_found())
        {
            return attribute_cache_blocker(AttributeCacheBlocker::None);
        }

        return descriptor.cache_blockers |
               attribute_cache_blockers_for_class_value(
                   descriptor.lookup_value);
    }

    static AttributeReadDescriptor
    with_mro_shape_and_contents_validity_cell_if_unblocked(
        AttributeReadDescriptor descriptor, ClassObject *cls)
    {
        descriptor.lookup_validity_cell = nullptr;
        if(descriptor.is_found() &&
           attribute_cache_blockers_are_none(descriptor.cache_blockers) &&
           !descriptor.plan.is_descriptor_get())
        {
            descriptor.lookup_validity_cell =
                cls->get_or_create_mro_shape_and_contents_validity_cell();
        }
        return descriptor;
    }

    static AttributeReadDescriptor
    with_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell_if_unblocked(
        AttributeReadDescriptor descriptor, ClassObject *cls)
    {
        descriptor.lookup_validity_cell = nullptr;
        if(descriptor.is_found() &&
           attribute_cache_blockers_are_none(descriptor.cache_blockers) &&
           !descriptor.plan.is_descriptor_get())
        {
            descriptor.lookup_validity_cell =
                cls->get_or_create_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell();
        }
        return descriptor;
    }

    static AttributeWriteDescriptor
    with_mro_shape_and_contents_validity_cell_if_unblocked(
        AttributeWriteDescriptor descriptor, ClassObject *cls)
    {
        descriptor.lookup_validity_cell = nullptr;
        if(descriptor.is_found() &&
           attribute_cache_blockers_are_none(descriptor.cache_blockers))
        {
            descriptor.lookup_validity_cell =
                cls->get_or_create_mro_shape_and_contents_validity_cell();
        }
        return descriptor;
    }

    static AttributeDeleteDescriptor
    with_mro_shape_and_contents_validity_cell_if_unblocked(
        AttributeDeleteDescriptor descriptor, ClassObject *cls)
    {
        descriptor.lookup_validity_cell = nullptr;
        if(descriptor.is_found() &&
           attribute_cache_blockers_are_none(descriptor.cache_blockers))
        {
            descriptor.lookup_validity_cell =
                cls->get_or_create_mro_shape_and_contents_validity_cell();
        }
        return descriptor;
    }

    static AttributeReadDescriptor class_chain_read_descriptor_from_lookup(
        const ClassObject *class_object, ClassObject *storage_class,
        DescriptorLookup lookup, AttributeReadPlanPath path,
        AttributeBindingContext binding)
    {
        if(lookup.info.has_flag(DescriptorFlag::SpecialRead))
        {
            return special_read_descriptor(
                class_chain_shape_class_receiver(class_object, path, binding),
                lookup.info);
        }

        Value value =
            storage_class->read_storage_location(lookup.storage_location());
        return AttributeReadDescriptor::found(
            AttributeReadPlan::from_storage(
                path, attribute_read_plan_kind_for_path(path, value),
                storage_class, lookup.storage_location()),
            value);
    }

    static AttributeReadDescriptor lookup_class_chain_read_descriptor(
        const ClassObject *class_object, TValue<String> name,
        AttributeReadPlanPath path, AttributeBindingContext binding)
    {
        Value mro_value = class_object->get_mro_value();
        assert(has_native_layout<Tuple>(mro_value));
        Tuple *mro = native_cast<Tuple>(mro_value);
        for(uint32_t mro_idx = 0; mro_idx < mro->size(); ++mro_idx)
        {
            Value class_value = mro->item_unchecked(mro_idx);
            ClassObject *cls = try_native_cast<ClassObject>(class_value);
            if(cls == nullptr)
            {
                continue;
            }

            DescriptorLookup lookup =
                cls->get_shape()->lookup_present_descriptor(name);
            if(!lookup.is_present())
            {
                continue;
            }
            return class_chain_read_descriptor_from_lookup(
                class_object, cls, lookup, path, binding);
        }

        return AttributeReadDescriptor::not_found();
    }

    static bool find_class_in_mro(Tuple *mro, ClassObject *target_class,
                                  uint32_t &index)
    {
        for(uint32_t mro_idx = 0; mro_idx < mro->size(); ++mro_idx)
        {
            if(mro->item_unchecked(mro_idx) == Value::from_oop(target_class))
            {
                index = mro_idx;
                return true;
            }
        }
        return false;
    }

    static AttributeReadDescriptor
    lookup_class_chain_read_descriptor_until_mro_index(
        const ClassObject *class_object, Tuple *mro, uint32_t stop_mro_idx,
        TValue<String> name, AttributeReadPlanPath path,
        AttributeBindingContext binding)
    {
        assert(stop_mro_idx <= mro->size());
        for(uint32_t mro_idx = 0; mro_idx < stop_mro_idx; ++mro_idx)
        {
            Value class_value = mro->item_unchecked(mro_idx);
            ClassObject *cls = try_native_cast<ClassObject>(class_value);
            if(cls == nullptr)
            {
                continue;
            }

            DescriptorLookup lookup =
                cls->get_shape()->lookup_present_descriptor(name);
            if(!lookup.is_present())
            {
                continue;
            }
            return class_chain_read_descriptor_from_lookup(
                class_object, cls, lookup, path, binding);
        }

        return AttributeReadDescriptor::not_found();
    }

    static AttributeReadDescriptor lookup_instance_attribute_read_descriptor(
        const ClassObject *class_object, TValue<String> name, Value receiver)
    {
        return lookup_class_chain_read_descriptor(
            class_object, name, AttributeReadPlanPath::InstanceClassChain,
            AttributeBindingContext{receiver, class_object});
    }

    static AttributeReadDescriptor
    lookup_class_attribute_read_descriptor(const ClassObject *class_object,
                                           TValue<String> name)
    {
        return lookup_class_chain_read_descriptor(
            class_object, name, AttributeReadPlanPath::ClassObjectChain,
            AttributeBindingContext{Value::None(), class_object});
    }

    static AttributeReadDescriptor
    lookup_metaclass_attribute_read_descriptor(const ClassObject *metaclass,
                                               TValue<String> name,
                                               ClassObject *receiver_class)
    {
        return lookup_class_chain_read_descriptor(
            metaclass, name, AttributeReadPlanPath::MetaclassChain,
            AttributeBindingContext{Value::from_oop(receiver_class),
                                    metaclass});
    }

    static DescriptorProtocol lookup_descriptor_protocol(ClassObject *type)
    {
        TValue<String> get_name(interned_string(L"__get__"));
        TValue<String> set_name(interned_string(L"__set__"));
        TValue<String> delete_name(interned_string(L"__delete__"));

        return DescriptorProtocol{
            lookup_class_attribute_read_descriptor(type, get_name).lookup_value,
            lookup_class_attribute_read_descriptor(type, set_name).lookup_value,
            lookup_class_attribute_read_descriptor(type, delete_name)
                .lookup_value};
    }

    enum class DescriptorReadClassification : uint8_t
    {
        General,
        InstanceAttribute,
    };

    static AttributeReadDescriptor
    classify_class_read_descriptor(AttributeReadDescriptor descriptor,
                                   DescriptorReadClassification classification =
                                       DescriptorReadClassification::General)
    {
        if(!descriptor.is_found() ||
           descriptor.plan.kind == AttributeReadPlanKind::BindFunctionReceiver)
        {
            return descriptor;
        }

        // The function getter returns the function unchanged on class reads.
        if(descriptor.plan.path == AttributeReadPlanPath::ClassObjectChain &&
           has_native_layout<Function>(descriptor.lookup_value))
        {
            return descriptor;
        }

        ThreadState *thread = active_thread();
        ClassObject *type = thread->class_of_value(descriptor.lookup_value);
        DescriptorProtocol protocol = lookup_descriptor_protocol(type);
        if(!protocol.has_get())
        {
            return descriptor;
        }

        AttributeReadPlanKind kind =
            protocol.has_set_or_delete()
                ? AttributeReadPlanKind::DataDescriptorGet
                : AttributeReadPlanKind::NonDataDescriptorGet;
        if(classification == DescriptorReadClassification::InstanceAttribute &&
           kind == AttributeReadPlanKind::DataDescriptorGet &&
           type == thread->class_for_native_layout(NativeLayoutId::Property))
        {
            kind = AttributeReadPlanKind::ExactPropertyGet;
        }
        descriptor = with_plan_kind(
            descriptor, kind, AttributeCacheBlocker::UnsupportedDescriptorKind);
        descriptor.plan.constant_or_getter =
            has_native_layout<Function>(protocol.get) ? protocol.get
                                                      : Value::not_present();
        return descriptor;
    }

    static AttributeReadDescriptor
    finalize_instance_class_read(AttributeReadDescriptor descriptor,
                                 ClassObject *receiver_class)
    {
        if(descriptor.cache_blockers !=
               attribute_cache_blocker(
                   AttributeCacheBlocker::UnsupportedDescriptorKind) ||
           !descriptor.plan.is_descriptor_get() ||
           descriptor.plan.constant_or_getter.is_not_present())
        {
            return with_mro_shape_and_contents_validity_cell_if_unblocked(
                descriptor, receiver_class);
        }

        Value value = descriptor.lookup_value;
        if((value.is_ptr() && can_change_class(value.get_ptr<Object>())) ||
           !descriptor_type_has_stable_protocol(
               active_thread()->class_of_value(value)))
        {
            return descriptor;
        }

        descriptor.cache_blockers =
            attribute_cache_blocker(AttributeCacheBlocker::None);
        descriptor.lookup_validity_cell =
            receiver_class
                ->get_or_create_mro_shape_and_contents_validity_cell();
        return descriptor;
    }

    static AttributeCacheBlockers mutation_cache_blockers_for_class_value(
        const AttributeReadDescriptor &descriptor, ClassObject *type)
    {
        Value value = descriptor.lookup_value;
        if((value.is_ptr() && can_change_class(value.get_ptr<Object>())) ||
           !descriptor_type_has_stable_protocol(type))
        {
            return attribute_cache_blockers(
                descriptor.cache_blockers,
                AttributeCacheBlocker::MutableDescriptorType);
        }
        return descriptor.cache_blockers;
    }

    static AttributeReadDescriptor
    resolve_class_attr_read_descriptor(ClassObject *cls, TValue<String> name)
    {
        AttributeReadDescriptor class_descriptor =
            classify_class_read_descriptor(
                lookup_class_attribute_read_descriptor(cls, name));
        if(class_descriptor.is_found())
        {
            return with_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell_if_unblocked(
                class_descriptor, cls);
        }

        ClassObject *metaclass = cls->get_shape()->get_class();
        if(metaclass == cls)
        {
            return AttributeReadDescriptor::not_found();
        }

        AttributeReadDescriptor metaclass_descriptor =
            classify_class_read_descriptor(
                lookup_metaclass_attribute_read_descriptor(metaclass, name,
                                                           cls));
        return with_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell_if_unblocked(
            metaclass_descriptor, cls);
    }

    AttributeReadDescriptor resolve_attr_read_descriptor(Value obj,
                                                         TValue<String> name)
    {
        if(likely(obj.is_ptr()))
        {
            Object *object = obj.get_ptr<Object>();
            if(object->get_shape()->has_flag(ShapeFlag::IsClassObject))
            {
                assert(object->native_layout_id() ==
                       NativeLayoutId::ClassObject);
                return resolve_class_attr_read_descriptor(
                    static_cast<ClassObject *>(object), name);
            }

            ClassObject *class_object = object->get_shape()->get_class();
            AttributeReadDescriptor class_descriptor =
                classify_class_read_descriptor(
                    lookup_instance_attribute_read_descriptor(class_object,
                                                              name, obj),
                    DescriptorReadClassification::InstanceAttribute);
            if(class_descriptor.is_found() &&
               class_descriptor.plan.is_data_descriptor_get())
            {
                return finalize_instance_class_read(class_descriptor,
                                                    class_object);
            }

            DescriptorLookup own_lookup =
                object->get_shape()->lookup_descriptor_including_latent(name);
            if(own_lookup.is_present() &&
               own_lookup.info.has_flag(DescriptorFlag::SpecialRead))
            {
                return with_mro_shape_and_contents_validity_cell_if_unblocked(
                    with_cache_blockers(
                        special_read_descriptor(obj, own_lookup.info),
                        superseded_class_read_descriptor_cache_blockers(
                            class_descriptor)),
                    class_object);
            }

            AttributeReadDescriptor own_descriptor =
                object->lookup_own_attribute_descriptor(name);
            if(own_descriptor.is_found())
            {
                return with_mro_shape_and_contents_validity_cell_if_unblocked(
                    with_cache_blockers(
                        own_descriptor,
                        superseded_class_read_descriptor_cache_blockers(
                            class_descriptor)),
                    class_object);
            }

            if(class_descriptor.is_found() &&
               class_descriptor.plan.kind ==
                   AttributeReadPlanKind::ReceiverSlot)
            {
                // The value's type can gain __get__ without a class-slot write.
                class_descriptor.cache_blockers |=
                    attribute_cache_blockers_for_class_value(
                        class_descriptor.lookup_value);
            }

            // Replacing the class slot value can change how the read binds.
            return finalize_instance_class_read(class_descriptor, class_object);
        }

        AttributeReadDescriptor inline_shape_descriptor =
            lookup_inline_shape_attribute_read_descriptor(obj, name);
        if(inline_shape_descriptor.is_found())
        {
            return uncacheable_inline_receiver_descriptor(
                inline_shape_descriptor);
        }

        ClassObject *class_object = active_thread()->class_of_value(obj);
        return uncacheable_inline_receiver_descriptor(
            classify_class_read_descriptor(
                lookup_instance_attribute_read_descriptor(class_object, name,
                                                          obj)));
    }

    AttributeReadDescriptor
    resolve_attr_value_read_descriptor(Value obj, TValue<String> name)
    {
        AttributeReadDescriptor descriptor =
            resolve_attr_read_descriptor(obj, name);
        if(!descriptor.is_found() ||
           descriptor.plan.kind != AttributeReadPlanKind::BindFunctionReceiver)
        {
            return descriptor;
        }

        // A call target can carry the function and receiver separately.
        // A value read instead executes the function's descriptor getter.
        ClassObject *function_class =
            active_thread()->class_for_native_layout(NativeLayoutId::Function);
        descriptor.plan.kind = AttributeReadPlanKind::NonDataDescriptorGet;
        descriptor.plan.constant_or_getter =
            function_class->get_own_property(interned_string(L"__get__"));
        assert(has_native_layout<Function>(descriptor.plan.constant_or_getter));
        return descriptor;
    }

    AttributeReadDescriptor
    resolve_special_method_read_descriptor(Value obj, TValue<String> name)
    {
        ClassObject *class_object = active_thread()->class_of_value(obj);
        AttributeReadDescriptor descriptor = classify_class_read_descriptor(
            lookup_instance_attribute_read_descriptor(class_object, name, obj));
        if(!obj.is_ptr())
        {
            return uncacheable_inline_receiver_descriptor(descriptor);
        }
        return with_mro_shape_and_contents_validity_cell_if_unblocked(
            descriptor, class_object);
    }

    bool is_callable(Value obj)
    {
        if(has_native_layout<Function>(obj) ||
           has_native_layout<ClassObject>(obj))
        {
            return true;
        }
        return resolve_special_method_read_descriptor(
                   obj, interned_string(L"__call__"))
            .is_found();
    }

    AttributeReadDescriptor
    resolve_reflected_priority_special_method_read_descriptor(
        Value receiver, Value alternate, TValue<String> name)
    {
        ClassObject *receiver_class = active_thread()->class_of_value(receiver);
        ClassObject *alternate_class =
            active_thread()->class_of_value(alternate);
        if(receiver_class == alternate_class)
        {
            return AttributeReadDescriptor::not_found();
        }

        Value mro_value = receiver_class->get_mro_value();
        assert(has_native_layout<Tuple>(mro_value));
        Tuple *mro = native_cast<Tuple>(mro_value);
        uint32_t alternate_mro_idx = 0;
        if(!find_class_in_mro(mro, alternate_class, alternate_mro_idx))
        {
            return AttributeReadDescriptor::not_found();
        }

        AttributeReadDescriptor descriptor = classify_class_read_descriptor(
            lookup_class_chain_read_descriptor_until_mro_index(
                receiver_class, mro, alternate_mro_idx, name,
                AttributeReadPlanPath::InstanceClassChain,
                AttributeBindingContext{receiver, receiver_class}));
        if(!receiver.is_ptr())
        {
            return uncacheable_inline_receiver_descriptor(descriptor);
        }
        return with_mro_shape_and_contents_validity_cell_if_unblocked(
            descriptor, receiver_class);
    }

    Expected<DescriptorGetCall>
    prepare_descriptor_get_call(Value receiver,
                                const AttributeReadDescriptor &read)
    {
        assert(read.plan.is_descriptor_get());
        Value getter = read.plan.constant_or_getter;
        if(getter.is_not_present())
        {
            return Expected<DescriptorGetCall>::raise_exception(
                L"TypeError",
                L"descriptor __get__ requires interpreter dispatch");
        }

        auto function = TValue<Function>::from_value_assumed(getter);
        switch(read.plan.path)
        {
            case AttributeReadPlanPath::InstanceClassChain:
            case AttributeReadPlanPath::MetaclassChain:
                return Expected<DescriptorGetCall>::ok(
                    {function, read.lookup_value, receiver,
                     Value::from_oop(
                         active_thread()->class_of_value(receiver))});
            case AttributeReadPlanPath::ClassObjectChain:
                return Expected<DescriptorGetCall>::ok(
                    {function, read.lookup_value, Value::None(), receiver});
            case AttributeReadPlanPath::ReceiverOwnProperty:
                assert(false && "own slot cannot require descriptor dispatch");
                __builtin_unreachable();
        }
        __builtin_unreachable();
    }

    Value load_attr_from_plan(Value receiver, const AttributeReadPlan &plan)
    {
        if(plan.kind == AttributeReadPlanKind::ConstantValue)
        {
            return plan.constant_or_getter;
        }

        const Object *storage_owner = plan.storage_owner;
        if(storage_owner == nullptr)
        {
            assert(receiver.is_ptr());
            storage_owner = receiver.get_ptr<Object>();
        }
        if(plan.kind == AttributeReadPlanKind::ReceiverSlot ||
           plan.kind == AttributeReadPlanKind::BindFunctionReceiver)
        {
            return storage_owner->read_storage_location(plan.storage_location);
        }

        if(plan.is_descriptor_get())
        {
            fatal("descriptor __get__ requires interpreter dispatch");
        }

        __builtin_unreachable();
    }

    bool load_method_from_plan(Value receiver, const AttributeReadPlan &plan,
                               Value &callable_out, Value &self_out)
    {
        callable_out = load_attr_from_plan(receiver, plan);
        if(plan.kind == AttributeReadPlanKind::ConstantValue)
        {
            self_out = Value::not_present();
            return true;
        }
        if(plan.kind == AttributeReadPlanKind::BindFunctionReceiver &&
           has_native_layout<Function>(callable_out))
        {
            self_out = receiver;
        }
        else
        {
            self_out = Value::not_present();
        }
        return true;
    }

    bool load_method(Value obj, TValue<String> name, Value &callable_out,
                     Value &self_out)
    {
        AttributeReadDescriptor descriptor =
            resolve_attr_read_descriptor(obj, name);
        if(!descriptor.is_found())
        {
            callable_out = Value::not_present();
            self_out = Value::not_present();
            return false;
        }
        return load_method_from_plan(obj, descriptor.plan, callable_out,
                                     self_out);
    }

    bool load_special_method(Value obj, TValue<String> name,
                             Value &callable_out, Value &self_out)
    {
        AttributeReadDescriptor descriptor =
            resolve_special_method_read_descriptor(obj, name);
        if(!descriptor.is_found())
        {
            callable_out = Value::not_present();
            self_out = Value::not_present();
            return false;
        }
        return load_method_from_plan(obj, descriptor.plan, callable_out,
                                     self_out);
    }

    Value load_attr(Value obj, TValue<String> name)
    {
        AttributeReadDescriptor descriptor =
            resolve_attr_read_descriptor(obj, name);
        if(!descriptor.is_found())
        {
            return Value::not_present();
        }
        return load_attr_from_plan(obj, descriptor.plan);
    }

    AttributeWriteDescriptor resolve_attr_write_descriptor(Value obj,
                                                           TValue<String> name)
    {
        if(!obj.is_ptr())
        {
            return AttributeWriteDescriptor::non_object_receiver();
        }

        Object *object = obj.get_ptr<Object>();
        ClassObject *lookup_class = object->get_shape()->get_class();
        AttributeReadDescriptor class_descriptor =
            lookup_class_attribute_read_descriptor(lookup_class, name);
        AttributeCacheBlockers cache_blockers = class_descriptor.cache_blockers;
        if(class_descriptor.is_found())
        {
            Value value = class_descriptor.lookup_value;
            ThreadState *thread = active_thread();
            ClassObject *type = thread->class_of_value(value);
            DescriptorProtocol protocol = lookup_descriptor_protocol(type);
            if(protocol.has_set_or_delete())
            {
                if(protocol.set.is_not_present())
                {
                    return AttributeWriteDescriptor::disallowed();
                }
                AttributeWriteDescriptor setter =
                    AttributeWriteDescriptor::found(
                        AttributeMutationPlan::descriptor_mutation(
                            AttributeMutationPlanKind::DescriptorSet, value,
                            protocol.set));
                if(object->get_shape()->has_flag(ShapeFlag::IsClassObject) ||
                   !has_native_layout<Function>(protocol.set))
                {
                    return setter;
                }
                setter.cache_blockers = mutation_cache_blockers_for_class_value(
                    class_descriptor, type);
                if(attribute_cache_blockers_are_none(setter.cache_blockers) &&
                   type == thread->class_for_native_layout(
                               NativeLayoutId::Property))
                {
                    setter.plan.kind =
                        AttributeMutationPlanKind::ExactPropertySet;
                }
                return with_mro_shape_and_contents_validity_cell_if_unblocked(
                    setter, lookup_class);
            }
            cache_blockers =
                mutation_cache_blockers_for_class_value(class_descriptor, type);
        }

        AttributeWriteDescriptor own_descriptor =
            object->lookup_own_attribute_write_descriptor(name);
        own_descriptor.cache_blockers |= cache_blockers;
        if(!own_descriptor.is_found())
        {
            if(string_eq(name, active_vm()->dunder_class_name()))
            {
                return AttributeWriteDescriptor::found(
                    AttributeMutationPlan::change_class());
            }
            return own_descriptor;
        }

        return with_mro_shape_and_contents_validity_cell_if_unblocked(
            own_descriptor, lookup_class);
    }

    AttributeDeleteDescriptor
    resolve_attr_delete_descriptor(Value obj, TValue<String> name)
    {
        if(!obj.is_ptr())
        {
            return AttributeDeleteDescriptor::non_object_receiver();
        }

        Object *object = obj.get_ptr<Object>();
        ClassObject *lookup_class = object->get_shape()->get_class();
        AttributeReadDescriptor class_descriptor =
            lookup_class_attribute_read_descriptor(lookup_class, name);
        AttributeCacheBlockers cache_blockers = class_descriptor.cache_blockers;
        if(class_descriptor.is_found())
        {
            Value value = class_descriptor.lookup_value;
            ClassObject *type = active_thread()->class_of_value(value);
            DescriptorProtocol protocol = lookup_descriptor_protocol(type);
            if(protocol.has_set_or_delete())
            {
                if(protocol.delete_.is_not_present())
                {
                    return AttributeDeleteDescriptor::disallowed();
                }
                AttributeDeleteDescriptor deleter =
                    AttributeDeleteDescriptor::found(
                        AttributeMutationPlan::descriptor_mutation(
                            AttributeMutationPlanKind::DescriptorDelete, value,
                            protocol.delete_));
                if(object->get_shape()->has_flag(ShapeFlag::IsClassObject) ||
                   !has_native_layout<Function>(protocol.delete_))
                {
                    return deleter;
                }
                deleter.cache_blockers =
                    mutation_cache_blockers_for_class_value(class_descriptor,
                                                            type);
                return with_mro_shape_and_contents_validity_cell_if_unblocked(
                    deleter, lookup_class);
            }
            cache_blockers =
                mutation_cache_blockers_for_class_value(class_descriptor, type);
        }

        AttributeDeleteDescriptor own_descriptor =
            object->lookup_own_attribute_delete_descriptor(name);
        if(!own_descriptor.is_found())
        {
            return own_descriptor;
        }

        own_descriptor.cache_blockers |= cache_blockers;
        return with_mro_shape_and_contents_validity_cell_if_unblocked(
            own_descriptor, lookup_class);
    }

    Expected<bool> store_attr_from_plan(Value receiver,
                                        const AttributeMutationPlan &plan,
                                        Value value)
    {
        value.assert_not_vm_sentinel();

        if(unlikely(plan.is_change_class()))
        {
            return store_attr_change_class(receiver, value);
        }
        assert(!plan.is_descriptor_set());
        if(likely(!plan.is_add_own_property()))
        {
            Object *storage_owner = plan.storage_owner;
            if(storage_owner == nullptr)
            {
                assert(receiver.is_ptr());
                storage_owner = receiver.get_ptr<Object>();
            }

            storage_owner->write_storage_location(plan.storage_location(),
                                                  value);
            invalidate_lookup_cells_for_class_target(storage_owner);
            return Expected<bool>::ok(true);
        }

        assert(plan.is_add_own_property());
        assert(receiver.is_ptr());
        assert(plan.next_shape != nullptr);
        assert(plan.storage_kind == StorageKind::Inline);
        Object *object = receiver.get_ptr<Object>();
        object->set_shape(plan.next_shape);
        object->write_empty_storage_location(plan.storage_location(), value);
        return Expected<bool>::ok(true);
    }

    bool delete_attr_from_plan(Value receiver,
                               const AttributeMutationPlan &plan)
    {
        assert(plan.is_delete_own_property());
        assert(receiver.is_ptr());
        assert(plan.next_shape != nullptr);
        Object *object = receiver.get_ptr<Object>();
        object->set_shape(plan.next_shape);
        object->write_storage_location(plan.storage_location(),
                                       Value::not_present());
        return true;
    }

    // Attribute mapping entries are the stored own namespace entries that back
    // live mapping views like globals() and, later, obj.__dict__. They are
    // deliberately narrower than attribute lookup: special-read descriptors,
    // synthetic shape values, and inherited/class-chain results do not appear
    // here.
    bool descriptor_is_attribute_mapping_entry(DescriptorInfo info)
    {
        return info.storage_location().is_found() &&
               !info.has_flag(DescriptorFlag::SpecialRead);
    }

    bool own_attribute_mapping_entry_at(Object *object, uint32_t descriptor_idx,
                                        AttributeMappingEntry &entry)
    {
        assert(object != nullptr);
        Shape *shape = object->get_shape();
        assert(shape != nullptr);
        if(descriptor_idx >= shape->present_count())
        {
            return false;
        }

        DescriptorInfo info = shape->get_descriptor_info(descriptor_idx);
        if(!descriptor_is_attribute_mapping_entry(info))
        {
            return false;
        }

        Value value = object->read_storage_location(info.storage_location());
        if(value.is_not_present())
        {
            return false;
        }

        entry = AttributeMappingEntry{
            shape->get_property_name(descriptor_idx).raw_value(), value};
        return true;
    }

    Value load_own_attribute_mapping_entry(Object *object, TValue<String> name)
    {
        assert(object != nullptr);
        Shape *shape = object->get_shape();
        assert(shape != nullptr);
        int32_t descriptor_idx = shape->lookup_descriptor_index(name);
        if(descriptor_idx < 0)
        {
            return active_thread()->set_pending_builtin_exception_none(
                L"KeyError");
        }

        AttributeMappingEntry entry;
        if(!own_attribute_mapping_entry_at(
               object, static_cast<uint32_t>(descriptor_idx), entry))
        {
            return active_thread()->set_pending_builtin_exception_none(
                L"KeyError");
        }
        return entry.value;
    }

    size_t count_own_attribute_mapping_entries(Object *object)
    {
        assert(object != nullptr);
        Shape *shape = object->get_shape();
        assert(shape != nullptr);
        size_t count = 0;
        AttributeMappingEntry entry;
        for(uint32_t idx = 0; idx < shape->present_count(); ++idx)
        {
            if(own_attribute_mapping_entry_at(object, idx, entry))
            {
                ++count;
            }
        }
        return count;
    }

    Expected<bool> store_attr(Value obj, TValue<String> name, Value value)
    {
        value.assert_not_vm_sentinel();

        AttributeWriteDescriptor descriptor =
            resolve_attr_write_descriptor(obj, name);
        if(descriptor.is_found())
        {
            if(descriptor.plan.is_descriptor_set())
            {
                CL_TRY(execute_descriptor_mutation(
                    *active_thread(), descriptor.plan, obj, value));
                return Expected<bool>::ok(true);
            }
            return store_attr_from_plan(obj, descriptor.plan, value);
        }
        if(descriptor.status == AttributeWriteStatus::NotFound && obj.is_ptr())
        {
            return Expected<bool>::ok(
                obj.get_ptr<Object>()->add_own_property(name, value));
        }
        return Expected<bool>::ok(false);
    }

    Expected<bool> delete_attr(Value obj, TValue<String> name)
    {
        AttributeDeleteDescriptor descriptor =
            resolve_attr_delete_descriptor(obj, name);
        if(descriptor.is_found())
        {
            if(descriptor.plan.is_descriptor_delete())
            {
                CL_TRY(execute_descriptor_mutation(
                    *active_thread(), descriptor.plan, obj, Value::None()));
                return Expected<bool>::ok(true);
            }
            return Expected<bool>::ok(
                delete_attr_from_plan(obj, descriptor.plan));
        }
        return Expected<bool>::ok(false);
    }
}  // namespace cl
