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


        void *allocated_refcounted(size_t n_bytes) { return refcounted_heap.allocate(n_bytes); }


    private:
        VirtualMachine *machine;

        ThreadLocalHeap refcounted_heap;

        std::vector<Value> stack;
        std::vector<Value> zero_count_table;




        static thread_local ThreadState *current_thread;

    };



}

#endif //CL_THREAD_STATE_H
