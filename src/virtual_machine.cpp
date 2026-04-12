#include "virtual_machine.h"
#include "builtin_function.h"
#include "range_iterator.h"
#include "scope.h"
#include "thread_state.h"
#include <stdexcept>

namespace cl
{
    static Value require_range_integer_arg(const CallArguments &args,
                                           uint32_t index)
    {
        if(!args[index].is_integer())
        {
            throw std::runtime_error(
                "TypeError: range() arguments must be integers");
        }
        return args[index];
    }

    static Value builtin_range(ThreadState *thread, const CallArguments &args)
    {
        Value start = Value::from_smi(0);
        Value stop = Value::None();
        Value step = Value::from_smi(1);

        switch(args.n_args)
        {
            case 1:
                stop = require_range_integer_arg(args, 0);
                break;

            case 2:
                start = require_range_integer_arg(args, 0);
                stop = require_range_integer_arg(args, 1);
                break;

            case 3:
                start = require_range_integer_arg(args, 0);
                stop = require_range_integer_arg(args, 1);
                step = require_range_integer_arg(args, 2);
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

        RangeIterator *iterator =
            thread->make_refcounted<RangeIterator>(start, stop, step);
        return Value::from_oop(iterator);
    }

    VirtualMachine::VirtualMachine()
        : refcounted_global_heap(GlobalHeap::refcounted_heap()),
          interned_global_heap(GlobalHeap::interned_heap()),
          interned_strings(&interned_global_heap)
    {
        initialize_builtin_scope();
        // make the main thread
        make_new_thread();
    }

    VirtualMachine::~VirtualMachine()
    {
        if(!threads.empty())
        {
            ThreadState::ActivationScope activation_scope(threads[0].get());
            range_builtin.reset();
            builtin_scope.reset();
        }
    }

    ThreadState *VirtualMachine::make_new_thread()
    {
        return threads.emplace_back(std::make_unique<ThreadState>(this)).get();
    }

    void VirtualMachine::initialize_builtin_scope()
    {
        builtin_scope = Value::from_oop(
            refcounted_global_heap.make_global<Scope>(Value::None()));

        Value range_name = get_or_create_interned_string(L"range");
        range_builtin =
            Value::from_oop(refcounted_global_heap.make_global<BuiltinFunction>(
                builtin_range, 1, 3));
        builtin_scope.get_ptr<Scope>()->set_by_name(range_name, range_builtin);
    }

}  // namespace cl
