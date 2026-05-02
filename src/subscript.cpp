#include "subscript.h"

#include "dict.h"
#include "list.h"
#include "tuple.h"
#include <stdexcept>

namespace cl
{
    static int64_t list_index_from_value(Value key)
    {
        if(!key.is_integer())
        {
            throw std::runtime_error(
                "TypeError: list indices must be integers");
        }
        return key.get_smi();
    }

    static int64_t tuple_index_from_value(Value key)
    {
        if(!key.is_integer())
        {
            throw std::runtime_error(
                "TypeError: tuple indices must be integers");
        }
        return key.get_smi();
    }

    Value load_subscript(Value obj, Value key)
    {
        if(!obj.is_ptr())
        {
            return Value::not_present();
        }

        Object *object = obj.get_ptr<Object>();
        if(object->native_layout_id() == NativeLayoutId::List)
        {
            return static_cast<List *>(object)->get_item(
                list_index_from_value(key));
        }
        if(object->native_layout_id() == NativeLayoutId::Tuple)
        {
            return static_cast<Tuple *>(object)->get_item(
                tuple_index_from_value(key));
        }
        if(object->native_layout_id() == NativeLayoutId::Dict)
        {
            return static_cast<Dict *>(object)->get_item(key);
        }

        return Value::not_present();
    }

    bool store_subscript(Value obj, Value key, Value value)
    {
        if(!obj.is_ptr())
        {
            return false;
        }

        Object *object = obj.get_ptr<Object>();
        if(object->native_layout_id() == NativeLayoutId::List)
        {
            static_cast<List *>(object)->set_item(list_index_from_value(key),
                                                  value);
            return true;
        }
        if(object->native_layout_id() == NativeLayoutId::Tuple)
        {
            throw std::runtime_error(
                "TypeError: 'tuple' object does not support item assignment");
        }
        if(object->native_layout_id() == NativeLayoutId::Dict)
        {
            static_cast<Dict *>(object)->set_item(key, value);
            return true;
        }

        return false;
    }

    bool del_subscript(Value obj, Value key)
    {
        if(!obj.is_ptr())
        {
            return false;
        }

        Object *object = obj.get_ptr<Object>();
        if(object->native_layout_id() == NativeLayoutId::List)
        {
            (void)static_cast<List *>(object)->pop_item(
                list_index_from_value(key));
            return true;
        }
        if(object->native_layout_id() == NativeLayoutId::Tuple)
        {
            throw std::runtime_error(
                "TypeError: 'tuple' object does not support item deletion");
        }
        if(object->native_layout_id() == NativeLayoutId::Dict)
        {
            static_cast<Dict *>(object)->del_item(key);
            return true;
        }

        return false;
    }
}  // namespace cl
