#include "jit/allocation_problem.h"

#include <utility>

namespace cl::jit
{
    void BundleTransferSchedule::add(TransferPoint point, TransferPhase phase,
                                     BundleTransfer transfer)
    {
        for(BundleTransferSet &set: sets_)
        {
            if(set.point == point && set.phase == phase)
            {
                set.transfers.push_back(transfer);
                return;
            }
        }
        sets_.push_back({point, phase, {transfer}});
    }

    LivenessRange minimum_liveness_coverage(LivenessPosition instruction_early,
                                            OccurrenceKind kind,
                                            AccessTiming timing)
    {
        LivenessPosition instruction_late = instruction_early.next();
        LivenessPosition next_instruction_early = instruction_late.next();

        switch(kind)
        {
            case OccurrenceKind::Use:
                return {instruction_early, timing == AccessTiming::Early
                                               ? instruction_late
                                               : next_instruction_early};
            case OccurrenceKind::Def:
                return {timing == AccessTiming::Early ? instruction_early
                                                      : instruction_late,
                        next_instruction_early};
            case OccurrenceKind::Temporary:
                break;
        }
        fatal("temporary occurrence has explicit liveness coverage");
    }

    PreparedAllocationProblem::PreparedAllocationProblem(
        std::vector<BlockLivenessRange> block_ranges,
        std::vector<Occurrence> occurrences,
        std::vector<FixedLocationConstraint> fixed_constraints,
        std::vector<LiveRange> live_ranges, std::vector<LiveBundle> bundles,
        std::vector<ClobberReservation> clobbers,
        std::vector<BundleAffinity> bundle_affinities)
        : block_ranges_(std::move(block_ranges)),
          occurrences_(std::move(occurrences)),
          fixed_constraints_(std::move(fixed_constraints)),
          live_ranges_(std::move(live_ranges)), bundles_(std::move(bundles)),
          clobbers_(std::move(clobbers)),
          bundle_affinities_(std::move(bundle_affinities))
    {
    }

}  // namespace cl::jit
