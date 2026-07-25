#ifndef CL_JIT_REGISTER_ALLOCATOR_H
#define CL_JIT_REGISTER_ALLOCATOR_H

#include "jit/allocation_constraints.h"
#include "jit/allocation_problem.h"
#include "jit/bundle_location_assignments.h"
#include "util/result.h"

#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace cl::jit
{
    enum class RegisterAllocationError : uint8_t
    {
        UnsupportedSnapshotConsumer,
        UnsupportedSameAsInput,
        UnsupportedTransferPoint,
        RequiresConstraintFixup,
        RequiresParallelTransferResolution,
        RequiresSplittingOrSpilling,
        RequiresTransferScratch,
    };

    class RegisterAllocationResult
    {
    public:
        RegisterAllocationResult(std::vector<LiveBundle> bundles,
                                 BundleLocationAssignments locations,
                                 BundleTransferSchedule transfers)
            : bundles_(std::move(bundles)), locations_(std::move(locations)),
              transfers_(std::move(transfers))
        {
        }

        std::span<const LiveBundle> bundles() const { return bundles_; }
        const BundleLocationAssignments &locations() const
        {
            return locations_;
        }
        const BundleTransferSchedule &transfers() const { return transfers_; }

    private:
        std::vector<LiveBundle> bundles_;
        BundleLocationAssignments locations_;
        BundleTransferSchedule transfers_;
    };

    Result<PreparedAllocationProblem, RegisterAllocationError>
    prepare_register_allocation(const ControlFlowGraph &graph,
                                const AllocationConstraints &constraints);

    Result<RegisterAllocationResult, RegisterAllocationError>
    assign_bundles(const PreparedAllocationProblem &problem,
                   const AllocationConstraints &constraints);

    void verify_prepared_allocation(const PreparedAllocationProblem &problem);
    void
    verify_bundle_assignments(const PreparedAllocationProblem &problem,
                              const AllocationConstraints &constraints,
                              std::span<const LiveBundle> bundles,
                              const BundleLocationAssignments &assignments);
    void verify_register_allocation(const PreparedAllocationProblem &problem,
                                    const AllocationConstraints &constraints,
                                    const RegisterAllocationResult &allocation);

    std::string
    format_prepared_allocation(const PreparedAllocationProblem &problem);
    std::string
    format_bundle_assignments(const BundleLocationAssignments &assignments);
    std::string
    format_register_allocation(const PreparedAllocationProblem &problem,
                               const RegisterAllocationResult &allocation);

}  // namespace cl::jit

#endif  // CL_JIT_REGISTER_ALLOCATOR_H
