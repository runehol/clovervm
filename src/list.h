#ifndef CL_LIST_H
#define CL_LIST_H

#include "builtin_class_registry.h"
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
        static constexpr NativeLayoutId native_layout_id = NativeLayoutId::List;

        explicit List(ClassObject *cls)
            : Object(cls, native_layout_id, compact_layout())
        {
        }
        List(ClassObject *cls, size_t size);

        size_t size() const { return items.size(); }
        bool empty() const { return items.empty(); }
        void reserve(size_t capacity) { items.reserve(capacity); }

        Value item_unchecked(size_t idx) const { return items[idx]; }
        void set_item_unchecked(size_t idx, Value value)
        {
            value.assert_not_vm_sentinel();
            items.set(idx, value);
        }
        void insert_item_unchecked(size_t idx, Value value);
        // Returns a borrowed Value. Interpreter callers immediately place the
        // result on the stack/accumulator, which keeps it rooted after the list
        // releases its slot ownership.
        Value pop_item_unchecked(size_t idx);
        void append(Value value)
        {
            value.assert_not_vm_sentinel();
            items.push_back(value);
        }

        [[nodiscard]] Value get_item(int64_t py_idx) const;
        [[nodiscard]] Value set_item(int64_t py_idx, Value value);
        void insert_item(int64_t py_idx, Value value);
        // Returns a borrowed Value. Interpreter callers immediately place the
        // result on the stack/accumulator, which keeps it rooted after the list
        // releases its slot ownership.
        [[nodiscard]] Value pop_item(int64_t py_idx = -1);

    private:
        size_t wrap_index(int64_t py_idx) const;
        [[nodiscard]] Value check_index(size_t idx) const;
        size_t normalize_insertion_index(int64_t py_idx) const;

        ValueArray<Value> items;

    public:
        CL_DECLARE_STATIC_LAYOUT_EXTENDS_WITH_VALUES(
            List, Object, ValueArray<Value>::embedded_value_count);
    };

    class VirtualMachine;
    BuiltinClassDefinition make_list_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_LIST_H
