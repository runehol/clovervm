#ifndef CL_THREAD_STATE_H
#define CL_THREAD_STATE_H

#include <vector>
#include <memory>

namespace cl
{

    class VirtualMachine;

    class ThreadState
    {
    public:
        ThreadState(VirtualMachine *_machine);
        ~ThreadState();

    private:
        VirtualMachine *machine;

    };



}

#endif //CL_THREAD_STATE_H
