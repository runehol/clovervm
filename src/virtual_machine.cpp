#include "virtual_machine.h"
#include "builtin_function.h"
#include "scope.h"
#include "thread_state.h"
#include <stdexcept>

namespace cl
{

    static Value builtin_range(ThreadState *, const CallArguments &)
    {
        throw std::runtime_error(
            "NotImplementedError: range() is not implemented yet");
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

    VirtualMachine::~VirtualMachine() = default;

    ThreadState *VirtualMachine::make_new_thread()
    {
        return threads.emplace_back(std::make_unique<ThreadState>(this)).get();
    }

    void VirtualMachine::initialize_builtin_scope()
    {
        builtin_scope = Value::from_oop(
            new(refcounted_global_heap.allocate_global(sizeof(Scope)))
                Scope(Value::None()));

        Value range_name = get_or_create_interned_string(L"range");
        Value range_builtin = Value::from_oop(
            new(refcounted_global_heap.allocate_global(sizeof(BuiltinFunction)))
                BuiltinFunction(builtin_range, 1, 1));
        builtin_scope.get_ptr<Scope>()->set_by_name(range_name, range_builtin);
    }

}  // namespace cl
