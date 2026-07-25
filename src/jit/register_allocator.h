#ifndef CL_JIT_REGISTER_ALLOCATOR_H
#define CL_JIT_REGISTER_ALLOCATOR_H

#include "jit/allocation_constraints.h"
#include "jit/allocation_problem.h"
#include "jit/bundle_register_assignments.h"
#include "util/result.h"

#include <cstdint>
#include <string>

namespace cl::jit
{
    enum class RegisterAllocationError : uint8_t
    {
        UnsupportedSnapshotConsumer,
        UnsupportedSameAsInput,
        RequiresConstraintFixup,
        RequiresSplittingOrSpilling,
    };

    Result<PreparedAllocationProblem, RegisterAllocationError>
    prepare_register_allocation(const ControlFlowGraph &graph,
                                const AllocationConstraints &constraints);

    Result<BundleRegisterAssignments, RegisterAllocationError>
    assign_bundles(const PreparedAllocationProblem &problem,
                   const AllocationConstraints &constraints);

    void verify_prepared_allocation(const PreparedAllocationProblem &problem);
    void
    verify_bundle_assignments(const PreparedAllocationProblem &problem,
                              const AllocationConstraints &constraints,
                              const BundleRegisterAssignments &assignments);

    std::string
    format_prepared_allocation(const PreparedAllocationProblem &problem);
    std::string
    format_bundle_assignments(const BundleRegisterAssignments &assignments);

}  // namespace cl::jit

#endif  // CL_JIT_REGISTER_ALLOCATOR_H
