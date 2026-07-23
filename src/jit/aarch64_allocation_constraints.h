#ifndef CL_JIT_AARCH64_ALLOCATION_CONSTRAINTS_H
#define CL_JIT_AARCH64_ALLOCATION_CONSTRAINTS_H

#include "jit/allocation_constraints.h"

namespace cl::jit
{
    class ControlFlowGraph;

    AllocationConstraints
    make_aarch64_allocation_constraints(const ControlFlowGraph &graph);

}  // namespace cl::jit

#endif  // CL_JIT_AARCH64_ALLOCATION_CONSTRAINTS_H
