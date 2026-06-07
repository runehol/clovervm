#include "builtin_types/list.h"
#include "builtin_types/list_iterator.h"
#include "builtin_types/slice.h"
#include "builtin_types/string_builder.h"
#include "builtin_types/tuple.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "object_model/typed_value.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <algorithm>
#include <iterator>

namespace cl
{
    static Value native_list_repr(ThreadState *thread, Value self)
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

    static Value native_list_len(ThreadState *thread, Value self)
    {
        if(!can_convert_to<List>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"list.__len__ expects a list receiver");
        }

        return Value::from_smi(
            static_cast<int64_t>(self.get_ptr<List>()->size()));
    }

    static Value native_list_iter(ThreadState *thread, Value self)
    {
        TValue<List> list = CL_TRY(TValue<List>::from_value_or_raise(
            self, L"TypeError", L"list.__iter__ expects a list receiver"));
        return make_object_value<ListIterator>(list).raw_value();
    }

    static Value require_list_receiver(Value self, const wchar_t *method_name)
    {
        if(!can_convert_to<List>(self))
        {
            std::wstring message = L"list.";
            message += method_name;
            message += L" expects a list receiver";
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", message.c_str());
        }
        return Value::None();
    }

    static Value require_smi_index(Value value, const wchar_t *message,
                                   int64_t &out)
    {
        if(!value.is_smi())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", message);
        }
        out = value.get_smi();
        return Value::None();
    }

    static Value native_list_getitem(ThreadState *thread, Value self,
                                     Value index_value)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"__getitem__"));
        if(can_convert_to<Slice>(index_value))
        {
            TValue<Slice> slice =
                TValue<Slice>::from_value_assumed(index_value);
            if(slice.extract()->step.raw_value().is_none())
            {
                NormalizedNonstridedSlice normalized =
                    CL_TRY(normalize_nonstrided_slice_for_length(
                        thread, slice,
                        static_cast<int64_t>(self.get_ptr<List>()->size())));
                return self.get_ptr<List>()->get_slice(normalized).raw_value();
            }
            NormalizedGeneralSlice normalized =
                CL_TRY(normalize_general_slice_for_length(
                    thread, slice,
                    static_cast<int64_t>(self.get_ptr<List>()->size())));
            return self.get_ptr<List>()->get_slice(normalized).raw_value();
        }
        int64_t py_idx = 0;
        CL_PROPAGATE_EXCEPTION(require_smi_index(
            index_value, L"list indices must be integers or slices", py_idx));
        return self.get_ptr<List>()->get_item(py_idx);
    }

    static Value native_list_setitem(ThreadState *thread, Value self,
                                     Value index_value, Value value)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"__setitem__"));
        int64_t py_idx = 0;
        CL_PROPAGATE_EXCEPTION(require_smi_index(
            index_value, L"list indices must be integers", py_idx));
        return self.get_ptr<List>()->set_item(py_idx, value);
    }

    static Value native_list_delitem(ThreadState *thread, Value self,
                                     Value index_value)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"__delitem__"));
        int64_t py_idx = 0;
        CL_PROPAGATE_EXCEPTION(require_smi_index(
            index_value, L"list indices must be integers", py_idx));
        CL_PROPAGATE_EXCEPTION(self.get_ptr<List>()->pop_item(py_idx));
        return Value::None();
    }

    static Value trusted_list_getitem_smi_handler(ThreadState *thread,
                                                  Value self, Value index_value)
    {
        (void)thread;
        return self.get_ptr<List>()->get_item(index_value.get_smi());
    }

    static Value trusted_list_setitem_smi_handler(ThreadState *thread,
                                                  Value self, Value index_value,
                                                  Value value)
    {
        (void)thread;
        return self.get_ptr<List>()->set_item(index_value.get_smi(), value);
    }

    static Value trusted_list_delitem_smi_handler(ThreadState *thread,
                                                  Value self, Value index_value)
    {
        (void)thread;
        CL_PROPAGATE_EXCEPTION(
            self.get_ptr<List>()->pop_item(index_value.get_smi()));
        return Value::None();
    }

    static TrustedHandlerResolution
    resolve_trusted_list_getitem_handler(VirtualMachine *vm,
                                         ShapeKey container_key,
                                         ShapeKey key_key, ShapeKey unused)
    {
        (void)unused;
        TrustedHandlerResolution resolution;
        if(vm->shape_for_key(container_key)->get_class() == vm->list_class() &&
           key_key == ShapeKey::from_value(Value::from_smi(0)))
        {
            resolution.arity = TrustedHandlerArity::Binary;
            resolution.binary = trusted_list_getitem_smi_handler;
        }
        return resolution;
    }

    static TrustedHandlerResolution
    resolve_trusted_list_setitem_handler(VirtualMachine *vm,
                                         ShapeKey container_key,
                                         ShapeKey key_key, ShapeKey unused)
    {
        (void)unused;
        TrustedHandlerResolution resolution;
        if(vm->shape_for_key(container_key)->get_class() == vm->list_class() &&
           key_key == ShapeKey::from_value(Value::from_smi(0)))
        {
            resolution.arity = TrustedHandlerArity::Ternary;
            resolution.ternary = trusted_list_setitem_smi_handler;
        }
        return resolution;
    }

    static TrustedHandlerResolution
    resolve_trusted_list_delitem_handler(VirtualMachine *vm,
                                         ShapeKey container_key,
                                         ShapeKey key_key, ShapeKey unused)
    {
        (void)unused;
        TrustedHandlerResolution resolution;
        if(vm->shape_for_key(container_key)->get_class() == vm->list_class() &&
           key_key == ShapeKey::from_value(Value::from_smi(0)))
        {
            resolution.arity = TrustedHandlerArity::Binary;
            resolution.binary = trusted_list_delitem_smi_handler;
        }
        return resolution;
    }

    static size_t normalize_list_search_bound(int64_t py_idx, size_t size)
    {
        int64_t normalized = py_idx;
        int64_t n_items = static_cast<int64_t>(size);
        if(normalized < 0)
        {
            normalized += n_items;
        }
        normalized = std::max<int64_t>(0, normalized);
        normalized = std::min<int64_t>(normalized, n_items);
        return static_cast<size_t>(normalized);
    }

    static Value native_list_append(ThreadState *thread, Value self,
                                    Value value)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"append"));
        self.get_ptr<List>()->append(value);
        return Value::None();
    }

    static Value native_list_clear(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"clear"));
        self.get_ptr<List>()->clear();
        return Value::None();
    }

    static Value native_list_copy(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"copy"));
        return self.get_ptr<List>()->copy().raw_value();
    }

    static Value native_list_extend(ThreadState *thread, Value self,
                                    Value other)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"extend"));
        List *list = self.get_ptr<List>();
        if(can_convert_to<List>(other))
        {
            list->extend_from_list(other.get_ptr<List>());
            return Value::None();
        }
        if(can_convert_to<Tuple>(other))
        {
            list->extend_from_tuple(other.get_ptr<Tuple>());
            return Value::None();
        }
        return active_thread()->set_pending_builtin_exception_string(
            L"TypeError", L"list.extend expects a list or tuple");
    }

    static Value native_list_count(ThreadState *thread, Value self,
                                   Value needle)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"count"));
        return Value::from_smi(self.get_ptr<List>()->count(needle));
    }

    static Value native_list_index(ThreadState *thread, Value self,
                                   Value needle, Value start_value,
                                   Value stop_value)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"index"));
        int64_t start_py_idx = 0;
        int64_t stop_py_idx = 0;
        CL_PROPAGATE_EXCEPTION(require_smi_index(
            start_value, L"list indices must be integers", start_py_idx));
        CL_PROPAGATE_EXCEPTION(require_smi_index(
            stop_value, L"list indices must be integers", stop_py_idx));

        return self.get_ptr<List>()->index(needle, start_py_idx, stop_py_idx);
    }

    static Value native_list_insert(ThreadState *thread, Value self,
                                    Value index_value, Value value)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"insert"));
        int64_t py_idx = 0;
        CL_PROPAGATE_EXCEPTION(require_smi_index(
            index_value, L"list indices must be integers", py_idx));
        self.get_ptr<List>()->insert_item(py_idx, value);
        return Value::None();
    }

    static Value native_list_pop(ThreadState *thread, Value self,
                                 Value index_value)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"pop"));
        int64_t py_idx = 0;
        CL_PROPAGATE_EXCEPTION(require_smi_index(
            index_value, L"list indices must be integers", py_idx));
        return self.get_ptr<List>()->pop_item(py_idx);
    }

    static Value native_list_remove(ThreadState *thread, Value self,
                                    Value needle)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"remove"));
        return self.get_ptr<List>()->remove(needle);
    }

    static Value native_list_reverse(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_list_receiver(self, L"reverse"));
        self.get_ptr<List>()->reverse();
        return Value::None();
    }

    static Value native_list_add(ThreadState *thread, Value left, Value right)
    {
        if(!can_convert_to<List>(left) || !can_convert_to<List>(right))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"can only concatenate list to list");
        }
        return left.get_ptr<List>()->concat(right.get_ptr<List>()).raw_value();
    }

    static TValue<Tuple> list_default_single(VirtualMachine *vm, Value value)
    {
        TValue<Tuple> defaults =
            vm->get_default_thread()->make_object_value<Tuple>(1);
        defaults.extract()->initialize_item_unchecked(0, value);
        return defaults;
    }

    static TValue<Tuple>
    list_default_pair(VirtualMachine *vm, Value first_value, Value second_value)
    {
        TValue<Tuple> defaults =
            vm->get_default_thread()->make_object_value<Tuple>(2);
        defaults.extract()->initialize_item_unchecked(0, first_value);
        defaults.extract()->initialize_item_unchecked(1, second_value);
        return defaults;
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
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Public);
    }

    void install_list_class_methods(VirtualMachine *vm)
    {
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__str__", native_list_repr,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_list_repr,
                                     L"Return repr(self)."),
            builtin_intrinsic_method(L"__len__", native_list_len,
                                     L"Return len(self)."),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__getitem__", native_list_getitem,
                                         L"Return self[index]."),
                resolve_trusted_list_getitem_handler),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__setitem__", native_list_setitem,
                                         L"Set self[index] to value."),
                resolve_trusted_list_setitem_handler),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__delitem__", native_list_delitem,
                                         L"Delete self[index]."),
                resolve_trusted_list_delitem_handler),
            builtin_intrinsic_method(L"__iter__", native_list_iter,
                                     L"Implement iter(self)."),
            builtin_intrinsic_method(L"__add__", native_list_add,
                                     L"Return self+value."),
            builtin_intrinsic_method(L"append", native_list_append,
                                     L"Append object to the end of the list."),
            builtin_intrinsic_method(L"clear", native_list_clear,
                                     L"Remove all items from list."),
            builtin_intrinsic_method(L"copy", native_list_copy,
                                     L"Return a shallow copy of the list."),
            builtin_intrinsic_method(L"extend", native_list_extend,
                                     L"Extend list by appending items."),
            builtin_intrinsic_method(L"count", native_list_count,
                                     L"Return number of occurrences of value."),
            builtin_intrinsic_method(L"insert", native_list_insert,
                                     L"Insert object before index."),
            builtin_intrinsic_method(L"remove", native_list_remove,
                                     L"Remove first occurrence of value."),
            builtin_intrinsic_method(L"reverse", native_list_reverse,
                                     L"Reverse list in place."),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->list_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");

        ClassObject *cls = vm->list_class();
        ShapeFlags class_shape_flags = cls->get_shape()->flags();
        cls->set_shape(cls->get_shape()->clone_with_flags(
            class_shape_flags & ~fixed_attribute_shape_flags()));
        DescriptorFlags method_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        auto install = [&](const wchar_t *name, auto function,
                           Optional<TValue<Tuple>> defaults) {
            bool stored = cls->define_own_property(
                vm->get_or_create_interned_string_value(name),
                unwrap_bootstrap_expected(
                    vm, make_intrinsic_function(vm, function, defaults),
                    "creating intrinsic function")
                    .raw_value(),
                method_flags);
            assert(stored);
            (void)stored;
        };
        install(L"index", native_list_index,
                Optional<TValue<Tuple>>::some(list_default_pair(
                    vm, Value::from_smi(0), Value::from_smi(value_smi_max))));
        install(L"pop", native_list_pop,
                Optional<TValue<Tuple>>::some(
                    list_default_single(vm, Value::from_smi(-1))));
        cls->set_shape(cls->get_shape()->clone_with_flags(class_shape_flags));
    }

    TValue<List> List::copy() const
    {
        TValue<List> result = make_object_value<List>(size());
        for(size_t idx = 0; idx < size(); ++idx)
        {
            result.extract()->set_item_unchecked(idx, item_unchecked(idx));
        }
        return result;
    }

    void List::extend_from_list(const List *other)
    {
        size_t original_size = other->size();
        for(size_t idx = 0; idx < original_size; ++idx)
        {
            append(other->item_unchecked(idx));
        }
    }

    void List::extend_from_tuple(const Tuple *other)
    {
        for(size_t idx = 0; idx < other->size(); ++idx)
        {
            append(other->item_unchecked(idx));
        }
    }

    int64_t List::count(Value needle) const
    {
        int64_t result = 0;
        for(size_t idx = 0; idx < size(); ++idx)
        {
            if(item_unchecked(idx) == needle)
            {
                ++result;
            }
        }
        return result;
    }

    Value List::index(Value needle, int64_t start_py_idx,
                      int64_t stop_py_idx) const
    {
        size_t start = normalize_list_search_bound(start_py_idx, size());
        size_t stop = normalize_list_search_bound(stop_py_idx, size());
        for(size_t idx = start; idx < stop; ++idx)
        {
            if(item_unchecked(idx) == needle)
            {
                return Value::from_smi(static_cast<int64_t>(idx));
            }
        }
        return active_thread()->set_pending_builtin_exception_string(
            L"ValueError", L"list.index(x): x not in list");
    }

    Value List::remove(Value needle)
    {
        for(size_t idx = 0; idx < size(); ++idx)
        {
            if(item_unchecked(idx) == needle)
            {
                (void)pop_item_unchecked(idx);
                return Value::None();
            }
        }
        return active_thread()->set_pending_builtin_exception_string(
            L"ValueError", L"list.remove(x): x not in list");
    }

    void List::reverse()
    {
        size_t left = 0;
        size_t right = size();
        while(left < right)
        {
            --right;
            Value left_value = item_unchecked(left);
            Value right_value = item_unchecked(right);
            set_item_unchecked(left, right_value);
            set_item_unchecked(right, left_value);
            ++left;
        }
    }

    TValue<List> List::concat(const List *other) const
    {
        TValue<List> result = make_object_value<List>(size() + other->size());
        size_t write_idx = 0;
        for(size_t idx = 0; idx < size(); ++idx)
        {
            result.extract()->set_item_unchecked(write_idx++,
                                                 item_unchecked(idx));
        }
        for(size_t idx = 0; idx < other->size(); ++idx)
        {
            result.extract()->set_item_unchecked(write_idx++,
                                                 other->item_unchecked(idx));
        }
        return result;
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

    TValue<List> List::get_slice(const NormalizedNonstridedSlice &slice) const
    {
        TValue<List> result =
            make_object_value<List>(slice.selected_sequence_length);
        for(size_t write_idx = 0; write_idx < slice.selected_sequence_length;
            ++write_idx)
        {
            result.extract()->set_item_unchecked(
                write_idx, item_unchecked(static_cast<size_t>(
                               slice.start + static_cast<int64_t>(write_idx))));
        }
        return result;
    }

    TValue<List> List::get_slice(const NormalizedGeneralSlice &slice) const
    {
        TValue<List> result =
            make_object_value<List>(slice.selected_sequence_length);
        int64_t read_idx = slice.start;
        for(size_t write_idx = 0; write_idx < slice.selected_sequence_length;
            ++write_idx)
        {
            result.extract()->set_item_unchecked(
                write_idx, item_unchecked(static_cast<size_t>(read_idx)));
            read_idx += slice.step;
        }
        return result;
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
