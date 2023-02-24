#ifndef CL_VIRTUAL_MACHINE_H
#define CL_VIRTUAL_MACHINE_H

#include <vector>
#include <memory>

#include "thread_state.h"

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

    private:
        std::vector<std::unique_ptr<ThreadState>> threads;
    };



}

#endif //CL_VIRTUAL_MACHINE_H
