#include "builtin_types/tuple.h"
#include "builtin_types/string_builder.h"
#include "builtin_types/tuple_iterator.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "object_model/refcount.h"
#include "object_model/typed_value.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <algorithm>
#include <iterator>

namespace cl
{
    static Value native_tuple_repr(ThreadState *thread, Value self)
    {
        if(!can_convert_to<Tuple>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"tuple.__repr__ expects a tuple receiver");
        }

        Tuple *tuple = self.get_ptr<Tuple>();
        StringBuilder builder;
        builder.append_char(L'(');
        for(size_t idx = 0; idx < tuple->size(); ++idx)
        {
            if(idx != 0)
            {
                builder.append_c_str(L", ");
            }
            CL_PROPAGATE_EXCEPTION(
                builder.append_repr(tuple->item_unchecked(idx)));
        }
        if(tuple->size() == 1)
        {
            builder.append_char(L',');
        }
        builder.append_char(L')');
        return builder.finish();
    }

    static Value native_tuple_iter(ThreadState *thread, Value self)
    {
        TValue<Tuple> tuple = CL_TRY(TValue<Tuple>::from_value_or_raise(
            self, L"TypeError", L"tuple.__iter__ expects a tuple receiver"));
        return make_object_value<TupleIterator>(tuple).raw_value();
    }

    static Value native_tuple_len(ThreadState *thread, Value self)
    {
        if(!can_convert_to<Tuple>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"tuple.__len__ expects a tuple receiver");
        }

        return Value::from_smi(
            static_cast<int64_t>(self.get_ptr<Tuple>()->size()));
    }

    static Value native_tuple_add(ThreadState *thread, Value left, Value right)
    {
        if(!can_convert_to<Tuple>(left) || !can_convert_to<Tuple>(right))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"can only concatenate tuple to tuple");
        }

        return left.get_ptr<Tuple>()
            ->concat(right.get_ptr<Tuple>())
            .raw_value();
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

    static size_t normalize_tuple_search_bound(int64_t py_idx, size_t size)
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

    static Value native_tuple_count(ThreadState *thread, Value self,
                                    Value needle)
    {
        if(!can_convert_to<Tuple>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"tuple.count expects a tuple receiver");
        }

        return Value::from_smi(self.get_ptr<Tuple>()->count(needle));
    }

    static Value native_tuple_index(ThreadState *thread, Value self,
                                    Value needle, Value start_value,
                                    Value stop_value)
    {
        if(!can_convert_to<Tuple>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"tuple.index expects a tuple receiver");
        }

        int64_t start_py_idx = 0;
        int64_t stop_py_idx = 0;
        CL_PROPAGATE_EXCEPTION(require_smi_index(
            start_value, L"tuple indices must be integers", start_py_idx));
        CL_PROPAGATE_EXCEPTION(require_smi_index(
            stop_value, L"tuple indices must be integers", stop_py_idx));

        return self.get_ptr<Tuple>()->index(needle, start_py_idx, stop_py_idx);
    }

    static TValue<Tuple> tuple_default_pair(VirtualMachine *vm,
                                            Value first_value,
                                            Value second_value)
    {
        TValue<Tuple> defaults =
            vm->get_default_thread()->make_object_value<Tuple>(2);
        defaults.extract()->initialize_item_unchecked(0, first_value);
        defaults.extract()->initialize_item_unchecked(1, second_value);
        return defaults;
    }

    Tuple::Tuple(BootstrapObjectTag, size_t size)
        : Object(BootstrapObjectTag{}, native_layout),
          size_value(TValue<SMI>::from_smi(static_cast<int64_t>(size)))
    {
        initialize_items(size);
    }

    Tuple::Tuple(ClassObject *cls, size_t size)
        : Object(cls, native_layout),
          size_value(TValue<SMI>::from_smi(static_cast<int64_t>(size)))
    {
        initialize_items(size);
    }

    BuiltinClassDefinition make_tuple_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Tuple};
        ClassObject *cls = ClassObject::make_bootstrap_builtin_class<Tuple>(
            vm->get_or_create_interned_string_value(L"tuple"), 1, nullptr, 0);
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Public);
    }

    void install_tuple_class_methods(VirtualMachine *vm)
    {
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__str__", native_tuple_repr,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_tuple_repr,
                                     L"Return repr(self)."),
            builtin_intrinsic_method(L"__len__", native_tuple_len,
                                     L"Return len(self)."),
            builtin_intrinsic_method(L"__iter__", native_tuple_iter,
                                     L"Implement iter(self)."),
            builtin_intrinsic_method(L"__add__", native_tuple_add,
                                     L"Return self+value."),
            builtin_intrinsic_method(L"count", native_tuple_count,
                                     L"Return number of occurrences of value."),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->tuple_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");

        ClassObject *cls = vm->tuple_class();
        ShapeFlags class_shape_flags = cls->get_shape()->flags();
        cls->set_shape(cls->get_shape()->clone_with_flags(
            class_shape_flags & ~fixed_attribute_shape_flags()));
        DescriptorFlags method_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        bool stored = cls->define_own_property(
            vm->get_or_create_interned_string_value(L"index"),
            unwrap_bootstrap_expected(
                vm,
                make_intrinsic_function(
                    vm, native_tuple_index,
                    Optional<TValue<Tuple>>::some(
                        tuple_default_pair(vm, Value::from_smi(0),
                                           Value::from_smi(value_smi_max)))),
                "creating intrinsic function")
                .raw_value(),
            method_flags);
        assert(stored);
        (void)stored;
        cls->set_shape(cls->get_shape()->clone_with_flags(class_shape_flags));
    }

    Value Tuple::get_item(int64_t py_idx) const
    {
        size_t idx = wrap_index(py_idx);
        CL_PROPAGATE_EXCEPTION(check_index(idx));
        return item_unchecked(idx);
    }

    TValue<Tuple> Tuple::concat(const Tuple *other) const
    {
        TValue<Tuple> result = make_object_value<Tuple>(size() + other->size());
        size_t write_idx = 0;
        for(size_t idx = 0; idx < size(); ++idx)
        {
            result.extract()->initialize_item_unchecked(write_idx++,
                                                        item_unchecked(idx));
        }
        for(size_t idx = 0; idx < other->size(); ++idx)
        {
            result.extract()->initialize_item_unchecked(
                write_idx++, other->item_unchecked(idx));
        }
        return result;
    }

    int64_t Tuple::count(Value needle) const
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

    Value Tuple::index(Value needle, int64_t start_py_idx,
                       int64_t stop_py_idx) const
    {
        size_t start = normalize_tuple_search_bound(start_py_idx, size());
        size_t stop = normalize_tuple_search_bound(stop_py_idx, size());
        for(size_t idx = start; idx < stop; ++idx)
        {
            if(item_unchecked(idx) == needle)
            {
                return Value::from_smi(static_cast<int64_t>(idx));
            }
        }
        return active_thread()->set_pending_builtin_exception_string(
            L"ValueError", L"tuple.index(x): x not in tuple");
    }

    size_t Tuple::wrap_index(int64_t py_idx) const
    {
        int64_t n_items = static_cast<int64_t>(size());
        int64_t normalized = py_idx;
        if(normalized < 0)
        {
            normalized += n_items;
        }
        return static_cast<size_t>(normalized);
    }

    Value Tuple::check_index(size_t idx) const
    {
        if(idx >= size())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"IndexError", L"tuple index out of range");
        }
        return Value::None();
    }

    void Tuple::initialize_items(size_t size)
    {
        for(size_t idx = 0; idx < size; ++idx)
        {
            elements[idx] = Value::not_present();
        }
    }

}  // namespace cl
