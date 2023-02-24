#include "virtual_machine.h"
#include "thread_state.h"

namespace cl
{

    VirtualMachine::VirtualMachine()
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
