#include "jit/register_allocator_internal.h"

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
    }  // namespace

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

    std::optional<BundleId>
    split_bundle(std::vector<LiveBundle> &bundles,
                 BundleTransferSchedule &transfers,
                 const PreparedAllocationProblem &problem, BundleId bundle_id,
                 LivenessPosition boundary, TransferPoint transfer_point)
    {
        const LiveBundle &source_bundle = bundles[bundle_id.value()];
        for(OccurrenceId occurrence_id:
            covered_occurrences(source_bundle, problem))
        {
            LivenessRange coverage =
                problem.occurrences()[occurrence_id.value()].minimum_coverage;
            if(coverage.start < boundary && boundary < coverage.end)
            {
                return std::nullopt;
            }
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
        if(left_fragments.empty() || right_fragments.empty())
        {
            return std::nullopt;
        }

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
        if(crosses_boundary)
        {
            transfers.add(transfer_point, TransferPhase::Regular,
                          BundleTransfer{bundle_id, right_id});
        }
        return right_id;
    }

}  // namespace cl::jit
