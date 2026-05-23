#include "list.h"
#include "class_object.h"
#include "exception_propagation.h"
#include "list_iterator.h"
#include "native_function.h"
#include "string_builder.h"
#include "thread_state.h"
#include "typed_value.h"
#include "virtual_machine.h"
#include <algorithm>
#include <iterator>

namespace cl
{
    static Value native_list_repr(Value self)
    {
        if(!can_convert_to<List>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"list.__repr__ expects a list receiver");
        }

        List *list = self.get_ptr<List>();
        StringBuilder builder;
        builder.append_char(L'[');
        for(size_t idx = 0; idx < list->size(); ++idx)
        {
            if(idx != 0)
            {
                builder.append_c_str(L", ");
            }
            CL_PROPAGATE_EXCEPTION(
                builder.append_repr(list->item_unchecked(idx)));
        }
        builder.append_char(L']');
        return builder.finish();
    }

    static Value native_list_len(Value self)
    {
        if(!can_convert_to<List>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"list.__len__ expects a list receiver");
        }

        return Value::from_smi(
            static_cast<int64_t>(self.get_ptr<List>()->size()));
    }

    static Value native_list_iter(Value self)
    {
        TValue<List> list = CL_TRY(TValue<List>::from_value_or_raise(
            self, L"TypeError", L"list.__iter__ expects a list receiver"));
        return make_object_value<ListIterator>(list).raw_value();
    }

    List::List(ClassObject *cls, size_t size) : Object(cls, native_layout)
    {
        items.resize(size, Value::not_present());
    }

    BuiltinClassDefinition make_list_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::List};
        ClassObject *cls = ClassObject::make_builtin_class<List>(
            vm->get_or_create_interned_string_value(L"list"),
            List::native_static_release_count(), nullptr, 0,
            vm->object_class());
        return builtin_class_definition(cls, native_layout_ids);
    }

    void install_list_class_methods(VirtualMachine *vm)
    {
        BuiltinNativeMethod methods[] = {
            builtin_native_method(L"__str__", native_list_repr,
                                  L"Return str(self)."),
            builtin_native_method(L"__repr__", native_list_repr,
                                  L"Return repr(self)."),
            builtin_native_method(L"__len__", native_list_len,
                                  L"Return len(self)."),
            builtin_native_method(L"__iter__", native_list_iter,
                                  L"Implement iter(self)."),
        };
        install_builtin_native_methods(vm, vm->list_class(), methods,
                                       std::size(methods));
    }

    void List::insert_item_unchecked(size_t idx, Value value)
    {
        assert(idx <= size());
        value.assert_not_vm_sentinel();

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
        size_t idx = wrap_index(py_idx);
        CL_PROPAGATE_EXCEPTION(check_index(idx));
        return item_unchecked(idx);
    }

    Value List::set_item(int64_t py_idx, Value value)
    {
        size_t idx = wrap_index(py_idx);
        CL_PROPAGATE_EXCEPTION(check_index(idx));
        set_item_unchecked(idx, value);
        return Value::None();
    }

    void List::insert_item(int64_t py_idx, Value value)
    {
        insert_item_unchecked(normalize_insertion_index(py_idx), value);
    }

    Value List::pop_item(int64_t py_idx)
    {
        size_t idx = wrap_index(py_idx);
        CL_PROPAGATE_EXCEPTION(check_index(idx));
        return pop_item_unchecked(idx);
    }

    size_t List::wrap_index(int64_t py_idx) const
    {
        int64_t n_items = static_cast<int64_t>(size());
        int64_t normalized = py_idx;
        if(normalized < 0)
        {
            normalized += n_items;
        }
        return static_cast<size_t>(normalized);
    }

    Value List::check_index(size_t idx) const
    {
        if(idx >= size())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"IndexError", L"list index out of range");
        }
        return Value::None();
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
