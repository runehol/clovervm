#include "virtual_machine.h"
#include "thread_state.h"

namespace cl
{

    VirtualMachine::VirtualMachine()
        : refcounted_global_heap(GlobalHeap::refcounted_heap()),
          immortal_global_heap(GlobalHeap::immortal_heap()),
          interned_global_heap(GlobalHeap::interned_heap()),
          immortal_heap(&immortal_global_heap),
          interned_heap(&interned_global_heap)
    {
        // make the main thread
        make_new_thread();
    }

    VirtualMachine::~VirtualMachine() = default;


    ThreadState *VirtualMachine::make_new_thread()
    {
        return threads.emplace_back(std::make_unique<ThreadState>(this)).get();
    }


    void *VirtualMachine::allocate_immortal(size_t n_bytes)
    {
        const std::lock_guard<std::mutex> lock(immortal_heap_mutex);
        return immortal_heap.allocate(n_bytes);
    }

    void *VirtualMachine::allocate_interned(size_t n_bytes)
    {
        const std::lock_guard<std::mutex> lock(interned_heap_mutex);
        return interned_heap.allocate(n_bytes);
    }
}
