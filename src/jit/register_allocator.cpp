#include "jit/register_allocator.h"

#include "jit/allocation_materializer.h"
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

    Result<LocationAssignments, RegisterAllocationError>
    allocate_registers(CompilationSession &session, ControlFlowGraph &graph,
                       const AllocationConstraints &constraints)
    {
        auto problem_result = prepare_register_allocation(graph, constraints);
        if(!problem_result)
        {
            return propagate_failure(std::move(problem_result));
        }
        PreparedAllocationProblem problem = std::move(problem_result).value();

        auto allocation_result = assign_bundles(problem, constraints);
        if(!allocation_result)
        {
            return propagate_failure(std::move(allocation_result));
        }
        RegisterAllocationResult allocation =
            std::move(allocation_result).value();

        return materialize_allocation(session, graph, problem, constraints,
                                      allocation);
    }

}  // namespace cl::jit
