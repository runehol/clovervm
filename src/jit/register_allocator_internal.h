#ifndef CL_JIT_REGISTER_ALLOCATOR_INTERNAL_H
#define CL_JIT_REGISTER_ALLOCATOR_INTERNAL_H

#include "jit/register_allocator.h"

#include <span>
#include <vector>

namespace cl::jit
{
    template <typename Id, typename Entry> class IdPositionLess
    {
    public:
        explicit IdPositionLess(std::span<const Entry> entries)
            : entries_(entries)
        {
        }

        bool operator()(Id lhs, Id rhs) const
        {
            LivenessPosition lhs_position = entries_[lhs.value()].position;
            LivenessPosition rhs_position = entries_[rhs.value()].position;
            return lhs_position != rhs_position ? lhs_position < rhs_position
                                                : lhs < rhs;
        }

    private:
        std::span<const Entry> entries_;
    };

    using OccurrencePositionLess = IdPositionLess<OccurrenceId, Occurrence>;
    using FixedConstraintPositionLess =
        IdPositionLess<FixedConstraintId, FixedLocationConstraint>;

    struct LiveRangeScan
    {
        std::vector<BlockLivenessRange> block_ranges;
        std::vector<Occurrence> occurrences;
        std::vector<FixedLocationConstraint> fixed_constraints;
        std::vector<LiveRange> live_ranges;
        std::vector<ClobberReservation> clobbers;
        std::vector<BundleAffinity> bundle_affinities;
    };

    Result<LiveRangeScan, RegisterAllocationError>
    scan_live_ranges(const ControlFlowGraph &graph,
                     const AllocationConstraints &constraints);

    PreparedAllocationProblem build_initial_bundles(LiveRangeScan scan);

    void
    schedule_affinity_transfers(const PreparedAllocationProblem &problem,
                                std::span<const LiveBundle> bundles,
                                const BundleLocationAssignments &assignments,
                                BundleTransferSchedule &transfers);

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

    TransferPoint
    transfer_point_for_occurrence(const PreparedAllocationProblem &problem,
                                  OccurrenceId occurrence);

}  // namespace cl::jit

#endif  // CL_JIT_REGISTER_ALLOCATOR_INTERNAL_H
