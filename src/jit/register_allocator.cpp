#include "jit/register_allocator.h"

#include "jit/register_allocator_internal.h"

#include <utility>

namespace cl::jit
{
    Result<PreparedAllocationProblem, RegisterAllocationError>
    prepare_register_allocation(const ControlFlowGraph &graph,
                                const AllocationConstraints &constraints)
    {
        auto scan = scan_live_ranges(graph, constraints);
        if(!scan)
        {
            return propagate_failure(std::move(scan));
        }
        PreparedAllocationProblem problem =
            build_initial_bundles(std::move(scan).value());
        verify_prepared_allocation(problem);
        return Result<PreparedAllocationProblem, RegisterAllocationError>::ok(
            std::move(problem));
    }

}  // namespace cl::jit
