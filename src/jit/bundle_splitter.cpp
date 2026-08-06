#include "jit/register_allocator_internal.h"

#include <cassert>
#include <optional>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        std::vector<OccurrenceId>
        covered_occurrences(const LiveBundle &bundle,
                            const PreparedAllocationProblem &problem)
        {
            std::vector<OccurrenceId> result;
            for(const BundleFragment &fragment: bundle.fragments)
            {
                const LiveRange &source =
                    problem.live_ranges()[fragment.source.value()];
                for(OccurrenceId occurrence_id: source.occurrences)
                {
                    if(fragment.range.contains(
                           problem.occurrences()[occurrence_id.value()]
                               .minimum_coverage))
                    {
                        result.push_back(occurrence_id);
                    }
                }
            }
            return result;
        }

        bool bundles_have_adjacent_fragments(const LiveBundle &source,
                                             const LiveBundle &destination)
        {
            for(const BundleFragment &source_fragment: source.fragments)
            {
                for(const BundleFragment &destination_fragment:
                    destination.fragments)
                {
                    if(source_fragment.source == destination_fragment.source &&
                       source_fragment.range.end ==
                           destination_fragment.range.start)
                    {
                        return true;
                    }
                }
            }
            return false;
        }
    }  // namespace

    void BundleTransferSchedule::remap_split_bundle(
        BundleId left, BundleId right, std::span<const LiveBundle> bundles)
    {
        assert(left.value() < bundles.size());
        assert(right.value() < bundles.size());
        for(BundleTransferSet &set: sets_)
        {
            if(set.point.kind() == TransferPoint::Kind::BlockEdge)
            {
                continue;
            }
            for(BundleTransfer &transfer: set.transfers)
            {
                if(transfer.source == left)
                {
                    bool left_connected = bundles_have_adjacent_fragments(
                        bundles[left.value()],
                        bundles[transfer.destination.value()]);
                    bool right_connected = bundles_have_adjacent_fragments(
                        bundles[right.value()],
                        bundles[transfer.destination.value()]);
                    if(left_connected == right_connected)
                    {
                        fatal("JIT split cannot remap bundle transfer source");
                    }
                    if(right_connected)
                    {
                        transfer.source = right;
                    }
                }
                if(transfer.destination == left)
                {
                    bool left_connected = bundles_have_adjacent_fragments(
                        bundles[transfer.source.value()],
                        bundles[left.value()]);
                    bool right_connected = bundles_have_adjacent_fragments(
                        bundles[transfer.source.value()],
                        bundles[right.value()]);
                    if(left_connected == right_connected)
                    {
                        fatal("JIT split cannot remap bundle transfer "
                              "destination");
                    }
                    if(right_connected)
                    {
                        transfer.destination = right;
                    }
                }
            }
        }
    }

    TransferPoint
    transfer_point_for_occurrence(const PreparedAllocationProblem &problem,
                                  OccurrenceId occurrence_id)
    {
        const Occurrence &occurrence =
            problem.occurrences()[occurrence_id.value()];
        const LiveRange &source =
            problem.live_ranges()[occurrence.live_range.value()];
        switch(occurrence.anchor.kind())
        {
            case OccurrenceAnchor::Kind::InstructionOperand:
            case OccurrenceAnchor::Kind::InstructionTemporary:
                return TransferPoint::before_instruction(
                    source.block->storage()->instruction(
                        occurrence.anchor.instruction_id()));
            case OccurrenceAnchor::Kind::InstructionResult:
                if(is_block_parameter_kind(
                       source.block->storage()
                           ->instruction(occurrence.anchor.instruction_id())
                           .kind()))
                {
                    return TransferPoint::block_entry(source.block);
                }
                return TransferPoint::before_instruction(
                    source.block->storage()->instruction(
                        occurrence.anchor.instruction_id()));
            case OccurrenceAnchor::Kind::BlockEdgeArgument:
                return TransferPoint::block_exit(
                    occurrence.anchor.block_edge()->source());
        }
        fatal("invalid occurrence anchor for JIT bundle split");
    }

    bool can_split_bundle(const LiveBundle &bundle,
                          const PreparedAllocationProblem &problem,
                          LivenessPosition boundary)
    {
        for(OccurrenceId occurrence_id: covered_occurrences(bundle, problem))
        {
            LivenessRange coverage =
                problem.occurrences()[occurrence_id.value()].minimum_coverage;
            if(coverage.start < boundary && boundary < coverage.end)
            {
                return false;
            }
        }

        bool has_left = false;
        bool has_right = false;
        for(const BundleFragment &fragment: bundle.fragments)
        {
            if(fragment.range.end <= boundary)
            {
                has_left = true;
            }
            else if(fragment.range.start >= boundary)
            {
                has_right = true;
            }
            else
            {
                has_left = true;
                has_right = true;
            }
        }
        return has_left && has_right;
    }

    std::optional<BundleId>
    split_bundle(std::vector<LiveBundle> &bundles,
                 BundleTransferSchedule &transfers,
                 const PreparedAllocationProblem &problem, BundleId bundle_id,
                 LivenessPosition boundary, TransferPoint transfer_point)
    {
        const LiveBundle &source_bundle = bundles[bundle_id.value()];
        if(!can_split_bundle(source_bundle, problem, boundary))
        {
            return std::nullopt;
        }

        std::vector<BundleFragment> left_fragments;
        std::vector<BundleFragment> right_fragments;
        bool crosses_boundary = false;
        for(const BundleFragment &fragment: source_bundle.fragments)
        {
            if(fragment.range.end <= boundary)
            {
                left_fragments.push_back(fragment);
            }
            else if(fragment.range.start >= boundary)
            {
                right_fragments.push_back(fragment);
            }
            else
            {
                crosses_boundary = true;
                left_fragments.push_back(
                    {{fragment.range.start, boundary}, fragment.source});
                right_fragments.push_back(
                    {{boundary, fragment.range.end}, fragment.source});
            }
        }
        assert(!left_fragments.empty() && !right_fragments.empty());

        LiveBundle left{
            source_bundle.register_class, std::move(left_fragments), {}, 0, 0};
        LiveBundle right{
            source_bundle.register_class, std::move(right_fragments), {}, 0, 0};
        recompute_bundle_properties(left, problem.occurrences(),
                                    problem.fixed_constraints(),
                                    problem.live_ranges());
        recompute_bundle_properties(right, problem.occurrences(),
                                    problem.fixed_constraints(),
                                    problem.live_ranges());

        bundles[bundle_id.value()] = std::move(left);
        BundleId right_id(static_cast<uint32_t>(bundles.size()));
        bundles.push_back(std::move(right));
        transfers.remap_split_bundle(bundle_id, right_id, bundles);
        if(crosses_boundary)
        {
            transfers.add(transfer_point, TransferPhase::Regular,
                          BundleTransfer{bundle_id, right_id});
        }
        return right_id;
    }

}  // namespace cl::jit
