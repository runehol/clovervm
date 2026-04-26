#include "tuple.h"
#include "class_object.h"
#include "refcount.h"
#include <stdexcept>

namespace cl
{
    Tuple::Tuple(HeapLayout layout, BootstrapObjectTag, size_t size)
        : Object(BootstrapObjectTag{}, native_layout_id, layout),
          size_value(Value::from_smi(static_cast<int64_t>(size)))
    {
        initialize_items(size);
    }

    Tuple::Tuple(HeapLayout layout, ClassObject *cls, size_t size)
        : Object(cls, native_layout_id, layout),
          size_value(Value::from_smi(static_cast<int64_t>(size)))
    {
        initialize_items(size);
    }

    void Tuple::initialize_item_unchecked(size_t idx, Value value)
    {
        assert(idx < size());
        Value old_value = elements[idx];
        elements[idx] = incref(value);
        decref(old_value);
    }

    Value Tuple::get_item(int64_t py_idx) const
    {
        return item_unchecked(normalize_index(py_idx));
    }

    size_t Tuple::normalize_index(int64_t py_idx) const
    {
        int64_t n_items = static_cast<int64_t>(size());
        int64_t normalized = py_idx;
        if(normalized < 0)
        {
            normalized += n_items;
        }
        if(normalized < 0 || normalized >= n_items)
        {
            throw std::runtime_error("IndexError: tuple index out of range");
        }
        return static_cast<size_t>(normalized);
    }

    void Tuple::initialize_items(size_t size)
    {
        for(size_t idx = 0; idx < size; ++idx)
        {
            elements[idx] = Value::not_present();
        }
    }

}  // namespace cl
