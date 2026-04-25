#include "list.h"
#include <algorithm>
#include <stdexcept>

namespace cl
{
    List::List(size_t size) : Object(native_layout_id, &klass, compact_layout())
    {
        items.resize(size, Value::not_present());
    }

    void List::insert_item_unchecked(size_t idx, Value value)
    {
        assert(idx <= size());

        size_t old_size = size();
        items.resize(old_size + 1, Value::None());
        for(size_t shift_idx = old_size; shift_idx > idx; --shift_idx)
        {
            items.set(shift_idx, items[shift_idx - 1]);
        }
        items.set(idx, value);
    }

    Value List::pop_item_unchecked(size_t idx)
    {
        assert(idx < size());

        Value removed = items[idx];
        size_t old_size = size();
        for(size_t shift_idx = idx; shift_idx + 1 < old_size; ++shift_idx)
        {
            items.set(shift_idx, items[shift_idx + 1]);
        }
        items.resize(old_size - 1);
        return removed;
    }

    Value List::get_item(int64_t py_idx) const
    {
        return item_unchecked(normalize_index(py_idx));
    }

    void List::set_item(int64_t py_idx, Value value)
    {
        set_item_unchecked(normalize_index(py_idx), value);
    }

    void List::insert_item(int64_t py_idx, Value value)
    {
        insert_item_unchecked(normalize_insertion_index(py_idx), value);
    }

    Value List::pop_item(int64_t py_idx)
    {
        return pop_item_unchecked(normalize_index(py_idx));
    }

    size_t List::normalize_index(int64_t py_idx) const
    {
        int64_t n_items = static_cast<int64_t>(size());
        int64_t normalized = py_idx;
        if(normalized < 0)
        {
            normalized += n_items;
        }
        if(normalized < 0 || normalized >= n_items)
        {
            throw std::runtime_error("IndexError: list index out of range");
        }
        return static_cast<size_t>(normalized);
    }

    size_t List::normalize_insertion_index(int64_t py_idx) const
    {
        int64_t n_items = static_cast<int64_t>(size());
        int64_t normalized = py_idx;
        if(normalized < 0)
        {
            normalized += n_items;
        }
        normalized = std::max<int64_t>(0, normalized);
        normalized = std::min(normalized, n_items);
        return static_cast<size_t>(normalized);
    }

}  // namespace cl
