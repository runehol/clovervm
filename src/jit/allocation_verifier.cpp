#include "jit/register_allocator.h"

#include "runtime/fatal.h"

#include <unordered_map>
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

        bool instruction_operand_matches(const Instruction *instruction,
                                         size_t operand_index,
                                         const Instruction *definition)
        {
            bool found = false;
            visit_operand_references(
                *instruction,
                [&](uint32_t index, OperandClass operand_class,
                    ValueRepresentation, Instruction *operand_definition) {
                    if(operand_class != OperandClass::Snapshot &&
                       index == operand_index &&
                       operand_definition == definition)
                    {
                        found = true;
                    }
                });
            return found;
        }
    }  // namespace

    void verify_prepared_allocation(const PreparedAllocationProblem &problem)
    {
        std::unordered_map<const Block *, LivenessRange> block_ranges;
        std::unordered_map<const Instruction *, LivenessPosition>
            parameter_positions;
        std::unordered_map<const Instruction *, InstructionPosition>
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
            for(const Instruction *parameter: block_range.block->parameters())
            {
                if(!parameter_positions.emplace(parameter, entry_after).second)
                {
                    fatal("duplicate JIT allocator block parameter");
                }
            }
            for(size_t index = 0;
                index < block_range.block->instructions().size(); ++index)
            {
                LivenessPosition early(block_range.range.start.value() + 2 +
                                       index * 2);
                const Instruction *instruction =
                    block_range.block->instructions()[index];
                if(!instruction_positions
                        .emplace(instruction,
                                 InstructionPosition{block_range.block, early,
                                                     early.next()})
                        .second)
                {
                    fatal("duplicate JIT allocator instruction");
                }
            }
            expected_block_start = block_range.range.end;
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
            if(!live_range.range.contains(occurrence.position) ||
               occurrence.anchor.owner() == nullptr)
            {
                fatal("invalid JIT allocator occurrence");
            }

            switch(occurrence.anchor.kind())
            {
                case OccurrenceAnchor::Kind::InstructionResult:
                    {
                        const Instruction *instruction =
                            occurrence.anchor.instruction();
                        LivenessPosition expected = LivenessPosition(0);
                        auto parameter = parameter_positions.find(instruction);
                        if(parameter != parameter_positions.end())
                        {
                            expected = parameter->second;
                            if(!live_range.range.contains(
                                   {expected, expected.next()}))
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
                            if(!live_range.range.contains(
                                   minimum_liveness_coverage(
                                       position->second.early,
                                       OccurrenceKind::Def, timing)))
                            {
                                fatal("JIT allocator result has insufficient "
                                      "liveness coverage");
                            }
                        }
                        if(occurrence.kind != OccurrenceKind::Def ||
                           live_range.origin.kind() !=
                               LiveRangeOrigin::Kind::ProgramValue ||
                           live_range.origin.instruction() != instruction ||
                           occurrence.position != expected)
                        {
                            fatal("invalid JIT allocator result occurrence");
                        }
                        break;
                    }
                case OccurrenceAnchor::Kind::InstructionOperand:
                    {
                        const Instruction *instruction =
                            occurrence.anchor.instruction();
                        auto position = instruction_positions.find(instruction);
                        if(position == instruction_positions.end() ||
                           position->second.block != live_range.block ||
                           (occurrence.position != position->second.early &&
                            occurrence.position != position->second.late) ||
                           occurrence.kind != OccurrenceKind::Use ||
                           live_range.origin.kind() !=
                               LiveRangeOrigin::Kind::ProgramValue ||
                           !instruction_operand_matches(
                               instruction, occurrence.anchor.index(),
                               live_range.origin.instruction()))
                        {
                            fatal("invalid JIT allocator operand occurrence");
                        }
                        AccessTiming timing =
                            occurrence.position == position->second.early
                                ? AccessTiming::Early
                                : AccessTiming::Late;
                        if(!live_range.range.contains(minimum_liveness_coverage(
                               position->second.early, OccurrenceKind::Use,
                               timing)))
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
                        if(edge->source() != live_range.block ||
                           argument_index >= edge->arguments().size() ||
                           range == block_ranges.end() ||
                           occurrence.position.value() + 2 !=
                               range->second.end.value() ||
                           occurrence.kind != OccurrenceKind::Use ||
                           live_range.origin.kind() !=
                               LiveRangeOrigin::Kind::ProgramValue ||
                           edge->arguments()[argument_index].instruction() !=
                               live_range.origin.instruction() ||
                           !live_range.range.contains(
                               {occurrence.position,
                                occurrence.position.next()}))
                        {
                            fatal("invalid JIT allocator edge occurrence");
                        }
                        break;
                    }
                case OccurrenceAnchor::Kind::InstructionTemporary:
                    {
                        const Instruction *instruction =
                            occurrence.anchor.instruction();
                        auto position = instruction_positions.find(instruction);
                        if(position == instruction_positions.end() ||
                           position->second.block != live_range.block ||
                           occurrence.kind != OccurrenceKind::Temporary ||
                           occurrence.position != position->second.early ||
                           live_range.origin.kind() !=
                               LiveRangeOrigin::Kind::Temporary ||
                           live_range.origin.instruction() != instruction ||
                           live_range.origin.temporary_index() !=
                               occurrence.anchor.index() ||
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

        if(problem.bundles().size() != problem.live_ranges().size())
        {
            fatal("initial JIT allocator problem is not singleton-bundled");
        }
        for(size_t index = 0; index < problem.bundles().size(); ++index)
        {
            const LiveBundle &bundle = problem.bundles()[index];
            if(bundle.fragments.size() != 1)
            {
                fatal("initial JIT allocator bundle is not a singleton");
            }
            const BundleFragment &fragment = bundle.fragments.front();
            if(fragment.source.value() >= problem.live_ranges().size() ||
               bundled[fragment.source.value()])
            {
                fatal("JIT allocator bundle has invalid source ownership");
            }
            bundled[fragment.source.value()] = true;
            const LiveRange &source =
                problem.live_ranges()[fragment.source.value()];
            if(fragment.range.start != source.range.start ||
               fragment.range.end != source.range.end ||
               bundle.register_class != source.register_class ||
               bundle.fixed_constraints != source.fixed_constraints)
            {
                fatal("invalid initial JIT allocator bundle");
            }
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
    }

    void verify_bundle_assignments(const PreparedAllocationProblem &problem,
                                   const AllocationConstraints &constraints,
                                   const BundleRegisterAssignments &assignments)
    {
        if(assignments.size() != problem.bundles().size())
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

        for(size_t bundle_index = 0; bundle_index < problem.bundles().size();
            ++bundle_index)
        {
            BundleId bundle_id(bundle_index);
            const LiveBundle &bundle = problem.bundles()[bundle_index];
            PhysicalRegister reg = assignments.register_for(bundle_id);
            const RegisterClassDefinition &definition =
                register_class(bundle.register_class);
            if(reg.register_class() != bundle.register_class ||
               !definition.members().contains(reg))
            {
                fatal("JIT allocator bundle assigned an incompatible register");
            }

            for(FixedConstraintId fixed_id: bundle.fixed_constraints)
            {
                AllocationLocation fixed =
                    problem.fixed_constraints()[fixed_id.value()].location;
                if(fixed.is_stack() || fixed.reg() != reg)
                {
                    fatal("JIT allocator assignment violates fixed constraint");
                }
            }

            for(const BundleFragment &fragment: bundle.fragments)
            {
                for(const ClobberReservation &clobber: problem.clobbers())
                {
                    if(clobber.reg == reg &&
                       ranges_overlap(fragment.range, clobber.range))
                    {
                        fatal("JIT allocator assignment overlaps a clobber");
                    }
                }
            }

            for(size_t other_index = 0; other_index < bundle_index;
                ++other_index)
            {
                BundleId other_id(other_index);
                if(assignments.register_for(other_id) != reg)
                {
                    continue;
                }
                const LiveBundle &other = problem.bundles()[other_index];
                if(bundles_overlap(bundle, other))
                {
                    fatal("JIT allocator assignments interfere");
                }
            }
        }
    }

}  // namespace cl::jit
