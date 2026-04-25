#ifndef CL_LIST_H
#define CL_LIST_H

#include "klass.h"
#include "object.h"
#include "owned.h"
#include "value.h"
#include "vm_array.h"
#include <cstddef>
#include <cstdint>

namespace cl
{
    class List : public Object
    {
    public:
        static constexpr Klass klass = Klass(L"List", nullptr);
        static constexpr NativeLayoutId native_layout_id = NativeLayoutId::List;

        List() : Object(native_layout_id, &klass, compact_layout()) {}
        explicit List(size_t size);

        size_t size() const { return items.size(); }
        bool empty() const { return items.empty(); }
        void reserve(size_t capacity) { items.reserve(capacity); }

        Value item_unchecked(size_t idx) const { return items[idx]; }
        void set_item_unchecked(size_t idx, Value value)
        {
            items.set(idx, value);
        }
        void insert_item_unchecked(size_t idx, Value value);
        // Returns a transferred Value; the caller assumes ownership that was
        // previously held by the list slot.
        Value pop_item_unchecked(size_t idx);
        void append(Value value) { items.push_back(value); }

        Value get_item(int64_t py_idx) const;
        void set_item(int64_t py_idx, Value value);
        void insert_item(int64_t py_idx, Value value);
        // Returns a transferred Value; the caller assumes ownership that was
        // previously held by the list slot.
        Value pop_item(int64_t py_idx = -1);

    private:
        size_t normalize_index(int64_t py_idx) const;
        size_t normalize_insertion_index(int64_t py_idx) const;

        ValueArray<Value> items;

    public:
        CL_DECLARE_STATIC_LAYOUT_WITH_VALUES(
            List, items, ValueArray<Value>::embedded_value_count);
    };

}  // namespace cl

#endif  // CL_LIST_H
