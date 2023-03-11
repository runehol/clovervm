#include "virtual_machine.h"
#include "thread_state.h"

namespace cl
{

    VirtualMachine::VirtualMachine()
        : refcounted_global_heap(GlobalHeap::refcounted_heap()),
          interned_global_heap(GlobalHeap::interned_heap()),
          interned_strings(&interned_global_heap)
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
