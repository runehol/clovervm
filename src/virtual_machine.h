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

        GlobalHeap &get_refcount_global_heap() { return refcount_global_heap; }

        template<typename T>
        T *allocate_immortal()
        {
            return allocate_immortal<T>(sizeof(T));
        }

        template<typename T>
        T *allocate_immortal(size_t n_bytes)
        {
            const std::lock_guard<std::mutex> lock(immortal_heap_mutex);
            return immortal_heap.allocate<T>(n_bytes);
        }

        template<typename T>
        T *allocate_interned()
        {
            return allocate_interned<T>(sizeof(T));
        }

        template<typename T>
        T *allocate_interned(size_t n_bytes)
        {
            const std::lock_guard<std::mutex> lock(interned_heap_mutex);
            return interned_heap.allocate<T>(n_bytes);
        }

    private:
        std::vector<std::unique_ptr<ThreadState>> threads;
        GlobalHeap refcount_global_heap;
        GlobalHeap immortal_global_heap;
        GlobalHeap interned_global_heap;
        std::mutex immortal_heap_mutex;
        std::mutex interned_heap_mutex;
        ThreadLocalHeap immortal_heap;
        ThreadLocalHeap interned_heap;
    };



}

#endif //CL_VIRTUAL_MACHINE_H
