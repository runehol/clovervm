#include "virtual_machine.h"
#include "builtin_function.h"
#include "range_iterator.h"
#include "scope.h"
#include "thread_state.h"
#include <stdexcept>

namespace cl
{
    static TValue<CLInt> require_range_integer_arg(Value *parameters,
                                                   size_t index)
    {
        if(!parameters[index].is_integer())
        {
            throw std::runtime_error(
                "TypeError: range() arguments must be integers");
        }
        return TValue<CLInt>(parameters[index]);
    }

    static Value builtin_range(Value *parameters, size_t n_parameters)
    {
        Value start = Value::from_smi(0);
        Value stop = Value::None();
        Value step = Value::from_smi(1);

        switch(n_parameters)
        {
            case 1:
                stop = require_range_integer_arg(parameters, 0);
                break;

            case 2:
                start = require_range_integer_arg(parameters, 0);
                stop = require_range_integer_arg(parameters, 1);
                break;

            case 3:
                start = require_range_integer_arg(parameters, 0);
                stop = require_range_integer_arg(parameters, 1);
                step = require_range_integer_arg(parameters, 2);
                if(step.get_smi() == 0)
                {
                    throw std::runtime_error(
                        "ValueError: range() arg 3 must not be zero");
                }
                break;

            default:
                throw std::runtime_error(
                    "TypeError: wrong number of arguments");
        }

        return ThreadState::get_active()->make_refcounted_value<RangeIterator>(
            TValue<CLInt>(start), TValue<CLInt>(stop), TValue<CLInt>(step));
    }

    VirtualMachine::VirtualMachine()
        : refcounted_global_heap(GlobalHeap::refcounted_heap()),
          interned_global_heap(GlobalHeap::interned_heap()),
          interned_strings(&interned_global_heap)
    {
        // make the main thread
        ThreadState *default_thread = make_new_thread();
        ThreadState::ActivationScope activation_scope(default_thread);
        initialize_builtin_scope();
    }

    VirtualMachine::~VirtualMachine()
    {
        if(!threads.empty())
        {
            ThreadState::ActivationScope activation_scope(threads[0].get());
            range_builtin.clear();
            builtin_scope.clear();
        }
    }

    ThreadState *VirtualMachine::make_new_thread()
    {
        return threads.emplace_back(std::make_unique<ThreadState>(this)).get();
    }

    void VirtualMachine::initialize_builtin_scope()
    {
        builtin_scope =
            refcounted_global_heap.make_global_value<Scope>(Value::None());

        TValue<String> range_name =
            get_or_create_interned_string_value(L"range");
        range_builtin =
            refcounted_global_heap.make_global_value<BuiltinFunction>(
                builtin_range, 1, 3);
        builtin_scope.extract()->set_by_name(range_name, range_builtin);
    }

}  // namespace cl
