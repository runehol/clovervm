#ifndef CL_JIT_SIDE_EXIT_REGION_ID_H
#define CL_JIT_SIDE_EXIT_REGION_ID_H

#include "util/dense_id.h"

namespace cl::jit
{
    class SideExitRegion;

    using SideExitRegionId = DenseId<SideExitRegion>;
}  // namespace cl::jit

#endif  // CL_JIT_SIDE_EXIT_REGION_ID_H
