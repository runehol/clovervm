#ifndef CL_JIT_SIDE_EXIT_ID_H
#define CL_JIT_SIDE_EXIT_ID_H

#include "util/dense_id.h"

namespace cl::jit
{
    class SideExit;

    using SideExitId = DenseId<SideExit>;
}  // namespace cl::jit

#endif  // CL_JIT_SIDE_EXIT_ID_H
