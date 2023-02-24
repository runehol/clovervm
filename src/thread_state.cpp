#include "thread_state.h"

namespace cl
{

    thread_local ThreadState *ThreadState::current_thread = nullptr;


    ThreadState::ThreadState(VirtualMachine *_machine)
        : machine(_machine),
          stack(10000)
    {

    }

    ThreadState::~ThreadState() = default;

    Value ThreadState::run(const CodeObject *obj)
    {
        current_thread = this;

        try {
            Value result = run_interpreter(obj, 0);

        } catch()
        {
            current_thread = nullptr;
            throw;
        }

        current_thread = nullptr;
        return result;
    }

    void ThreadState::add_to_active_zero_count_table(Value v)
    {
        ThreadState *ts = ThreadState::get_active();
        zero_count_table.push_back(v);
    }


}
