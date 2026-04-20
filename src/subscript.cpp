#include "subscript.h"

#include "dict.h"
#include "list.h"
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

    Value load_subscript(Value obj, Value key)
    {
        if(!obj.is_ptr())
        {
            return Value::not_present();
        }

        Object *object = obj.get_ptr<Object>();
        if(object->klass == &List::klass)
        {
            return static_cast<List *>(object)->get_item(
                list_index_from_value(key));
        }
        if(object->klass == &Dict::klass)
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
        if(object->klass == &List::klass)
        {
            static_cast<List *>(object)->set_item(list_index_from_value(key),
                                                  value);
            return true;
        }
        if(object->klass == &Dict::klass)
        {
            static_cast<Dict *>(object)->set_item(key, value);
            return true;
        }

        return false;
    }
}  // namespace cl
