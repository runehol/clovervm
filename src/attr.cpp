#include "attr.h"

#include "attribute_descriptor.h"
#include "class_object.h"

namespace cl
{
    static AttributeReadDescriptor
    resolve_class_attr_descriptor(ClassObject *cls, TValue<String> name)
    {
        AttributeReadDescriptor class_descriptor =
            cls->lookup_class_attribute_descriptor(name);
        if(class_descriptor.is_found())
        {
            return class_descriptor;
        }

        ClassObject *metaclass = cls->get_class().extract();
        if(metaclass == cls)
        {
            return AttributeReadDescriptor::not_found();
        }

        return metaclass->lookup_metaclass_attribute_descriptor(name, cls);
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

        AttributeReadDescriptor own_descriptor =
            object->lookup_own_attribute_descriptor(name);
        if(own_descriptor.is_found())
        {
            return own_descriptor;
        }

        ClassObject *class_object = object->get_class().extract();
        return class_object->lookup_instance_attribute_descriptor(name, obj);
    }

    Value load_attr_from_descriptor(const AttributeReadDescriptor &descriptor)
    {
        if(!descriptor.is_found())
        {
            return Value::not_present();
        }
        const AttributeReadAccess &access = descriptor.access;
        if(access.kind == AttributeReadAccessKind::ReceiverSlot)
        {
            assert(access.storage_owner != nullptr);
            return access.storage_owner->read_storage_location(
                access.storage_location);
        }

        return access.value;
    }

    bool load_method_from_descriptor(const AttributeReadDescriptor &descriptor,
                                     Value &callable_out, Value &self_out)
    {
        if(!descriptor.is_found())
        {
            callable_out = Value::not_present();
            self_out = Value::not_present();
            return false;
        }

        const AttributeReadAccess &access = descriptor.access;
        callable_out = load_attr_from_descriptor(descriptor);
        if(access.kind == AttributeReadAccessKind::BindFunctionReceiver)
        {
            self_out = access.binding.self;
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

        return load_method_from_descriptor(
            resolve_attr_read_descriptor(obj, name), callable_out, self_out);
    }

    Value load_attr(Value obj, TValue<String> name)
    {
        return load_attr_from_descriptor(
            resolve_attr_read_descriptor(obj, name));
    }

    bool store_attr(Value obj, TValue<String> name, Value value)
    {
        if(!obj.is_ptr())
        {
            return false;
        }

        Object *object = obj.get_ptr<Object>();
        return object->set_own_property(name, value);
    }
}  // namespace cl
