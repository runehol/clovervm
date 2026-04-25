#include "attr.h"
#include "class_object.h"
#include "function.h"
#include "instance.h"
#include <cwchar>

namespace cl
{
    static bool is_dunder_class(TValue<String> name)
    {
        return std::wcscmp(name.extract()->data, L"__class__") == 0;
    }

    bool load_method(Value obj, TValue<String> name, Value &callable_out,
                     Value &self_out)
    {
        if(is_dunder_class(name) &&
           (!obj.is_ptr() || !can_convert_to<Instance>(obj)))
        {
            if(ClassObject *cls = try_convert_to<ClassObject>(obj))
            {
                callable_out = cls->lookup_class_chain(name);
            }
            else
            {
                callable_out = Value::not_present();
            }
            self_out = Value::not_present();
            return !callable_out.is_not_present();
        }

        if(!obj.is_ptr())
        {
            callable_out = Value::not_present();
            self_out = Value::not_present();
            return false;
        }

        Object *object = obj.get_ptr<Object>();
        if(object->native_layout_id() == NativeLayoutId::Instance)
        {
            Instance *instance = static_cast<Instance *>(object);
            Value own_property = instance->get_own_property(name);
            if(!own_property.is_not_present())
            {
                callable_out = own_property;
                self_out = Value::not_present();
                return true;
            }

            Value cls = instance->get_class();
            ClassObject *class_object = try_convert_to<ClassObject>(cls);
            if(class_object != nullptr)
            {
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

            callable_out = Value::not_present();
            self_out = Value::not_present();
            return false;
        }

        if(object->native_layout_id() == NativeLayoutId::ClassObject)
        {
            callable_out =
                static_cast<ClassObject *>(object)->lookup_class_chain(name);
            self_out = Value::not_present();
            return !callable_out.is_not_present();
        }

        callable_out = Value::not_present();
        self_out = Value::not_present();
        return false;
    }

    Value load_attr(Value obj, TValue<String> name)
    {
        if(is_dunder_class(name) &&
           (!obj.is_ptr() || !can_convert_to<Instance>(obj)))
        {
            if(ClassObject *cls = try_convert_to<ClassObject>(obj))
            {
                return cls->lookup_class_chain(name);
            }
            return Value::not_present();
        }

        if(!obj.is_ptr())
        {
            return Value::not_present();
        }

        Object *object = obj.get_ptr<Object>();
        if(object->native_layout_id() == NativeLayoutId::Instance)
        {
            Instance *instance = static_cast<Instance *>(object);
            Value own_property = instance->get_own_property(name);
            if(!own_property.is_not_present())
            {
                return own_property;
            }

            Value cls = instance->get_class();
            ClassObject *class_object = try_convert_to<ClassObject>(cls);
            if(class_object != nullptr)
            {
                return class_object->lookup_class_chain(name);
            }

            return Value::not_present();
        }

        if(object->native_layout_id() == NativeLayoutId::ClassObject)
        {
            return static_cast<ClassObject *>(object)->lookup_class_chain(name);
        }

        return Value::not_present();
    }

    bool store_attr(Value obj, TValue<String> name, Value value)
    {
        if(!obj.is_ptr())
        {
            return false;
        }

        Object *object = obj.get_ptr<Object>();
        if(object->native_layout_id() == NativeLayoutId::Instance)
        {
            Instance *instance = static_cast<Instance *>(object);
            if(is_dunder_class(name))
            {
                return value == instance->get_class();
            }

            return instance->set_own_property(name, value);
        }

        if(is_dunder_class(name))
        {
            return false;
        }

        if(object->native_layout_id() == NativeLayoutId::ClassObject)
        {
            return static_cast<ClassObject *>(object)->set_own_property(name,
                                                                        value);
        }

        return false;
    }
}  // namespace cl
