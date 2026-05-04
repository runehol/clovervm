#include "tuple.h"
#include "class_object.h"
#include "exception_propagation.h"
#include "refcount.h"
#include "thread_state.h"
#include "virtual_machine.h"

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

    BuiltinClassDefinition make_tuple_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Tuple};
        ClassObject *cls = ClassObject::make_bootstrap_builtin_class(
            vm->get_or_create_interned_string_value(L"tuple"), 1, nullptr, 0);
        return builtin_class_definition(cls, native_layout_ids);
    }

    void Tuple::initialize_item_unchecked(size_t idx, Value value)
    {
        assert(idx < size());
        value.assert_not_vm_sentinel();
        Value old_value = elements[idx];
        elements[idx] = incref(value);
        decref(old_value);
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
