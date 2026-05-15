#ifndef CL_SUBSCRIPT_H
#define CL_SUBSCRIPT_H

#include "dict.h"
#include "list.h"
#include "tuple.h"
#include "value.h"

namespace cl
{
    [[nodiscard]] Value load_subscript_slow(Value obj, Value key);

    [[nodiscard]] inline ALWAYSINLINE Value load_subscript_fast(Value obj,
                                                                Value key)
    {
        if(!obj.is_ptr())
        {
            return Value::not_present();
        }

        Object *object = obj.get_ptr<Object>();
        NativeLayoutId native_layout_id = object->native_layout_id();
        if(native_layout_id == NativeLayoutId::List)
        {
            if(!key.is_smi())
            {
                return Value::not_present();
            }

            List *list = static_cast<List *>(object);
            int64_t idx = key.get_smi();
            if(idx < 0)
            {
                idx += static_cast<int64_t>(list->size());
            }
            size_t wrapped_idx = static_cast<size_t>(idx);
            if(wrapped_idx >= list->size())
            {
                return Value::not_present();
            }
            return list->item_unchecked(wrapped_idx);
        }
        if(native_layout_id == NativeLayoutId::Tuple)
        {
            if(!key.is_smi())
            {
                return Value::not_present();
            }

            Tuple *tuple = static_cast<Tuple *>(object);
            int64_t idx = key.get_smi();
            if(idx < 0)
            {
                idx += static_cast<int64_t>(tuple->size());
            }
            size_t wrapped_idx = static_cast<size_t>(idx);
            if(wrapped_idx >= tuple->size())
            {
                return Value::not_present();
            }
            return tuple->item_unchecked(wrapped_idx);
        }
        if(native_layout_id == NativeLayoutId::Dict)
        {
            return static_cast<Dict *>(object)->get_item(key);
        }

        return Value::not_present();
    }

    [[nodiscard]] inline ALWAYSINLINE Value load_subscript(Value obj, Value key)
    {
        Value result = load_subscript_fast(obj, key);
        if(!result.is_not_present())
        {
            return result;
        }
        return load_subscript_slow(obj, key);
    }

    [[nodiscard]] Value store_subscript_slow(Value obj, Value key, Value value);

    [[nodiscard]] inline ALWAYSINLINE Value store_subscript_fast(Value obj,
                                                                 Value key,
                                                                 Value value)
    {
        value.assert_not_vm_sentinel();
        if(!obj.is_ptr())
        {
            return Value::not_present();
        }

        Object *object = obj.get_ptr<Object>();
        NativeLayoutId native_layout_id = object->native_layout_id();
        if(native_layout_id == NativeLayoutId::List)
        {
            if(!key.is_smi())
            {
                return Value::not_present();
            }

            List *list = static_cast<List *>(object);
            int64_t idx = key.get_smi();
            if(idx < 0)
            {
                idx += static_cast<int64_t>(list->size());
            }
            size_t wrapped_idx = static_cast<size_t>(idx);
            if(wrapped_idx >= list->size())
            {
                return Value::not_present();
            }
            list->set_item_unchecked(wrapped_idx, value);
            return Value::None();
        }
        if(native_layout_id == NativeLayoutId::Dict)
        {
            static_cast<Dict *>(object)->set_item(key, value);
            return Value::None();
        }

        return Value::not_present();
    }

    [[nodiscard]] inline ALWAYSINLINE Value store_subscript(Value obj,
                                                            Value key,
                                                            Value value)
    {
        Value result = store_subscript_fast(obj, key, value);
        if(!result.is_not_present())
        {
            return result;
        }
        return store_subscript_slow(obj, key, value);
    }

    [[nodiscard]] Value del_subscript(Value obj, Value key);
}  // namespace cl

#endif  // CL_SUBSCRIPT_H
