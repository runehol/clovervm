#ifndef CL_JIT_BLOCK_EDGE_ID_H
#define CL_JIT_BLOCK_EDGE_ID_H

#include "util/dense_id.h"

namespace cl::jit
{
    class BlockEdge;

    using BlockEdgeId = DenseId<BlockEdge>;
}  // namespace cl::jit

#endif  // CL_JIT_BLOCK_EDGE_ID_H
