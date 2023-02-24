#include "thread_state.h"

namespace cl
{

    ThreadState::ThreadState(VirtualMachine *_machine)
        : machine(_machine)
    {

    }

    ThreadState::~ThreadState() = default;



}
