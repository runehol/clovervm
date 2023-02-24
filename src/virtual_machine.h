#ifndef CL_VIRTUAL_MACHINE_H
#define CL_VIRTUAL_MACHINE_H

#include <vector>
#include <memory>

#include "thread_state.h"
#include "heap.h"

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

        void *allocate_immortal(size_t n_bytes);
        void *allocate_interned(size_t n_bytes);

    private:
        std::vector<std::unique_ptr<ThreadState>> threads;
        GlobalHeap refcounted_global_heap;
        GlobalHeap immortal_global_heap;
        GlobalHeap interned_global_heap;
        std::mutex immortal_heap_mutex;
        std::mutex interned_heap_mutex;
        ThreadLocalHeap immortal_heap;
        ThreadLocalHeap interned_heap;
    };



}

#endif //CL_VIRTUAL_MACHINE_H
