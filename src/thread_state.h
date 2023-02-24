#ifndef CL_THREAD_STATE_H
#define CL_THREAD_STATE_H

#include <vector>
#include <memory>

#include "value.h"

namespace cl
{


    class VirtualMachine;
    class CodeObject;

    class ThreadState
    {
    public:
        ThreadState(VirtualMachine *_machine);
        ~ThreadState();

        static void add_to_active_zero_count_table(Value v);


        Value run(const CodeObject *obj);

        static ThreadState *get_active()
        {
            assert(current_thread != nullptr);
            return current_thread;
        }

    private:
        VirtualMachine *machine;

        std::vector<Value> stack;

        std::vector<Value> zero_count_table;

        static thread_local ThreadState *current_thread;

    };



}

#endif //CL_THREAD_STATE_H
