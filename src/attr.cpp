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

    static AttributeReadAccess with_access_kind(
        AttributeReadAccess access, AttributeReadAccessKind kind,
        AttributeCacheBlocker blocker = AttributeCacheBlocker::None)
    {
        access.kind = kind;
        if(blocker != AttributeCacheBlocker::None)
        {
            access.cache_blockers =
                attribute_cache_blockers(access.cache_blockers, blocker);
        }
        return access;
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
            type->lookup_class_attribute_descriptor(get_name).access.value,
            type->lookup_class_attribute_descriptor(set_name).access.value,
            type->lookup_class_attribute_descriptor(delete_name).access.value};
    }

    static AttributeReadDescriptor
    classify_class_descriptor(AttributeReadDescriptor descriptor)
    {
        if(!descriptor.is_found() ||
           descriptor.access.kind ==
               AttributeReadAccessKind::BindFunctionReceiver)
        {
            return descriptor;
        }

        DescriptorProtocol protocol =
            lookup_descriptor_protocol(descriptor.access.value);
        if(!protocol.has_get())
        {
            return descriptor;
        }

        AttributeReadAccessKind kind =
            protocol.has_set_or_delete()
                ? AttributeReadAccessKind::DataDescriptorGet
                : AttributeReadAccessKind::NonDataDescriptorGet;
        return AttributeReadDescriptor::found(
            with_access_kind(descriptor.access, kind,
                             AttributeCacheBlocker::UnsupportedDescriptorKind));
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
           class_descriptor.access.kind ==
               AttributeReadAccessKind::DataDescriptorGet)
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

        if(access.kind == AttributeReadAccessKind::DataDescriptorGet ||
           access.kind == AttributeReadAccessKind::NonDataDescriptorGet)
        {
            throw std::runtime_error(
                "TypeError: descriptor __get__ requires interpreter dispatch");
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
