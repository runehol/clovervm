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
            ProgramPoint early;
            ProgramPoint late;
        };

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
        std::unordered_map<const Block *, ProgramRange> block_ranges;
        std::unordered_map<const Instruction *, ProgramPoint>
            parameter_positions;
        std::unordered_map<const Instruction *, InstructionPosition>
            instruction_positions;

        ProgramPoint expected_block_start(0);
        for(const BlockProgramRange &block_range: problem.block_ranges())
        {
            if(block_range.block == nullptr ||
               block_range.range.start != expected_block_start ||
               block_range.range.empty())
            {
                fatal("invalid JIT allocator block program range");
            }
            size_t expected_size =
                (block_range.block->instructions().size() + 2) * 2;
            if(block_range.range.length() != expected_size ||
               !block_ranges.emplace(block_range.block, block_range.range)
                    .second)
            {
                fatal("incorrect JIT allocator block program range");
            }

            ProgramPoint entry_after = block_range.range.start.next();
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
                ProgramPoint early(block_range.range.start.value() + 2 +
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
            if(!live_range.range.contains(occurrence.point) ||
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
                        ProgramPoint expected = ProgramPoint(0);
                        auto parameter = parameter_positions.find(instruction);
                        if(parameter != parameter_positions.end())
                        {
                            expected = parameter->second;
                        }
                        else
                        {
                            auto position =
                                instruction_positions.find(instruction);
                            if(position == instruction_positions.end())
                            {
                                fatal("JIT allocator result anchor is outside "
                                      "the graph");
                            }
                            expected =
                                occurrence.point == position->second.early
                                    ? position->second.early
                                    : position->second.late;
                        }
                        if(occurrence.kind != OccurrenceKind::Def ||
                           live_range.origin.kind() !=
                               LiveRangeOrigin::Kind::ProgramValue ||
                           live_range.origin.instruction() != instruction ||
                           occurrence.point != expected)
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
                           (occurrence.point != position->second.early &&
                            occurrence.point != position->second.late) ||
                           occurrence.kind != OccurrenceKind::Use ||
                           live_range.origin.kind() !=
                               LiveRangeOrigin::Kind::ProgramValue ||
                           !instruction_operand_matches(
                               instruction, occurrence.anchor.index(),
                               live_range.origin.instruction()))
                        {
                            fatal("invalid JIT allocator operand occurrence");
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
                           occurrence.point.value() + 2 !=
                               range->second.end.value() ||
                           occurrence.kind != OccurrenceKind::Use ||
                           live_range.origin.kind() !=
                               LiveRangeOrigin::Kind::ProgramValue ||
                           edge->arguments()[argument_index].instruction() !=
                               live_range.origin.instruction())
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
                           occurrence.point != position->second.early ||
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

            ProgramPoint previous = live_range.range.start;
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
                   (!first && occurrence.point < previous))
                {
                    fatal("JIT allocator live-range occurrences are malformed");
                }
                previous = occurrence.point;
                first = false;
            }

            ProgramPoint previous_fixed = live_range.range.start;
            first = true;
            std::unordered_set<size_t> seen_fixed;
            for(FixedConstraintId fixed_id: live_range.fixed_constraints)
            {
                if(fixed_id.value() >= problem.fixed_constraints().size() ||
                   !seen_fixed.insert(fixed_id.value()).second)
                {
                    fatal("JIT allocator live range names no fixed constraint");
                }
                const FixedRegisterConstraint &fixed =
                    problem.fixed_constraints()[fixed_id.value()];
                if(fixed.live_range != live_range_id ||
                   (!first && fixed.point < previous_fixed))
                {
                    fatal("JIT allocator fixed constraints are malformed");
                }
                previous_fixed = fixed.point;
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
            const FixedRegisterConstraint &fixed =
                problem.fixed_constraints()[index];
            if(fixed.live_range.value() >= problem.live_ranges().size() ||
               fixed.occurrence.value() >= problem.occurrences().size())
            {
                fatal("invalid JIT allocator fixed constraint");
            }
            const Occurrence &occurrence =
                problem.occurrences()[fixed.occurrence.value()];
            if(occurrence.live_range != fixed.live_range ||
               occurrence.point != fixed.point ||
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

}  // namespace cl::jit
