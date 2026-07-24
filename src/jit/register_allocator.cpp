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
        return Result<PreparedAllocationProblem, RegisterAllocationError>::ok(
            build_initial_bundles(std::move(scan).value()));
    }

}  // namespace cl::jit
