#ifndef CL_LIST_H
#define CL_LIST_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"
#include "object_model/vm_array.h"
#include <cstddef>
#include <cstdint>

namespace cl
{
    class Function;
    class ModuleObject;
    class Tuple;
    struct NormalizedNonstridedSlice;
    struct NormalizedGeneralSlice;

    class List : public Object
    {
    public:
        static constexpr uint32_t kPredefinedInlineSlotCount = 0;

        static constexpr NativeLayoutId native_layout = NativeLayoutId::List;

        explicit List(ClassObject *cls) : Object(cls, native_layout) {}
        List(ClassObject *cls, size_t size);

        size_t size() const { return items.size(); }
        bool empty() const { return items.empty(); }
        void reserve(size_t capacity) { items.reserve(capacity); }
        void clear()
        {
            if(!empty())
                sort_mutated_ = true;
            items.clear();
        }

        Value item_unchecked(size_t idx) const { return items[idx]; }
        void set_item_unchecked(size_t idx, Value value)
        {
            value.assert_not_vm_sentinel();
            sort_mutated_ = true;
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
            sort_mutated_ = true;
            items.push_back(value);
        }
        [[nodiscard]] TValue<List> copy() const;
        [[nodiscard]] TValue<Tuple> to_tuple(ThreadState *thread) const;
        void extend_from_list(const List *other);
        void extend_from_tuple(const Tuple *other);
        void reverse();
        // The trusted Python helper owns the generic algorithm; List owns the
        // detached backing, mutation state, and unconditional restoration.
        [[nodiscard]] Value sort(ThreadState *thread, Value key,
                                 TValue<Function> sort_items, bool reverse);
        [[nodiscard]] TValue<List> concat(const List *other) const;

        [[nodiscard]] Value get_item(int64_t py_idx) const;
        [[nodiscard]] TValue<List>
        get_slice(const NormalizedNonstridedSlice &slice) const;
        [[nodiscard]] TValue<List>
        get_slice(const NormalizedGeneralSlice &slice) const;
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
        // Successful writes set this even if their net size change is zero.
        // Each sort saves/restores the enclosing invocation's mutation state.
        bool sort_mutated_ = false;

    public:
        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(
            List, Object, ValueArray<Value>::embedded_value_count);
        CL_DECLARE_STATIC_OBJECT_SIZE(List);
    };

    class VirtualMachine;
    BuiltinClassDefinition make_list_class(VirtualMachine *vm);
    [[nodiscard]] TValue<List> allocate_default_list(ThreadState *thread,
                                                     TValue<ClassObject> cls);
    void install_list_class_methods(VirtualMachine *vm);
    void install_list_python_methods(VirtualMachine *vm,
                                     ModuleObject *implementation_module);

}  // namespace cl

#endif  // CL_LIST_H
