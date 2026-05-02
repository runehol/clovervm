#include "attr.h"

#include "attribute_descriptor.h"
#include "class_object.h"
#include "function.h"
#include "runtime_helpers.h"
#include "tuple.h"

namespace cl
{
    struct DescriptorProtocol
    {
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
            assume_convert_to<ClassObject>(object)
                ->invalidate_lookup_validity_cells_for_contents_change();
        }
    }

    static AttributeReadPlanKind
    attribute_read_plan_kind_for_path(AttributeReadPlanPath path, Value value)
    {
        if(path == AttributeReadPlanPath::InstanceClassChain &&
           can_convert_to<Function>(value))
        {
            // Class-chain method plans reload the defining class slot before
            // binding, so ordinary class contents writes do not invalidate the
            // method IC.
            return AttributeReadPlanKind::BindFunctionReceiver;
        }

        // Ordinary lookup hits are cached as storage loads. This lets
        // contents writes update the observed value without invalidating
        // shape-only lookup assumptions, and keeps even rare metaclass-chain
        // hits on the same simple IC representation.
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
        Shape *type_shape = object->get_class().extract()->get_shape();
        if(type_shape->has_flag(ShapeFlag::IsImmutableType))
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
        descriptor.plan.lookup_validity_cell = nullptr;
        if(descriptor.is_found() &&
           attribute_cache_blockers_are_none(descriptor.cache_blockers) &&
           descriptor.plan.kind != AttributeReadPlanKind::DataDescriptorGet &&
           descriptor.plan.kind != AttributeReadPlanKind::NonDataDescriptorGet)
        {
            descriptor.plan.lookup_validity_cell =
                cls->get_or_create_mro_shape_and_contents_validity_cell();
        }
        return descriptor;
    }

    static AttributeReadDescriptor
    with_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell_if_unblocked(
        AttributeReadDescriptor descriptor, ClassObject *cls)
    {
        descriptor.plan.lookup_validity_cell = nullptr;
        if(descriptor.is_found() &&
           attribute_cache_blockers_are_none(descriptor.cache_blockers) &&
           descriptor.plan.kind != AttributeReadPlanKind::DataDescriptorGet &&
           descriptor.plan.kind != AttributeReadPlanKind::NonDataDescriptorGet)
        {
            descriptor.plan.lookup_validity_cell =
                cls->get_or_create_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell();
        }
        return descriptor;
    }

    static AttributeWriteDescriptor
    with_mro_shape_and_contents_validity_cell_if_unblocked(
        AttributeWriteDescriptor descriptor, ClassObject *cls)
    {
        descriptor.plan.lookup_validity_cell = nullptr;
        if(descriptor.is_found() &&
           attribute_cache_blockers_are_none(descriptor.cache_blockers))
        {
            descriptor.plan.lookup_validity_cell =
                cls->get_or_create_mro_shape_and_contents_validity_cell();
        }
        return descriptor;
    }

    static AttributeReadDescriptor lookup_class_chain_read_descriptor(
        const ClassObject *class_object, TValue<String> name,
        AttributeReadPlanPath path, AttributeBindingContext binding)
    {
        Value mro_value = class_object->get_mro_value();
        if(!can_convert_to<Tuple>(mro_value))
        {
            StorageLocation own_location =
                class_object->get_shape()->resolve_present_property(name);
            if(!own_location.is_found())
            {
                return AttributeReadDescriptor::not_found();
            }

            Value own_value = class_object->read_storage_location(own_location);
            return AttributeReadDescriptor::found(
                AttributeReadPlan::from_storage(
                    path, attribute_read_plan_kind_for_path(path, own_value),
                    class_object, own_location, binding),
                own_value);
        }

        Tuple *mro = try_convert_to<Tuple>(mro_value);
        for(uint32_t mro_idx = 0; mro_idx < mro->size(); ++mro_idx)
        {
            Value class_value = mro->item_unchecked(mro_idx);
            ClassObject *cls = try_convert_to<ClassObject>(class_value);
            if(cls == nullptr)
            {
                continue;
            }

            DescriptorLookup lookup =
                cls->get_shape()->lookup_descriptor_including_latent(name);
            if(!lookup.is_present())
            {
                continue;
            }

            Value value = cls->read_storage_location(lookup.storage_location());
            return AttributeReadDescriptor::found(
                AttributeReadPlan::from_storage(
                    path, attribute_read_plan_kind_for_path(path, value), cls,
                    lookup.storage_location(), binding),
                value);
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

    static DescriptorProtocol lookup_descriptor_protocol(Value value)
    {
        if(!value.is_ptr())
        {
            return DescriptorProtocol{Value::not_present(),
                                      Value::not_present(),
                                      Value::not_present()};
        }

        Object *object = value.get_ptr<Object>();
        ClassObject *type = object->get_class().extract();
        TValue<String> get_name(interned_string(L"__get__"));
        TValue<String> set_name(interned_string(L"__set__"));
        TValue<String> delete_name(interned_string(L"__delete__"));

        return DescriptorProtocol{
            lookup_class_attribute_read_descriptor(type, get_name).lookup_value,
            lookup_class_attribute_read_descriptor(type, set_name).lookup_value,
            lookup_class_attribute_read_descriptor(type, delete_name)
                .lookup_value};
    }

    static AttributeReadDescriptor
    classify_class_read_descriptor(AttributeReadDescriptor descriptor)
    {
        if(!descriptor.is_found() ||
           descriptor.plan.kind == AttributeReadPlanKind::BindFunctionReceiver)
        {
            return descriptor;
        }

        DescriptorProtocol protocol =
            lookup_descriptor_protocol(descriptor.lookup_value);
        if(!protocol.has_get())
        {
            return descriptor;
        }

        AttributeReadPlanKind kind =
            protocol.has_set_or_delete()
                ? AttributeReadPlanKind::DataDescriptorGet
                : AttributeReadPlanKind::NonDataDescriptorGet;
        return with_plan_kind(descriptor, kind,
                              AttributeCacheBlocker::UnsupportedDescriptorKind);
    }

    static bool class_read_descriptor_is_write_descriptor(
        const AttributeReadDescriptor &descriptor)
    {
        if(!descriptor.is_found())
        {
            return false;
        }

        DescriptorProtocol protocol =
            lookup_descriptor_protocol(descriptor.lookup_value);
        return protocol.has_set_or_delete();
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

        ClassObject *metaclass = cls->get_class().extract();
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
        if(!obj.is_ptr())
        {
            return AttributeReadDescriptor::non_object_receiver();
        }

        Object *object = obj.get_ptr<Object>();
        if(object->get_shape()->has_flag(ShapeFlag::IsClassObject))
        {
            assert(object->native_layout_id() == NativeLayoutId::ClassObject);
            return resolve_class_attr_read_descriptor(
                static_cast<ClassObject *>(object), name);
        }

        ClassObject *class_object = object->get_class().extract();
        AttributeReadDescriptor class_descriptor =
            classify_class_read_descriptor(
                lookup_instance_attribute_read_descriptor(class_object, name,
                                                          obj));
        if(class_descriptor.is_found() &&
           class_descriptor.plan.kind ==
               AttributeReadPlanKind::DataDescriptorGet)
        {
            return with_mro_shape_and_contents_validity_cell_if_unblocked(
                class_descriptor, class_object);
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

        // A class-chain hit only needs the receiver class MRO shape. Reuse the
        // combined class-read cell instead of adding a third owned cell: it is
        // shape-only along this MRO and merely over-invalidates on metaclass
        // changes, which are much colder than class contents writes.
        return with_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell_if_unblocked(
            class_descriptor, class_object);
    }

    Value load_attr_from_plan(Value receiver, const AttributeReadPlan &plan)
    {
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

        if(plan.kind == AttributeReadPlanKind::DataDescriptorGet ||
           plan.kind == AttributeReadPlanKind::NonDataDescriptorGet)
        {
            throw std::runtime_error(
                "TypeError: descriptor __get__ requires interpreter dispatch");
        }

        __builtin_unreachable();
    }

    bool load_method_from_plan(Value receiver, const AttributeReadPlan &plan,
                               Value &callable_out, Value &self_out)
    {
        callable_out = load_attr_from_plan(receiver, plan);
        if(plan.kind == AttributeReadPlanKind::BindFunctionReceiver &&
           can_convert_to<Function>(callable_out))
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
        if(!obj.is_ptr())
        {
            callable_out = Value::not_present();
            self_out = Value::not_present();
            return false;
        }

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
        ClassObject *lookup_class = object->get_class().extract();
        AttributeReadDescriptor class_descriptor =
            lookup_class_attribute_read_descriptor(lookup_class, name);
        if(class_read_descriptor_is_write_descriptor(class_descriptor))
        {
            return AttributeWriteDescriptor::disallowed();
        }

        AttributeWriteDescriptor own_descriptor =
            object->lookup_own_attribute_write_descriptor(name);
        if(!own_descriptor.is_found())
        {
            return own_descriptor;
        }

        own_descriptor.cache_blockers |=
            superseded_class_read_descriptor_cache_blockers(class_descriptor);
        return with_mro_shape_and_contents_validity_cell_if_unblocked(
            own_descriptor, lookup_class);
    }

    bool store_attr_from_plan(Value receiver, const AttributeMutationPlan &plan,
                              Value value)
    {
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
            return true;
        }

        assert(plan.is_add_own_property());
        assert(receiver.is_ptr());
        assert(plan.next_shape != nullptr);
        assert(plan.storage_kind == StorageKind::Inline);
        Object *object = receiver.get_ptr<Object>();
        object->set_shape(plan.next_shape);
        object->write_storage_location(plan.storage_location(), value);
        return true;
    }

    bool store_attr(Value obj, TValue<String> name, Value value)
    {
        AttributeWriteDescriptor descriptor =
            resolve_attr_write_descriptor(obj, name);
        if(descriptor.is_found())
        {
            return store_attr_from_plan(obj, descriptor.plan, value);
        }
        if(descriptor.status == AttributeWriteStatus::NotFound && obj.is_ptr())
        {
            return obj.get_ptr<Object>()->add_own_property(name, value);
        }
        return false;
    }
}  // namespace cl
