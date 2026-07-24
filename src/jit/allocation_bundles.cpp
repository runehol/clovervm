#include "jit/register_allocator_internal.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        constexpr uint64_t FixedMinimalSpillWeight =
            std::numeric_limits<uint64_t>::max();
        constexpr uint64_t OrdinaryMinimalSpillWeight =
            FixedMinimalSpillWeight - 1;

        bool occurrence_is_fixed(
            const LiveRange &live_range, OccurrenceId occurrence,
            const std::vector<FixedRegisterConstraint> &fixed_constraints)
        {
            for(FixedConstraintId fixed_id: live_range.fixed_constraints)
            {
                if(fixed_constraints[fixed_id.value()].occurrence == occurrence)
                {
                    return true;
                }
            }
            return false;
        }

        uint64_t occurrence_spill_weight(uint32_t loop_depth,
                                         OccurrenceKind kind, bool fixed)
        {
            uint32_t capped_depth = std::min(loop_depth, uint32_t{10});
            uint64_t hot = uint64_t{1000} << (2 * capped_depth);
            uint64_t definition =
                kind == OccurrenceKind::Def ? uint64_t{2000} : uint64_t{0};
            uint64_t requirement = fixed ? uint64_t{2000} : uint64_t{1000};
            return hot + definition + requirement;
        }

        bool is_minimal(const LiveRange &live_range)
        {
            if(live_range.occurrences.size() != 1)
            {
                return false;
            }
            if(live_range.origin.kind() == LiveRangeOrigin::Kind::Temporary)
            {
                return true;
            }
            return live_range.range.length() == 1;
        }

        uint64_t bundle_spill_weight(const LiveRange &live_range,
                                     const std::vector<Occurrence> &occurrences)
        {
            if(is_minimal(live_range))
            {
                return live_range.fixed_constraints.empty()
                           ? OrdinaryMinimalSpillWeight
                           : FixedMinimalSpillWeight;
            }

            uint64_t total = 0;
            for(OccurrenceId occurrence_id: live_range.occurrences)
            {
                uint64_t weight =
                    occurrences[occurrence_id.value()].spill_weight;
                if(weight > std::numeric_limits<uint64_t>::max() - total)
                {
                    total = std::numeric_limits<uint64_t>::max();
                    break;
                }
                total += weight;
            }
            return std::max<uint64_t>(1, total / live_range.range.length());
        }
    }  // namespace

    PreparedAllocationProblem build_initial_bundles(LiveRangeScan scan)
    {
        for(size_t index = 0; index < scan.live_ranges.size(); ++index)
        {
            LiveRangeId live_range_id(index);
            const LiveRange &live_range = scan.live_ranges[index];
            for(OccurrenceId occurrence_id: live_range.occurrences)
            {
                Occurrence &occurrence =
                    scan.occurrences[occurrence_id.value()];
                occurrence.spill_weight = occurrence_spill_weight(
                    live_range.block->loop_depth(), occurrence.kind,
                    occurrence_is_fixed(live_range, occurrence_id,
                                        scan.fixed_constraints));
            }
        }

        std::vector<LiveBundle> bundles;
        bundles.reserve(scan.live_ranges.size());
        for(size_t index = 0; index < scan.live_ranges.size(); ++index)
        {
            LiveRangeId live_range_id(index);
            const LiveRange &live_range = scan.live_ranges[index];
            bundles.push_back(
                {live_range.register_class,
                 {{live_range.range, live_range_id}},
                 live_range.fixed_constraints,
                 live_range.range.length(),
                 bundle_spill_weight(live_range, scan.occurrences)});
        }

        return PreparedAllocationProblem(
            std::move(scan.block_ranges), std::move(scan.occurrences),
            std::move(scan.fixed_constraints), std::move(scan.live_ranges),
            std::move(bundles), std::move(scan.clobbers));
    }

}  // namespace cl::jit
