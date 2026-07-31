#include "jit/register_allocator.h"

#include "runtime/fatal.h"

#include <absl/container/flat_hash_map.h>

#include <algorithm>
#include <optional>
#include <unordered_set>
#include <vector>

namespace cl::jit
{
    namespace
    {
        struct InstructionPosition
        {
            const Block *block;
            LivenessPosition early;
            LivenessPosition late;
        };

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

        std::optional<InstructionId>
        instruction_operand_definition(Instruction instruction,
                                       size_t operand_index)
        {
            std::optional<InstructionId> result;
            visit_operand_references(
                instruction, [&](uint32_t index, OperandClass operand_class,
                                 ValueRepresentationRequirement,
                                 InstructionId operand_definition) {
                    if(operand_class != OperandClass::Snapshot &&
                       index == operand_index)
                    {
                        result = operand_definition;
                    }
                });
            return result;
        }

        bool bundle_covers_occurrence(const LiveBundle &bundle,
                                      const PreparedAllocationProblem &problem,
                                      OccurrenceId occurrence_id)
        {
            const Occurrence &occurrence =
                problem.occurrences()[occurrence_id.value()];
            return std::ranges::any_of(
                bundle.fragments, [&](const BundleFragment &fragment) {
                    return fragment.source == occurrence.live_range &&
                           fragment.range.contains(occurrence.minimum_coverage);
                });
        }
    }  // namespace

    void verify_prepared_allocation(const PreparedAllocationProblem &problem)
    {
        absl::flat_hash_map<const Block *, LivenessRange> block_ranges;
        absl::flat_hash_map<InstructionId, LivenessPosition>
            parameter_positions;
        absl::flat_hash_map<InstructionId, InstructionPosition>
            instruction_positions;

        LivenessPosition expected_block_start(0);
        for(const BlockLivenessRange &block_range: problem.block_ranges())
        {
            if(block_range.block == nullptr ||
               block_range.range.start != expected_block_start ||
               block_range.range.empty())
            {
                fatal("invalid JIT allocator block liveness range");
            }
            size_t expected_size =
                (block_range.block->instructions().size() + 2) * 2;
            if(block_range.range.length() != expected_size ||
               !block_ranges.emplace(block_range.block, block_range.range)
                    .second)
            {
                fatal("incorrect JIT allocator block liveness range");
            }

            LivenessPosition entry_after = block_range.range.start.next();
            for(Instruction parameter: block_range.block->parameters())
            {
                if(!parameter_positions.emplace(parameter.id(), entry_after)
                        .second)
                {
                    fatal("duplicate JIT allocator block parameter");
                }
            }
            for(size_t index = 0;
                index < block_range.block->instructions().size(); ++index)
            {
                LivenessPosition early(block_range.range.start.value() + 2 +
                                       index * 2);
                Instruction instruction =
                    block_range.block->instruction_at(index);
                if(!instruction_positions
                        .emplace(instruction.id(),
                                 InstructionPosition{block_range.block, early,
                                                     early.next()})
                        .second)
                {
                    fatal("duplicate JIT allocator instruction");
                }
            }
            expected_block_start = block_range.range.end;
        }

        absl::flat_hash_map<InstructionId, LiveRangeId> program_value_ranges;
        for(const Occurrence &occurrence: problem.occurrences())
        {
            if(occurrence.anchor.kind() !=
               OccurrenceAnchor::Kind::InstructionResult)
            {
                continue;
            }
            if(occurrence.live_range.value() >= problem.live_ranges().size() ||
               !program_value_ranges
                    .emplace(occurrence.anchor.instruction_id(),
                             occurrence.live_range)
                    .second)
            {
                fatal("invalid JIT allocator result range mapping");
            }
        }

        for(size_t index = 0; index < problem.occurrences().size(); ++index)
        {
            const Occurrence &occurrence = problem.occurrences()[index];
            if(occurrence.live_range.value() >= problem.live_ranges().size())
            {
                fatal("JIT allocator occurrence names no live range");
            }
            const LiveRange &live_range =
                problem.live_ranges()[occurrence.live_range.value()];
            if(!live_range.range.contains(occurrence.minimum_coverage) ||
               !occurrence.minimum_coverage.contains(occurrence.position))
            {
                fatal("invalid JIT allocator occurrence");
            }

            switch(occurrence.anchor.kind())
            {
                case OccurrenceAnchor::Kind::InstructionResult:
                    {
                        InstructionId instruction =
                            occurrence.anchor.instruction_id();
                        LivenessPosition expected = LivenessPosition(0);
                        auto parameter = parameter_positions.find(instruction);
                        if(parameter != parameter_positions.end())
                        {
                            expected = parameter->second;
                            if(occurrence.minimum_coverage !=
                               LivenessRange{expected, expected.next()})
                            {
                                fatal("JIT allocator block parameter is not "
                                      "live at block entry");
                            }
                        }
                        else
                        {
                            auto position =
                                instruction_positions.find(instruction);
                            if(position == instruction_positions.end() ||
                               (occurrence.position != position->second.early &&
                                occurrence.position != position->second.late))
                            {
                                fatal("JIT allocator result anchor is outside "
                                      "the graph");
                            }
                            expected = occurrence.position;
                            AccessTiming timing =
                                expected == position->second.early
                                    ? AccessTiming::Early
                                    : AccessTiming::Late;
                            if(occurrence.minimum_coverage !=
                               minimum_liveness_coverage(position->second.early,
                                                         occurrence.kind,
                                                         timing))
                            {
                                fatal("JIT allocator result has insufficient "
                                      "liveness coverage");
                            }
                        }
                        Instruction definition =
                            live_range.block->storage()->instruction(
                                instruction);
                        ResultDefinitionKind definition_kind =
                            instruction_kind_metadata(definition.kind())
                                .result_definition_kind;
                        bool valid_definition =
                            definition_kind == ResultDefinitionKind::Def
                                ? occurrence.kind == OccurrenceKind::Def &&
                                      live_range.origin.kind() ==
                                          LiveRangeOrigin::Kind::ProgramValue &&
                                      live_range.origin.instruction_id() ==
                                          instruction
                                : definition_kind ==
                                          ResultDefinitionKind::ForwardingDef &&
                                      occurrence.kind ==
                                          OccurrenceKind::ForwardingDef &&
                                      live_range.origin.kind() ==
                                          LiveRangeOrigin::Kind::ProgramValue;
                        if(!valid_definition || occurrence.position != expected)
                        {
                            fatal("invalid JIT allocator result occurrence");
                        }
                        break;
                    }
                case OccurrenceAnchor::Kind::InstructionOperand:
                    {
                        InstructionId instruction =
                            occurrence.anchor.instruction_id();
                        auto position = instruction_positions.find(instruction);
                        std::optional<InstructionId> definition =
                            instruction_operand_definition(
                                live_range.block->storage()->instruction(
                                    instruction),
                                occurrence.anchor.index());
                        auto definition_range =
                            definition.has_value()
                                ? program_value_ranges.find(*definition)
                                : program_value_ranges.end();
                        if(position == instruction_positions.end() ||
                           position->second.block != live_range.block ||
                           (occurrence.position != position->second.early &&
                            occurrence.position != position->second.late) ||
                           occurrence.kind != OccurrenceKind::Use ||
                           live_range.origin.kind() !=
                               LiveRangeOrigin::Kind::ProgramValue ||
                           definition_range == program_value_ranges.end() ||
                           definition_range->second != occurrence.live_range)
                        {
                            fatal("invalid JIT allocator operand occurrence");
                        }
                        AccessTiming timing =
                            occurrence.position == position->second.early
                                ? AccessTiming::Early
                                : AccessTiming::Late;
                        if(occurrence.minimum_coverage !=
                           minimum_liveness_coverage(position->second.early,
                                                     OccurrenceKind::Use,
                                                     timing))
                        {
                            fatal("JIT allocator operand has insufficient "
                                  "liveness coverage");
                        }
                        break;
                    }
                case OccurrenceAnchor::Kind::BlockEdgeArgument:
                    {
                        const BlockEdge *edge = occurrence.anchor.block_edge();
                        size_t argument_index = occurrence.anchor.index();
                        auto range = block_ranges.find(live_range.block);
                        auto definition_range =
                            edge != nullptr &&
                                    argument_index < edge->arguments().size()
                                ? program_value_ranges.find(
                                      edge->arguments()[argument_index]
                                          .instruction_id())
                                : program_value_ranges.end();
                        if(edge == nullptr ||
                           edge->source() != live_range.block ||
                           argument_index >= edge->arguments().size() ||
                           range == block_ranges.end() ||
                           occurrence.position.value() + 2 !=
                               range->second.end.value() ||
                           occurrence.kind != OccurrenceKind::Use ||
                           live_range.origin.kind() !=
                               LiveRangeOrigin::Kind::ProgramValue ||
                           definition_range == program_value_ranges.end() ||
                           definition_range->second != occurrence.live_range ||
                           occurrence.minimum_coverage !=
                               LivenessRange{occurrence.position,
                                             occurrence.position.next()})
                        {
                            fatal("invalid JIT allocator edge occurrence");
                        }
                        break;
                    }
                case OccurrenceAnchor::Kind::InstructionTemporary:
                    {
                        InstructionId instruction =
                            occurrence.anchor.instruction_id();
                        auto position = instruction_positions.find(instruction);
                        if(position == instruction_positions.end() ||
                           position->second.block != live_range.block ||
                           occurrence.kind != OccurrenceKind::Temporary ||
                           occurrence.position != position->second.early ||
                           live_range.origin.kind() !=
                               LiveRangeOrigin::Kind::Temporary ||
                           live_range.origin.instruction_id() != instruction ||
                           live_range.origin.temporary_index() !=
                               occurrence.anchor.index() ||
                           occurrence.minimum_coverage !=
                               LivenessRange{position->second.early,
                                             position->second.late.next()} ||
                           live_range.range.start != position->second.early ||
                           live_range.range.end != position->second.late.next())
                        {
                            fatal("invalid JIT allocator temporary occurrence");
                        }
                        break;
                    }
            }
        }

        std::vector<bool> bundled(problem.live_ranges().size(), false);
        for(size_t index = 0; index < problem.live_ranges().size(); ++index)
        {
            LiveRangeId live_range_id(index);
            const LiveRange &live_range = problem.live_ranges()[index];
            auto block_range = block_ranges.find(live_range.block);
            if(block_range == block_ranges.end() ||
               !block_range->second.contains(live_range.range) ||
               live_range.range.empty() || live_range.occurrences.empty())
            {
                fatal("invalid JIT allocator live range");
            }

            LivenessPosition previous = live_range.range.start;
            bool first = true;
            std::unordered_set<size_t> seen_occurrences;
            for(OccurrenceId occurrence_id: live_range.occurrences)
            {
                if(occurrence_id.value() >= problem.occurrences().size() ||
                   !seen_occurrences.insert(occurrence_id.value()).second)
                {
                    fatal("JIT allocator live range names no occurrence");
                }
                const Occurrence &occurrence =
                    problem.occurrences()[occurrence_id.value()];
                if(occurrence.live_range != live_range_id ||
                   (!first && occurrence.position < previous))
                {
                    fatal("JIT allocator live-range occurrences are malformed");
                }
                previous = occurrence.position;
                first = false;
            }

            LivenessPosition previous_fixed = live_range.range.start;
            first = true;
            std::unordered_set<size_t> seen_fixed;
            for(FixedConstraintId fixed_id: live_range.fixed_constraints)
            {
                if(fixed_id.value() >= problem.fixed_constraints().size() ||
                   !seen_fixed.insert(fixed_id.value()).second)
                {
                    fatal("JIT allocator live range names no fixed constraint");
                }
                const FixedLocationConstraint &fixed =
                    problem.fixed_constraints()[fixed_id.value()];
                if(fixed.live_range != live_range_id ||
                   (!first && fixed.position < previous_fixed))
                {
                    fatal("JIT allocator fixed constraints are malformed");
                }
                previous_fixed = fixed.position;
                first = false;
            }
        }

        for(const LiveBundle &bundle: problem.bundles())
        {
            if(bundle.fragments.empty())
            {
                fatal("initial JIT allocator bundle is empty");
            }
            std::vector<FixedConstraintId> expected_fixed;
            for(const BundleFragment &fragment: bundle.fragments)
            {
                if(fragment.source.value() >= problem.live_ranges().size() ||
                   bundled[fragment.source.value()])
                {
                    fatal("JIT allocator bundle has invalid source ownership");
                }
                bundled[fragment.source.value()] = true;
                const LiveRange &source =
                    problem.live_ranges()[fragment.source.value()];
                if(fragment.range != source.range ||
                   bundle.register_class != source.register_class)
                {
                    fatal("invalid initial JIT allocator bundle");
                }
                expected_fixed.insert(expected_fixed.end(),
                                      source.fixed_constraints.begin(),
                                      source.fixed_constraints.end());
            }
            for(size_t index = 1; index < bundle.fragments.size(); ++index)
            {
                if(bundle.fragments[index - 1].range.end >
                   bundle.fragments[index].range.start)
                {
                    fatal("initial JIT allocator bundle overlaps itself");
                }
            }
            std::ranges::sort(expected_fixed, [&](FixedConstraintId lhs,
                                                  FixedConstraintId rhs) {
                LivenessPosition lhs_position =
                    problem.fixed_constraints()[lhs.value()].position;
                LivenessPosition rhs_position =
                    problem.fixed_constraints()[rhs.value()].position;
                return lhs_position != rhs_position
                           ? lhs_position < rhs_position
                           : lhs < rhs;
            });
            if(bundle.fixed_constraints != expected_fixed)
            {
                fatal("initial JIT allocator bundle loses fixed constraints");
            }
        }
        if(std::ranges::find(bundled, false) != bundled.end())
        {
            fatal("initial JIT allocator bundles do not cover every range");
        }

        for(size_t index = 0; index < problem.fixed_constraints().size();
            ++index)
        {
            const FixedLocationConstraint &fixed =
                problem.fixed_constraints()[index];
            if(fixed.live_range.value() >= problem.live_ranges().size() ||
               fixed.occurrence.value() >= problem.occurrences().size())
            {
                fatal("invalid JIT allocator fixed constraint");
            }
            const Occurrence &occurrence =
                problem.occurrences()[fixed.occurrence.value()];
            if(occurrence.live_range != fixed.live_range ||
               occurrence.position != fixed.position ||
               !occurrence_is_fixed(
                   problem.live_ranges()[fixed.live_range.value()],
                   fixed.occurrence, problem.fixed_constraints()))
            {
                fatal("JIT allocator fixed constraint does not match its "
                      "occurrence");
            }
        }

        for(const ClobberReservation &clobber: problem.clobbers())
        {
            auto position = instruction_positions.find(clobber.instruction);
            if(position == instruction_positions.end() ||
               clobber.range.start != position->second.late ||
               clobber.range.end != position->second.late.next())
            {
                fatal("invalid JIT allocator clobber reservation");
            }
        }

        for(const BundleAffinity &affinity: problem.bundle_affinities())
        {
            if(affinity.source.value() >= problem.occurrences().size() ||
               affinity.destination.value() >= problem.occurrences().size())
            {
                fatal("invalid JIT allocator bundle affinity");
            }
            const Occurrence &source =
                problem.occurrences()[affinity.source.value()];
            const Occurrence &destination =
                problem.occurrences()[affinity.destination.value()];
            switch(affinity.kind)
            {
                case BundleAffinityKind::BlockEdge:
                    if(affinity.edge == nullptr ||
                       affinity.argument_index >=
                           affinity.edge->arguments().size() ||
                       affinity.argument_index >=
                           affinity.edge->target()->parameters().size() ||
                       source.anchor.kind() !=
                           OccurrenceAnchor::Kind::BlockEdgeArgument ||
                       source.anchor.block_edge() != affinity.edge ||
                       source.anchor.index() != affinity.argument_index ||
                       destination.anchor.kind() !=
                           OccurrenceAnchor::Kind::InstructionResult ||
                       destination.anchor.instruction_id() !=
                           affinity.edge->target()
                               ->parameter_at(affinity.argument_index)
                               .id())
                    {
                        fatal(
                            "JIT allocator block-edge affinity mismatches its "
                            "CFG edge");
                    }
                    break;
                case BundleAffinityKind::SameAsInput:
                    if(affinity.edge != nullptr ||
                       affinity.argument_index != 0 ||
                       source.anchor.kind() !=
                           OccurrenceAnchor::Kind::InstructionOperand ||
                       destination.anchor.kind() !=
                           OccurrenceAnchor::Kind::InstructionResult ||
                       source.anchor.instruction_id() !=
                           destination.anchor.instruction_id())
                    {
                        fatal("JIT allocator same-as-input affinity mismatches "
                              "its instruction");
                    }
                    break;
            }
        }
    }

    void verify_bundle_assignments(const PreparedAllocationProblem &problem,
                                   const AllocationConstraints &constraints,
                                   std::span<const LiveBundle> bundles,
                                   const BundleLocationAssignments &assignments)
    {
        if(assignments.size() != bundles.size())
        {
            fatal("JIT allocator assignment count does not match bundles");
        }

        auto register_class =
            [&](RegisterClass required) -> const RegisterClassDefinition & {
            for(const RegisterClassDefinition &definition:
                constraints.register_classes())
            {
                if(definition.register_class() == required)
                {
                    return definition;
                }
            }
            fatal("JIT allocator assignment has no register class definition");
        };
        auto ranges_overlap = [](LivenessRange lhs, LivenessRange rhs) {
            return lhs.start < rhs.end && rhs.start < lhs.end;
        };

        for(size_t bundle_index = 0; bundle_index < bundles.size();
            ++bundle_index)
        {
            BundleId bundle_id(bundle_index);
            const LiveBundle &bundle = bundles[bundle_index];
            PhysicalLocation location = assignments.location_for(bundle_id);
            if(location.is_register())
            {
                PhysicalRegister reg = location.reg();
                const RegisterClassDefinition &definition =
                    register_class(bundle.register_class);
                if(reg.register_class() != bundle.register_class ||
                   !definition.members().contains(reg))
                {
                    fatal("JIT allocator bundle assigned an incompatible "
                          "register");
                }
            }

            for(FixedConstraintId fixed_id: bundle.fixed_constraints)
            {
                PhysicalLocation fixed =
                    problem.fixed_constraints()[fixed_id.value()].location;
                if(!fixed.aliases(location))
                {
                    fatal("JIT allocator assignment violates fixed constraint");
                }
            }

            for(const BundleFragment &fragment: bundle.fragments)
            {
                if(location.is_register())
                {
                    for(const ClobberReservation &clobber: problem.clobbers())
                    {
                        if(clobber.reg == location.reg() &&
                           ranges_overlap(fragment.range, clobber.range))
                        {
                            fatal(
                                "JIT allocator assignment overlaps a clobber");
                        }
                    }
                }
                else
                {
                    const LiveRange &source =
                        problem.live_ranges()[fragment.source.value()];
                    for(OccurrenceId occurrence_id: source.occurrences)
                    {
                        const Occurrence &occurrence =
                            problem.occurrences()[occurrence_id.value()];
                        if(fragment.range.contains(
                               occurrence.minimum_coverage) &&
                           !occurrence_is_fixed(source, occurrence_id,
                                                problem.fixed_constraints()))
                        {
                            fatal("JIT stack bundle covers a register-only "
                                  "occurrence");
                        }
                    }
                }
            }

            for(size_t other_index = 0; other_index < bundle_index;
                ++other_index)
            {
                BundleId other_id(other_index);
                if(!assignments.location_for(other_id).aliases(location))
                {
                    continue;
                }
                const LiveBundle &other = bundles[other_index];
                if(bundles_overlap(bundle, other))
                {
                    fatal("JIT allocator assignments interfere");
                }
            }
        }
    }

    void verify_register_allocation(const PreparedAllocationProblem &problem,
                                    const AllocationConstraints &constraints,
                                    const RegisterAllocationResult &allocation)
    {
        std::span<const LiveBundle> bundles = allocation.bundles();
        std::vector<std::vector<LivenessRange>> fragments_by_source(
            problem.live_ranges().size());
        for(const LiveBundle &bundle: bundles)
        {
            if(bundle.fragments.empty())
            {
                fatal("JIT allocation contains an empty bundle");
            }
            for(const BundleFragment &fragment: bundle.fragments)
            {
                if(fragment.source.value() >= problem.live_ranges().size() ||
                   fragment.range.empty() ||
                   !problem.live_ranges()[fragment.source.value()]
                        .range.contains(fragment.range))
                {
                    fatal("JIT allocation contains an invalid fragment");
                }
                fragments_by_source[fragment.source.value()].push_back(
                    fragment.range);
            }
        }

        for(size_t source_index = 0;
            source_index < problem.live_ranges().size(); ++source_index)
        {
            std::vector<LivenessRange> &fragments =
                fragments_by_source[source_index];
            std::ranges::sort(fragments,
                              [](LivenessRange lhs, LivenessRange rhs) {
                                  return lhs.start < rhs.start;
                              });
            LivenessPosition expected =
                problem.live_ranges()[source_index].range.start;
            for(LivenessRange fragment: fragments)
            {
                if(fragment.start != expected)
                {
                    fatal("JIT bundle partition loses or overlaps liveness");
                }
                expected = fragment.end;
            }
            if(expected != problem.live_ranges()[source_index].range.end)
            {
                fatal("JIT bundle partition does not cover a live range");
            }
        }

        for(const BundleTransferSet &set: allocation.transfers().sets())
        {
            bool valid_point = false;
            switch(set.point.kind())
            {
                case TransferPoint::Kind::BeforeInstruction:
                    valid_point = true;
                    break;
                case TransferPoint::Kind::BlockEntry:
                case TransferPoint::Kind::BlockExit:
                    valid_point = set.point.block() != nullptr;
                    break;
                case TransferPoint::Kind::BlockEdge:
                    valid_point = set.point.edge() != nullptr;
                    break;
            }
            if(!valid_point || set.transfers.empty())
            {
                fatal("invalid JIT bundle transfer set");
            }
            for(const BundleTransfer &transfer: set.transfers)
            {
                if(transfer.source.value() >= bundles.size() ||
                   transfer.destination.value() >= bundles.size() ||
                   transfer.source == transfer.destination)
                {
                    fatal("invalid JIT bundle transfer");
                }
                bool connected = false;
                if(set.point.kind() == TransferPoint::Kind::BlockEdge)
                {
                    for(const BundleAffinity &affinity:
                        problem.bundle_affinities())
                    {
                        if(affinity.kind == BundleAffinityKind::BlockEdge &&
                           affinity.edge == set.point.edge() &&
                           bundle_covers_occurrence(
                               bundles[transfer.source.value()], problem,
                               affinity.source) &&
                           bundle_covers_occurrence(
                               bundles[transfer.destination.value()], problem,
                               affinity.destination))
                        {
                            connected = true;
                        }
                    }
                }
                else
                {
                    for(const BundleFragment &source:
                        bundles[transfer.source.value()].fragments)
                    {
                        for(const BundleFragment &destination:
                            bundles[transfer.destination.value()].fragments)
                        {
                            if(source.source == destination.source &&
                               source.range.end == destination.range.start)
                            {
                                connected = true;
                            }
                        }
                    }
                }
                if(!connected)
                {
                    fatal("JIT bundle transfer connects no adjacent fragments");
                }
            }
        }

        verify_bundle_assignments(problem, constraints, bundles,
                                  allocation.locations());
    }

}  // namespace cl::jit
