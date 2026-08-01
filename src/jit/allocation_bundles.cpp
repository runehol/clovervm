#include "jit/register_allocator_internal.h"

#include <algorithm>
#include <limits>
#include <optional>
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
                                         OccurrenceKind kind, bool fixed,
                                         bool register_required)
        {
            if(!fixed && !register_required)
            {
                return 0;
            }
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

        std::optional<PhysicalLocation> single_fixed_location(
            const LiveBundle &bundle,
            const std::vector<FixedLocationConstraint> &fixed_constraints)
        {
            std::optional<PhysicalLocation> result;
            for(FixedConstraintId id: bundle.fixed_constraints)
            {
                PhysicalLocation location =
                    fixed_constraints[id.value()].location;
                if(result.has_value() && !result->aliases(location))
                {
                    return std::nullopt;
                }
                result = location;
            }
            return result;
        }

        bool can_merge_bundles(
            const LiveBundle &lhs, const LiveBundle &rhs,
            const std::vector<FixedLocationConstraint> &fixed_constraints)
        {
            if(lhs.register_class != rhs.register_class ||
               bundles_overlap(lhs, rhs))
            {
                return false;
            }

            std::optional<PhysicalLocation> lhs_fixed =
                single_fixed_location(lhs, fixed_constraints);
            std::optional<PhysicalLocation> rhs_fixed =
                single_fixed_location(rhs, fixed_constraints);
            if((!lhs.fixed_constraints.empty() && !lhs_fixed.has_value()) ||
               (!rhs.fixed_constraints.empty() && !rhs_fixed.has_value()))
            {
                return false;
            }
            return !lhs_fixed.has_value() || !rhs_fixed.has_value() ||
                   lhs_fixed->aliases(*rhs_fixed);
        }

        uint8_t affinity_kind_rank(BundleAffinityKind kind)
        {
            switch(kind)
            {
                case BundleAffinityKind::SameAsInput:
                    return 0;
                case BundleAffinityKind::BlockEdge:
                    return 1;
            }
            fatal("invalid JIT bundle affinity kind");
        }

        size_t affinity_distance(const BundleAffinity &affinity,
                                 const std::vector<Occurrence> &occurrences)
        {
            size_t source =
                occurrences[affinity.source.value()].position.value();
            size_t destination =
                occurrences[affinity.destination.value()].position.value();
            return source < destination ? destination - source
                                        : source - destination;
        }

        void sort_bundle_affinities(std::vector<BundleAffinity> &affinities,
                                    const std::vector<Occurrence> &occurrences)
        {
            std::ranges::sort(affinities, [&](const BundleAffinity &lhs,
                                              const BundleAffinity &rhs) {
                uint8_t lhs_kind = affinity_kind_rank(lhs.kind);
                uint8_t rhs_kind = affinity_kind_rank(rhs.kind);
                if(lhs_kind != rhs_kind)
                {
                    return lhs_kind < rhs_kind;
                }

                uint64_t lhs_hotness =
                    occurrences[lhs.source.value()].spill_weight +
                    occurrences[lhs.destination.value()].spill_weight;
                uint64_t rhs_hotness =
                    occurrences[rhs.source.value()].spill_weight +
                    occurrences[rhs.destination.value()].spill_weight;
                if(lhs_hotness != rhs_hotness)
                {
                    return lhs_hotness > rhs_hotness;
                }

                size_t lhs_distance = affinity_distance(lhs, occurrences);
                size_t rhs_distance = affinity_distance(rhs, occurrences);
                if(lhs_distance != rhs_distance)
                {
                    return lhs_distance < rhs_distance;
                }

                LivenessPosition lhs_source =
                    occurrences[lhs.source.value()].position;
                LivenessPosition rhs_source =
                    occurrences[rhs.source.value()].position;
                if(lhs_source != rhs_source)
                {
                    return lhs_source < rhs_source;
                }

                LivenessPosition lhs_destination =
                    occurrences[lhs.destination.value()].position;
                LivenessPosition rhs_destination =
                    occurrences[rhs.destination.value()].position;
                if(lhs_destination != rhs_destination)
                {
                    return lhs_destination < rhs_destination;
                }

                if(lhs.argument_index != rhs.argument_index)
                {
                    return lhs.argument_index < rhs.argument_index;
                }

                return lhs.source != rhs.source
                           ? lhs.source < rhs.source
                           : lhs.destination < rhs.destination;
            });
        }

        void merge_bundle_affinities(
            std::vector<LiveBundle> &bundles,
            const std::vector<BundleAffinity> &affinities,
            const std::vector<Occurrence> &occurrences,
            const std::vector<FixedLocationConstraint> &fixed_constraints,
            const std::vector<LiveRange> &live_ranges)
        {
            std::vector<BundleAffinity> ordered_affinities = affinities;
            sort_bundle_affinities(ordered_affinities, occurrences);

            std::vector<BundleId> parent;
            parent.reserve(bundles.size());
            for(size_t index = 0; index < bundles.size(); ++index)
            {
                parent.emplace_back(static_cast<uint32_t>(index));
            }

            auto root = [&](BundleId id) {
                while(parent[id.value()] != id)
                {
                    parent[id.value()] = parent[parent[id.value()].value()];
                    id = parent[id.value()];
                }
                return id;
            };

            for(const BundleAffinity &affinity: ordered_affinities)
            {
                BundleId lhs = root(BundleId(
                    occurrences[affinity.source.value()].live_range.value()));
                BundleId rhs =
                    root(BundleId(occurrences[affinity.destination.value()]
                                      .live_range.value()));
                if(lhs == rhs ||
                   !can_merge_bundles(bundles[lhs.value()],
                                      bundles[rhs.value()], fixed_constraints))
                {
                    continue;
                }

                LiveBundle &destination = bundles[lhs.value()];
                LiveBundle &source = bundles[rhs.value()];
                destination.fragments.insert(destination.fragments.end(),
                                             source.fragments.begin(),
                                             source.fragments.end());
                std::ranges::sort(
                    destination.fragments, [](const BundleFragment &left,
                                              const BundleFragment &right) {
                        return left.range.start < right.range.start;
                    });
                recompute_bundle_properties(destination, occurrences,
                                            fixed_constraints, live_ranges);
                source.fragments.clear();
                parent[rhs.value()] = lhs;
            }

            std::erase_if(bundles, [](const LiveBundle &bundle) {
                return bundle.fragments.empty();
            });
        }

        BundleId
        bundle_covering_occurrence(std::span<const LiveBundle> bundles,
                                   const std::vector<Occurrence> &occurrences,
                                   OccurrenceId occurrence_id)
        {
            const Occurrence &occurrence = occurrences[occurrence_id.value()];
            std::optional<BundleId> result;
            for(size_t index = 0; index < bundles.size(); ++index)
            {
                for(const BundleFragment &fragment: bundles[index].fragments)
                {
                    if(fragment.source == occurrence.live_range &&
                       fragment.range.contains(occurrence.minimum_coverage))
                    {
                        assert(!result.has_value());
                        result = BundleId(static_cast<uint32_t>(index));
                    }
                }
            }
            assert(result.has_value());
            return *result;
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
                                        scan.fixed_constraints),
                    occurrence.register_required);
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
        merge_bundle_affinities(bundles, scan.bundle_affinities,
                                scan.occurrences, scan.fixed_constraints,
                                scan.live_ranges);

        return PreparedAllocationProblem(
            std::move(scan.block_ranges), std::move(scan.occurrences),
            std::move(scan.fixed_constraints), std::move(scan.live_ranges),
            std::move(bundles), std::move(scan.clobbers),
            std::move(scan.bundle_affinities));
    }

    void
    schedule_affinity_transfers(const PreparedAllocationProblem &problem,
                                std::span<const LiveBundle> bundles,
                                const BundleLocationAssignments &assignments,
                                BundleTransferSchedule &transfers)
    {
        for(const BundleAffinity &affinity: problem.bundle_affinities())
        {
            if(affinity.kind != BundleAffinityKind::BlockEdge)
            {
                continue;
            }
            BundleId source = bundle_covering_occurrence(
                bundles, problem.occurrences(), affinity.source);
            BundleId destination = bundle_covering_occurrence(
                bundles, problem.occurrences(), affinity.destination);
            if(source != destination &&
               !assignments.location_for(source).aliases(
                   assignments.location_for(destination)))
            {
                transfers.add(TransferPoint::block_edge(affinity.edge),
                              TransferPhase::Regular,
                              BundleTransfer{source, destination});
            }
        }
    }

}  // namespace cl::jit
