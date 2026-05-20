#include "tuple.h"
#include "class_object.h"
#include "exception_propagation.h"
#include "native_function.h"
#include "refcount.h"
#include "string_builder.h"
#include "thread_state.h"
#include "tuple_iterator.h"
#include "typed_value.h"
#include "virtual_machine.h"
#include <iterator>

namespace cl
{
    static Value native_tuple_repr(Value self)
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

    static Value native_tuple_iter(Value self)
    {
        TValue2<Tuple> tuple = CL_TRY(TValue2<Tuple>::from_value_or_raise(
            self, L"TypeError", L"tuple.__iter__ expects a tuple receiver"));
        return make_object_value<TupleIterator>(tuple).raw_value();
    }

    static Value native_tuple_len(Value self)
    {
        if(!can_convert_to<Tuple>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"tuple.__len__ expects a tuple receiver");
        }

        return Value::from_smi(
            static_cast<int64_t>(self.get_ptr<Tuple>()->size()));
    }

    Tuple::Tuple(BootstrapObjectTag, size_t size)
        : Object(BootstrapObjectTag{}, native_layout),
          size_value(TValue2<SMI>::from_smi(static_cast<int64_t>(size)))
    {
        initialize_items(size);
    }

    Tuple::Tuple(ClassObject *cls, size_t size)
        : Object(cls, native_layout),
          size_value(TValue2<SMI>::from_smi(static_cast<int64_t>(size)))
    {
        initialize_items(size);
    }

    BuiltinClassDefinition make_tuple_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Tuple};
        ClassObject *cls = ClassObject::make_bootstrap_builtin_class(
            vm->get_or_create_interned_string_value(L"tuple"), 1, nullptr, 0);
        return builtin_class_definition(cls, native_layout_ids);
    }

    void install_tuple_class_methods(VirtualMachine *vm)
    {
        BuiltinNativeMethod methods[] = {
            builtin_native_method(L"__str__", native_tuple_repr,
                                  L"Return str(self)."),
            builtin_native_method(L"__repr__", native_tuple_repr,
                                  L"Return repr(self)."),
            builtin_native_method(L"__len__", native_tuple_len,
                                  L"Return len(self)."),
            builtin_native_method(L"__iter__", native_tuple_iter,
                                  L"Implement iter(self)."),
        };
        install_builtin_native_methods(vm, vm->tuple_class(), methods,
                                       std::size(methods));
    }

    Value Tuple::get_item(int64_t py_idx) const
    {
        size_t idx = wrap_index(py_idx);
        CL_PROPAGATE_EXCEPTION(check_index(idx));
        return item_unchecked(idx);
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
