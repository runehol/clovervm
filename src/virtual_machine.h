#ifndef CL_VIRTUAL_MACHINE_H
#define CL_VIRTUAL_MACHINE_H

#include <vector>
#include <memory>

#include "thread_state.h"
#include "heap.h"
#include "intern_store.h"

namespace cl
{
    class ThreadState;

    class VirtualMachine
    {
    public:
        VirtualMachine();
        ~VirtualMachine();

        ThreadState *get_default_thread() { return threads[0].get(); }

        ThreadState *make_new_thread();

        GlobalHeap &get_refcounted_global_heap() { return refcounted_global_heap; }
        GlobalHeap &get_immortal_global_heap() { return immortal_global_heap; }
        GlobalHeap &get_interned_global_heap() { return interned_global_heap; }


    private:
        std::vector<std::unique_ptr<ThreadState>> threads;
        GlobalHeap refcounted_global_heap;
        GlobalHeap immortal_global_heap;
        GlobalHeap interned_global_heap;
    };



}

#endif //CL_VIRTUAL_MACHINE_H
