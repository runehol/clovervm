#include "subscript.h"

#include "dict.h"
#include "exception_propagation.h"
#include "list.h"
#include "thread_state.h"
#include "tuple.h"
#include <stdexcept>

namespace cl
{
    [[nodiscard]] static Value list_index_from_value(Value key,
                                                     int64_t &idx_out)
    {
        if(!key.is_integer())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"list indices must be integers");
        }
        idx_out = key.get_smi();
        return Value::None();
    }

    [[nodiscard]] static Value tuple_index_from_value(Value key,
                                                      int64_t &idx_out)
    {
        if(!key.is_integer())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"tuple indices must be integers");
        }
        idx_out = key.get_smi();
        return Value::None();
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
            int64_t idx = 0;
            CL_PROPAGATE_EXCEPTION(list_index_from_value(key, idx));
            return static_cast<List *>(object)->get_item(idx);
        }
        if(object->native_layout_id() == NativeLayoutId::Tuple)
        {
            int64_t idx = 0;
            CL_PROPAGATE_EXCEPTION(tuple_index_from_value(key, idx));
            return static_cast<Tuple *>(object)->get_item(idx);
        }
        if(object->native_layout_id() == NativeLayoutId::Dict)
        {
            Value result = static_cast<Dict *>(object)->get_item(key);
            CL_PROPAGATE_EXCEPTION(result);
            return result;
        }

        return Value::not_present();
    }

    Value store_subscript(Value obj, Value key, Value value)
    {
        value.assert_not_vm_sentinel();

        if(!obj.is_ptr())
        {
            return Value::not_present();
        }

        Object *object = obj.get_ptr<Object>();
        if(object->native_layout_id() == NativeLayoutId::List)
        {
            int64_t idx = 0;
            CL_PROPAGATE_EXCEPTION(list_index_from_value(key, idx));
            CL_PROPAGATE_EXCEPTION(
                static_cast<List *>(object)->set_item(idx, value));
            return Value::None();
        }
        if(object->native_layout_id() == NativeLayoutId::Tuple)
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError",
                L"'tuple' object does not support item assignment");
        }
        if(object->native_layout_id() == NativeLayoutId::Dict)
        {
            static_cast<Dict *>(object)->set_item(key, value);
            return Value::None();
        }

        return Value::not_present();
    }

    Value del_subscript(Value obj, Value key)
    {
        if(!obj.is_ptr())
        {
            return Value::not_present();
        }

        Object *object = obj.get_ptr<Object>();
        if(object->native_layout_id() == NativeLayoutId::List)
        {
            int64_t idx = 0;
            CL_PROPAGATE_EXCEPTION(list_index_from_value(key, idx));
            CL_PROPAGATE_EXCEPTION(static_cast<List *>(object)->pop_item(idx));
            return Value::None();
        }
        if(object->native_layout_id() == NativeLayoutId::Tuple)
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"'tuple' object does not support item deletion");
        }
        if(object->native_layout_id() == NativeLayoutId::Dict)
        {
            CL_PROPAGATE_EXCEPTION(static_cast<Dict *>(object)->del_item(key));
            return Value::None();
        }

        return Value::not_present();
    }
}  // namespace cl
