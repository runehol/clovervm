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
            const std::vector<FixedLocationConstraint> &fixed_constraints)
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

        uint64_t bundle_spill_weight(
            const LiveBundle &bundle,
            const std::vector<OccurrenceId> &covered_occurrences,
            const std::vector<Occurrence> &occurrences)
        {
            if(covered_occurrences.size() == 1 &&
               bundle.allocation_priority ==
                   occurrences[covered_occurrences.front().value()]
                       .minimum_coverage.length())
            {
                return bundle.fixed_constraints.empty()
                           ? OrdinaryMinimalSpillWeight
                           : FixedMinimalSpillWeight;
            }

            uint64_t total = 0;
            for(OccurrenceId occurrence_id: covered_occurrences)
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
            return std::max<uint64_t>(1, total / bundle.allocation_priority);
        }
    }  // namespace

    void recompute_bundle_properties(
        LiveBundle &bundle, const std::vector<Occurrence> &occurrences,
        const std::vector<FixedLocationConstraint> &fixed_constraints,
        const std::vector<LiveRange> &live_ranges)
    {
        bundle.fixed_constraints.clear();
        bundle.allocation_priority = 0;
        std::vector<OccurrenceId> covered_occurrences;

        for(const BundleFragment &fragment: bundle.fragments)
        {
            bundle.allocation_priority += fragment.range.length();
            const LiveRange &source = live_ranges[fragment.source.value()];
            for(OccurrenceId occurrence_id: source.occurrences)
            {
                const Occurrence &occurrence =
                    occurrences[occurrence_id.value()];
                if(fragment.range.contains(occurrence.minimum_coverage))
                {
                    covered_occurrences.push_back(occurrence_id);
                }
            }
            for(FixedConstraintId fixed_id: source.fixed_constraints)
            {
                const FixedLocationConstraint &fixed =
                    fixed_constraints[fixed_id.value()];
                if(fragment.range.contains(
                       occurrences[fixed.occurrence.value()].minimum_coverage))
                {
                    bundle.fixed_constraints.push_back(fixed_id);
                }
            }
        }

        std::ranges::sort(covered_occurrences);
        if(std::ranges::adjacent_find(covered_occurrences) !=
           covered_occurrences.end())
        {
            fatal("JIT bundle covers one occurrence more than once");
        }
        std::ranges::sort(bundle.fixed_constraints,
                          FixedConstraintPositionLess(fixed_constraints));
        if(bundle.allocation_priority == 0)
        {
            fatal("JIT bundle has no liveness coverage");
        }
        bundle.spill_weight =
            bundle_spill_weight(bundle, covered_occurrences, occurrences);
    }

    bool bundles_overlap(const LiveBundle &lhs, const LiveBundle &rhs)
    {
        size_t lhs_index = 0;
        size_t rhs_index = 0;
        while(lhs_index < lhs.fragments.size() &&
              rhs_index < rhs.fragments.size())
        {
            LivenessRange lhs_range = lhs.fragments[lhs_index].range;
            LivenessRange rhs_range = rhs.fragments[rhs_index].range;
            if(lhs_range.end <= rhs_range.start)
            {
                ++lhs_index;
            }
            else if(rhs_range.end <= lhs_range.start)
            {
                ++rhs_index;
            }
            else
            {
                return true;
            }
        }
        return false;
    }

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
            bundles.push_back({live_range.register_class,
                               {{live_range.range, live_range_id}},
                               {},
                               0,
                               0});
            recompute_bundle_properties(bundles.back(), scan.occurrences,
                                        scan.fixed_constraints,
                                        scan.live_ranges);
        }

        return PreparedAllocationProblem(
            std::move(scan.block_ranges), std::move(scan.occurrences),
            std::move(scan.fixed_constraints), std::move(scan.live_ranges),
            std::move(bundles), std::move(scan.clobbers));
    }

}  // namespace cl::jit
