#ifndef CL_JIT_BLOCK_ORDER_H
#define CL_JIT_BLOCK_ORDER_H

#include "jit/control_flow_graph.h"

#include <cstdint>
#include <vector>

namespace cl::jit
{
    enum class BlockOrder : uint8_t
    {
        Program,
        Forward,
        Backward,
    };

    std::vector<const Block *> ordered_blocks(const ControlFlowGraph &graph,
                                              BlockOrder order);

}  // namespace cl::jit

#endif  // CL_JIT_BLOCK_ORDER_H
