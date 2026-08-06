#ifndef CL_JIT_ALLOCATION_MATERIALIZER_H
#define CL_JIT_ALLOCATION_MATERIALIZER_H

#include "jit/location_assignments.h"
#include "jit/register_allocator.h"

namespace cl::jit
{
    class CompilationSession;
    class ControlFlowGraph;

    Result<MaterializedAllocation, RegisterAllocationError>
    materialize_allocation(CompilationSession &session, ControlFlowGraph &graph,
                           const PreparedAllocationProblem &problem,
                           const AllocationConstraints &constraints,
                           const RegisterAllocationResult &allocation);

}  // namespace cl::jit

#endif  // CL_JIT_ALLOCATION_MATERIALIZER_H
