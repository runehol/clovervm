#ifndef CL_STACK_FRAME_H
#define CL_STACK_FRAME_H

#include "cl_value.h"

namespace cl
{
    struct StackFrame
    {
        Value registers[100]; //great advantage to having registers being the first member of StackFrame, as we do frequent indexing arithmetic on these.
        // - otherwise we get extra arithmetic for every access



    };


}

#endif //CL_STACK_FRAME_H
