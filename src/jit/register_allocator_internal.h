#ifndef CL_JIT_REGISTER_ALLOCATOR_INTERNAL_H
#define CL_JIT_REGISTER_ALLOCATOR_INTERNAL_H

#include "jit/register_allocator.h"

#include <vector>

namespace cl::jit
{
    struct LiveRangeScan
    {
        std::vector<BlockLivenessRange> block_ranges;
        std::vector<Occurrence> occurrences;
        std::vector<FixedLocationConstraint> fixed_constraints;
        std::vector<LiveRange> live_ranges;
        std::vector<ClobberReservation> clobbers;
    };

    Result<LiveRangeScan, RegisterAllocationError>
    scan_live_ranges(const ControlFlowGraph &graph,
                     const AllocationConstraints &constraints);

    PreparedAllocationProblem build_initial_bundles(LiveRangeScan scan);

    void recompute_bundle_properties(
        LiveBundle &bundle, const std::vector<Occurrence> &occurrences,
        const std::vector<FixedLocationConstraint> &fixed_constraints,
        const std::vector<LiveRange> &live_ranges);

    struct LocationConstraintSplit
    {
        std::vector<LiveBundle> bundles;
        BundleTransferSchedule transfers;
    };

    Result<LocationConstraintSplit, RegisterAllocationError>
    split_for_location_constraints(const PreparedAllocationProblem &problem);

    std::optional<BundleId>
    split_bundle(std::vector<LiveBundle> &bundles,
                 BundleTransferSchedule &transfers,
                 const PreparedAllocationProblem &problem, BundleId bundle,
                 LivenessPosition boundary, TransferPoint transfer_point);

}  // namespace cl::jit

#endif  // CL_JIT_REGISTER_ALLOCATOR_INTERNAL_H
