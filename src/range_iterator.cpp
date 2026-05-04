#include "range_iterator.h"
#include "class_object.h"
#include "native_function.h"
#include "thread_state.h"
#include "virtual_machine.h"

namespace cl
{
    static Value native_range_iterator_iter(Value self)
    {
        if(!can_convert_to<RangeIterator>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError",
                L"range_iterator.__iter__ expects a range_iterator receiver");
        }
        return self;
    }

    static Value native_range_iterator_next(Value self)
    {
        if(!can_convert_to<RangeIterator>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError",
                L"range_iterator.__next__ expects a range_iterator receiver");
        }

        RangeIterator *iterator = self.get_ptr<RangeIterator>();
        Value current = iterator->current;
        Value stop = iterator->stop;
        Value step = iterator->step;
        if(!current.is_smi() || !stop.is_smi() || !step.is_smi())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"range iterator values must be small integers");
        }

        int64_t current_smi = current.get_smi();
        int64_t stop_smi = stop.get_smi();
        int64_t step_smi = step.get_smi();
        if(step_smi == 0)
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"ValueError", L"range() arg 3 must not be zero");
        }

        bool exhausted =
            step_smi > 0 ? current_smi >= stop_smi : current_smi <= stop_smi;
        if(exhausted)
        {
            return active_thread()->set_pending_stop_iteration_no_value();
        }

        int64_t next_smi = 0;
        if(__builtin_add_overflow(current_smi, step_smi, &next_smi))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"UnimplementedError", L"integer overflow");
        }
        iterator->current = Value::from_smi(next_smi);
        return current;
    }

    BuiltinClassDefinition make_range_iterator_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::RangeIterator};
        BuiltinClassMethod methods[] = {
            {vm->get_or_create_interned_string_value(L"__iter__"),
             make_native_function(vm, native_range_iterator_iter)},
            {vm->get_or_create_interned_string_value(L"__next__"),
             make_native_function(vm, native_range_iterator_next)},
        };
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"range_iterator"), 1,
            methods, std::size(methods), vm->object_class());
        return builtin_class_definition(cls, native_layout_ids);
    }
}  // namespace cl
