#include "attr.h"
#include "class_object.h"
#include "instance.h"
#include "klass.h"
#include <cwchar>

namespace cl
{
    static bool is_dunder_class(TValue<String> name)
    {
        return std::wcscmp(name.extract()->data, L"__class__") == 0;
    }

    static Value load_dunder_class(Value obj)
    {
        if(!obj.is_ptr())
        {
            return Value::not_present();
        }

        Object *object = obj.get_ptr<Object>();
        if(object->klass == &Instance::klass)
        {
            return obj.get_ptr<Instance>()->get_class();
        }

        return Value::from_oop(const_cast<Klass *>(object->klass));
    }

    Value load_attr(Value obj, TValue<String> name)
    {
        if(is_dunder_class(name))
        {
            return load_dunder_class(obj);
        }

        if(!obj.is_ptr())
        {
            return Value::not_present();
        }

        Object *object = obj.get_ptr<Object>();
        if(object->klass == &Instance::klass)
        {
            Instance *instance = static_cast<Instance *>(object);
            Value own_property = instance->get_own_property(name);
            if(!own_property.is_not_present())
            {
                return own_property;
            }

            Value cls = instance->get_class();
            if(cls.is_ptr() &&
               cls.get_ptr<Object>()->klass == &ClassObject::klass)
            {
                return cls.get_ptr<ClassObject>()->get_member(name);
            }

            return Value::not_present();
        }

        if(object->klass == &ClassObject::klass)
        {
            return static_cast<ClassObject *>(object)->get_member(name);
        }

        return Value::not_present();
    }

    bool store_attr(Value obj, TValue<String> name, Value value)
    {
        if(is_dunder_class(name) || !obj.is_ptr())
        {
            return false;
        }

        Object *object = obj.get_ptr<Object>();
        if(object->klass == &Instance::klass)
        {
            static_cast<Instance *>(object)->set_own_property(name, value);
            return true;
        }

        if(object->klass == &ClassObject::klass)
        {
            static_cast<ClassObject *>(object)->set_member(name, value);
            return true;
        }

        return false;
    }
}  // namespace cl
