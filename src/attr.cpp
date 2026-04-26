#include "attr.h"

#include "attribute_descriptor.h"
#include "class_object.h"
#include "runtime_helpers.h"

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
                ->invalidate_lookup_validity_cells();
        }
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
            type->lookup_class_attribute_descriptor(get_name).plan.value,
            type->lookup_class_attribute_descriptor(set_name).plan.value,
            type->lookup_class_attribute_descriptor(delete_name).plan.value};
    }

    static AttributeReadDescriptor
    classify_class_descriptor(AttributeReadDescriptor descriptor)
    {
        if(!descriptor.is_found() ||
           descriptor.plan.kind == AttributeReadPlanKind::BindFunctionReceiver)
        {
            return descriptor;
        }

        DescriptorProtocol protocol =
            lookup_descriptor_protocol(descriptor.plan.value);
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

    static AttributeReadDescriptor
    resolve_class_attr_descriptor(ClassObject *cls, TValue<String> name)
    {
        AttributeReadDescriptor class_descriptor = classify_class_descriptor(
            cls->lookup_class_attribute_descriptor(name));
        if(class_descriptor.is_found())
        {
            return class_descriptor;
        }

        ClassObject *metaclass = cls->get_class().extract();
        if(metaclass == cls)
        {
            return AttributeReadDescriptor::not_found();
        }

        return classify_class_descriptor(
            metaclass->lookup_metaclass_attribute_descriptor(name, cls));
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
            return resolve_class_attr_descriptor(
                static_cast<ClassObject *>(object), name);
        }

        ClassObject *class_object = object->get_class().extract();
        AttributeReadDescriptor class_descriptor = classify_class_descriptor(
            class_object->lookup_instance_attribute_descriptor(name, obj));
        if(class_descriptor.is_found() &&
           class_descriptor.plan.kind ==
               AttributeReadPlanKind::DataDescriptorGet)
        {
            return class_descriptor;
        }

        AttributeReadDescriptor own_descriptor =
            object->lookup_own_attribute_descriptor(name);
        if(own_descriptor.is_found())
        {
            return own_descriptor;
        }

        return class_descriptor;
    }

    Value load_attr_from_plan(const AttributeReadPlan &plan)
    {
        if(plan.kind == AttributeReadPlanKind::ReceiverSlot)
        {
            assert(plan.storage_owner != nullptr);
            return plan.storage_owner->read_storage_location(
                plan.storage_location);
        }

        if(plan.kind == AttributeReadPlanKind::DataDescriptorGet ||
           plan.kind == AttributeReadPlanKind::NonDataDescriptorGet)
        {
            throw std::runtime_error(
                "TypeError: descriptor __get__ requires interpreter dispatch");
        }

        return plan.value;
    }

    bool load_method_from_plan(const AttributeReadPlan &plan,
                               Value &callable_out, Value &self_out)
    {
        callable_out = load_attr_from_plan(plan);
        if(plan.kind == AttributeReadPlanKind::BindFunctionReceiver)
        {
            self_out = plan.binding.self;
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
        return load_method_from_plan(descriptor.plan, callable_out, self_out);
    }

    Value load_attr(Value obj, TValue<String> name)
    {
        AttributeReadDescriptor descriptor =
            resolve_attr_read_descriptor(obj, name);
        if(!descriptor.is_found())
        {
            return Value::not_present();
        }
        return load_attr_from_plan(descriptor.plan);
    }

    AttributeWriteDescriptor resolve_attr_write_descriptor(Value obj,
                                                           TValue<String> name)
    {
        if(!obj.is_ptr())
        {
            return AttributeWriteDescriptor::non_object_receiver();
        }

        Object *object = obj.get_ptr<Object>();
        return object->lookup_own_attribute_write_descriptor(name);
    }

    bool store_attr_from_plan(const AttributeWritePlan &plan, Value value)
    {
        assert(plan.storage_owner != nullptr);

        plan.storage_owner->write_storage_location(plan.storage_location,
                                                   value);
        invalidate_lookup_cells_for_class_target(plan.storage_owner);
        return true;
    }

    bool store_attr(Value obj, TValue<String> name, Value value)
    {
        AttributeWriteDescriptor descriptor =
            resolve_attr_write_descriptor(obj, name);
        if(descriptor.is_found())
        {
            return store_attr_from_plan(descriptor.plan, value);
        }
        if(descriptor.status == AttributeWriteStatus::NotFound && obj.is_ptr())
        {
            return obj.get_ptr<Object>()->add_own_property(name, value);
        }
        return false;
    }
}  // namespace cl
