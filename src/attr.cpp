#include "attr.h"
#include "class_object.h"
#include "function.h"

namespace cl
{
    static Value load_class_attr(ClassObject *cls, TValue<String> name)
    {
        Value class_property = cls->lookup_class_chain(name);
        if(!class_property.is_not_present())
        {
            return class_property;
        }

        ClassObject *metaclass = cls->get_class().extract();
        if(metaclass == cls)
        {
            return Value::not_present();
        }

        return metaclass->lookup_class_chain(name);
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

        Object *object = obj.get_ptr<Object>();
        if(object->native_layout_id() == NativeLayoutId::ClassObject)
        {
            callable_out =
                load_class_attr(static_cast<ClassObject *>(object), name);
            self_out = Value::not_present();
            return !callable_out.is_not_present();
        }

        Value own_property = object->get_own_property(name);
        if(!own_property.is_not_present())
        {
            callable_out = own_property;
            self_out = Value::not_present();
            return true;
        }

        ClassObject *class_object = object->get_class().extract();
        Value class_property = class_object->lookup_class_chain(name);
        if(class_property.is_not_present())
        {
            callable_out = Value::not_present();
            self_out = Value::not_present();
            return false;
        }

        callable_out = class_property;
        if(can_convert_to<Function>(class_property))
        {
            self_out = obj;
        }
        else
        {
            self_out = Value::not_present();
        }
        return true;
    }

    Value load_attr(Value obj, TValue<String> name)
    {
        if(!obj.is_ptr())
        {
            return Value::not_present();
        }

        Object *object = obj.get_ptr<Object>();
        if(object->native_layout_id() == NativeLayoutId::ClassObject)
        {
            return load_class_attr(static_cast<ClassObject *>(object), name);
        }

        Value own_property = object->get_own_property(name);
        if(!own_property.is_not_present())
        {
            return own_property;
        }

        return object->get_class().extract()->lookup_class_chain(name);
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
