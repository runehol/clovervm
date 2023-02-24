#include "virtual_machine.h"
#include "thread_state.h"

namespace cl
{

    VirtualMachine::VirtualMachine()
        : refcount_global_heap(GlobalHeap::refcount_heap()),
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
}
