#include "thread_state.h"
#include "virtual_machine.h"
#include "interpreter.h"

namespace cl
{

    thread_local ThreadState *ThreadState::current_thread = nullptr;


    ThreadState::ThreadState(VirtualMachine *_machine)
        : machine(_machine),
          refcounted_heap(&machine->get_refcounted_global_heap()),
          stack(10000)
    {

    }

    ThreadState::~ThreadState() = default;

    Value ThreadState::run(const CodeObject *obj)
    {
        current_thread = this;

        try {
            Value result = run_interpreter(obj, 0);
            current_thread = nullptr;
            return result;

        } catch(...)
        {
            current_thread = nullptr;
            throw;
        }
    }

    void ThreadState::add_to_active_zero_count_table(Value v)
    {
        ThreadState *ts = ThreadState::get_active();
        ts->zero_count_table.push_back(v);
    }



}
