#include "jit/register_allocator.h"

#include "runtime/fatal.h"

#include <fmt/format.h>

#include <iterator>
#include <string>
#include <unordered_map>

namespace cl::jit
{
    namespace
    {
        class DumpNames
        {
        public:
            explicit DumpNames(const PreparedAllocationProblem &problem)
            {
                size_t result_index = 0;
                size_t instruction_index = 0;
                size_t edge_index = 0;
                for(size_t block_index = 0;
                    block_index < problem.block_ranges().size(); ++block_index)
                {
                    const Block *block =
                        problem.block_ranges()[block_index].block;
                    blocks_.emplace(block, block_index);
                    for(const Instruction *parameter: block->parameters())
                    {
                        instructions_.emplace(parameter, instruction_index++);
                        results_.emplace(parameter, result_index++);
                        instruction_blocks_.emplace(parameter, block);
                    }
                    for(const Instruction *instruction: block->instructions())
                    {
                        instructions_.emplace(instruction, instruction_index++);
                        instruction_blocks_.emplace(instruction, block);
                        if(instruction->result_class() != ResultClass::None)
                        {
                            results_.emplace(instruction, result_index++);
                        }
                    }
                    for(const BlockEdge *edge: block->block_successor_edges())
                    {
                        edges_.emplace(edge, edge_index++);
                    }
                }
            }

            std::string instruction(const Instruction *instruction) const
            {
                auto result = results_.find(instruction);
                if(result != results_.end())
                {
                    return fmt::format("%{}", result->second);
                }
                return fmt::format("i{}", instructions_.at(instruction));
            }

            size_t block(const Block *block) const { return blocks_.at(block); }
            size_t edge(const BlockEdge *edge) const { return edges_.at(edge); }
            const Block *instruction_block(const Instruction *instruction) const
            {
                return instruction_blocks_.at(instruction);
            }

        private:
            std::unordered_map<const Instruction *, size_t> instructions_;
            std::unordered_map<const Instruction *, size_t> results_;
            std::unordered_map<const Instruction *, const Block *>
                instruction_blocks_;
            std::unordered_map<const Block *, size_t> blocks_;
            std::unordered_map<const BlockEdge *, size_t> edges_;
        };

        std::string format_range(ProgramRange range)
        {
            return fmt::format("[{}, {})", range.start.value(),
                               range.end.value());
        }

        std::string format_anchor(const OccurrenceAnchor &anchor,
                                  const DumpNames &names)
        {
            switch(anchor.kind())
            {
                case OccurrenceAnchor::Kind::InstructionOperand:
                    return fmt::format("operand({}, {})",
                                       names.instruction(anchor.instruction()),
                                       anchor.index());
                case OccurrenceAnchor::Kind::InstructionResult:
                    return fmt::format("result({})",
                                       names.instruction(anchor.instruction()));
                case OccurrenceAnchor::Kind::BlockEdgeArgument:
                    return fmt::format("edge_argument(e{}, {})",
                                       names.edge(anchor.block_edge()),
                                       anchor.index());
                case OccurrenceAnchor::Kind::InstructionTemporary:
                    return fmt::format("temporary({}, {})",
                                       names.instruction(anchor.instruction()),
                                       anchor.index());
            }
            fatal("invalid occurrence anchor in JIT allocator dump");
        }

        const char *register_class_name(RegisterClass register_class)
        {
            switch(register_class)
            {
                case RegisterClass::GPR:
                    return "gpr";
                case RegisterClass::SIMD:
                    return "simd";
                case RegisterClass::Count:
                    break;
            }
            fatal("invalid register class in JIT allocator dump");
        }

        const char *occurrence_kind_name(OccurrenceKind kind)
        {
            switch(kind)
            {
                case OccurrenceKind::Use:
                    return "use";
                case OccurrenceKind::Def:
                    return "def";
                case OccurrenceKind::Temporary:
                    return "temporary";
            }
            fatal("invalid occurrence kind in JIT allocator dump");
        }

        const FixedRegisterConstraint *
        fixed_for_occurrence(const PreparedAllocationProblem &problem,
                             OccurrenceId occurrence_id)
        {
            const FixedRegisterConstraint *result = nullptr;
            for(const FixedRegisterConstraint &fixed:
                problem.fixed_constraints())
            {
                if(fixed.occurrence != occurrence_id)
                {
                    continue;
                }
                if(result != nullptr)
                {
                    fatal("multiple fixed constraints for one occurrence in "
                          "JIT allocator dump");
                }
                result = &fixed;
            }
            return result;
        }

        template <typename Range>
        void print_fixed_constraints(fmt::memory_buffer &out,
                                     const Range &fixed_ids,
                                     const PreparedAllocationProblem &problem)
        {
            fmt::format_to(std::back_inserter(out), "[");
            for(size_t index = 0; index < fixed_ids.size(); ++index)
            {
                if(index != 0)
                {
                    fmt::format_to(std::back_inserter(out), ", ");
                }
                const FixedRegisterConstraint &fixed =
                    problem.fixed_constraints()[fixed_ids[index].value()];
                fmt::format_to(std::back_inserter(out), "o{}:{}{}",
                               fixed.occurrence.value(),
                               register_class_name(fixed.reg.register_class()),
                               fixed.reg.number());
            }
            fmt::format_to(std::back_inserter(out), "]");
        }
    }  // namespace

    std::string
    format_prepared_allocation(const PreparedAllocationProblem &problem)
    {
        DumpNames names(problem);
        fmt::memory_buffer out;
        auto append = [&](fmt::string_view text) {
            fmt::format_to(std::back_inserter(out), "{}", text);
        };

        append("allocation {\n");
        for(const BlockProgramRange &block_range: problem.block_ranges())
        {
            fmt::format_to(
                std::back_inserter(out), "  bb{} {} {{loop_depth = {}}} {{\n",
                names.block(block_range.block), format_range(block_range.range),
                block_range.block->loop_depth());

            append("    occurrences {\n");
            for(size_t index = 0; index < problem.occurrences().size(); ++index)
            {
                OccurrenceId occurrence_id(index);
                const Occurrence &occurrence = problem.occurrences()[index];
                const LiveRange &live_range =
                    problem.live_ranges()[occurrence.live_range.value()];
                if(live_range.block != block_range.block)
                {
                    continue;
                }
                fmt::format_to(std::back_inserter(out),
                               "      {} o{} {} l{} {} {{",
                               occurrence.point.value(), index,
                               occurrence_kind_name(occurrence.kind),
                               occurrence.live_range.value(),
                               format_anchor(occurrence.anchor, names));
                const FixedRegisterConstraint *fixed =
                    fixed_for_occurrence(problem, occurrence_id);
                if(fixed != nullptr)
                {
                    fmt::format_to(
                        std::back_inserter(out), "fixed = {}{}, ",
                        register_class_name(fixed->reg.register_class()),
                        fixed->reg.number());
                }
                fmt::format_to(std::back_inserter(out), "weight = {}}}\n",
                               occurrence.spill_weight);
            }
            append("    }\n\n");

            append("    ranges {\n");
            for(size_t index = 0; index < problem.live_ranges().size(); ++index)
            {
                const LiveRange &live_range = problem.live_ranges()[index];
                if(live_range.block != block_range.block)
                {
                    continue;
                }
                fmt::format_to(std::back_inserter(out),
                               "      l{} {} {} {{origin = ", index,
                               register_class_name(live_range.register_class),
                               format_range(live_range.range));
                if(live_range.origin.kind() ==
                   LiveRangeOrigin::Kind::ProgramValue)
                {
                    append(names.instruction(live_range.origin.instruction()));
                }
                else
                {
                    fmt::format_to(
                        std::back_inserter(out), "temporary({}, {})",
                        names.instruction(live_range.origin.instruction()),
                        live_range.origin.temporary_index());
                }
                append(", occurrences = [");
                for(size_t occurrence_index = 0;
                    occurrence_index < live_range.occurrences.size();
                    ++occurrence_index)
                {
                    if(occurrence_index != 0)
                    {
                        append(", ");
                    }
                    fmt::format_to(
                        std::back_inserter(out), "o{}",
                        live_range.occurrences[occurrence_index].value());
                }
                append("], fixed = ");
                print_fixed_constraints(out, live_range.fixed_constraints,
                                        problem);
                append("}\n");
            }
            append("    }\n");

            bool has_clobbers = false;
            for(const ClobberReservation &clobber: problem.clobbers())
            {
                if(names.instruction_block(clobber.instruction) ==
                   block_range.block)
                {
                    has_clobbers = true;
                    break;
                }
            }
            if(has_clobbers)
            {
                append("\n    clobbers {\n");
                for(const ClobberReservation &clobber: problem.clobbers())
                {
                    if(names.instruction_block(clobber.instruction) !=
                       block_range.block)
                    {
                        continue;
                    }
                    fmt::format_to(
                        std::back_inserter(out), "      {} {}{} {}\n",
                        format_range(clobber.range),
                        register_class_name(clobber.reg.register_class()),
                        clobber.reg.number(),
                        names.instruction(clobber.instruction));
                }
                append("    }\n");
            }
            append("  }\n\n");
        }

        append("  bundles {\n");
        for(size_t index = 0; index < problem.bundles().size(); ++index)
        {
            const LiveBundle &bundle = problem.bundles()[index];
            fmt::format_to(std::back_inserter(out), "    b{} {} [", index,
                           register_class_name(bundle.register_class));
            for(size_t fragment_index = 0;
                fragment_index < bundle.fragments.size(); ++fragment_index)
            {
                if(fragment_index != 0)
                {
                    append(", ");
                }
                const BundleFragment &fragment =
                    bundle.fragments[fragment_index];
                fmt::format_to(std::back_inserter(out), "{}:l{}",
                               format_range(fragment.range),
                               fragment.source.value());
            }
            fmt::format_to(std::back_inserter(out), "] {{fixed = ");
            print_fixed_constraints(out, bundle.fixed_constraints, problem);
            fmt::format_to(std::back_inserter(out),
                           ", priority = {}, spill_weight = {}}}\n",
                           bundle.allocation_priority, bundle.spill_weight);
        }
        append("  }\n");
        append("}\n");
        return {out.data(), out.size()};
    }

}  // namespace cl::jit
