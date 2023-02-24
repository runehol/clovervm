#ifndef CL_THREAD_STATE_H
#define CL_THREAD_STATE_H

#include <vector>
#include <memory>

#include "value.h"
#include "heap.h"

namespace cl
{


    class VirtualMachine;
    struct CodeObject;

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

        ThreadLocalHeap &get_refcount_heap() { return refcount_heap; }


    private:
        VirtualMachine *machine;

        ThreadLocalHeap refcount_heap;

        std::vector<Value> stack;
        std::vector<Value> zero_count_table;




        static thread_local ThreadState *current_thread;

    };



}

#endif //CL_THREAD_STATE_H
