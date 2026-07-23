#include "jit/cfg_verifier.h"

#include "jit/control_flow_graph.h"
#include "jit/instruction.h"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>

#include <algorithm>
#include <cassert>
#include <string>
#include <utility>

namespace cl::jit
{
    namespace
    {
        CfgVerificationResult invalid(std::string message)
        {
            return {false, std::move(message)};
        }

        std::string block_name(const Block *block)
        {
            if(block == nullptr)
            {
                return "null block";
            }
            return "block b" + std::to_string(block->serial().value());
        }

        std::string edge_name(const BlockEdge *edge)
        {
            if(edge == nullptr)
            {
                return "null edge";
            }
            return "block edge e" + std::to_string(edge->serial().value());
        }

        std::string instruction_name(const Instruction *instruction)
        {
            if(instruction == nullptr)
            {
                return "null instruction";
            }
            return "instruction i" +
                   std::to_string(instruction->serial().value());
        }

        bool is_parameter_kind(InstructionKind kind)
        {
            return kind == InstructionKind::Parameter ||
                   kind == InstructionKind::ParameterF64;
        }

        bool is_core_instruction(InstructionKind kind)
        {
            uint8_t levels = static_cast<uint8_t>(
                instruction_kind_metadata(kind).allowed_ir_levels);
            return (levels & static_cast<uint8_t>(IRLevelMask::Core)) != 0;
        }

        size_t expected_successor_count(InstructionKind kind)
        {
            switch(kind)
            {
                case InstructionKind::ConditionalBranch:
                    return 2;
                case InstructionKind::UnconditionalBranch:
                    return 1;
                case InstructionKind::Return:
                    return 0;
                default:
                    break;
            }
            assert(false);
            return 0;
        }
    }  // namespace

    CfgVerificationResult verify_cfg(const ControlFlowGraph &graph)
    {
        const std::vector<Block *> &blocks = graph.blocks();
        if(blocks.empty())
        {
            return invalid("control-flow graph has no blocks");
        }
        if(graph.entry_block() == nullptr)
        {
            return invalid("control-flow graph has no entry block");
        }
        if(graph.entry_block() != blocks.front())
        {
            return invalid("control-flow graph entry is not its first block");
        }

        absl::flat_hash_set<const Block *> block_set;
        for(const Block *block: blocks)
        {
            if(block == nullptr)
            {
                return invalid(
                    "control-flow graph block order contains a null block");
            }
            if(!block_set.insert(block).second)
            {
                return invalid(block_name(block) +
                               " occurs more than once in graph block order");
            }
        }
        if(block_set.find(graph.entry_block()) == block_set.end())
        {
            return invalid("control-flow graph entry does not belong to the "
                           "graph");
        }

        absl::flat_hash_set<const Instruction *> instruction_set;
        absl::flat_hash_map<const BlockEdge *, size_t> outgoing_edge_uses;

        for(const Block *block: blocks)
        {
            const std::vector<Instruction *> &parameters = block->parameters();
            if(block != graph.entry_block() && !parameters.empty())
            {
                return invalid(block_name(block) +
                               " has parameters before block-edge arguments "
                               "are supported");
            }
            for(const Instruction *parameter: parameters)
            {
                if(parameter == nullptr)
                {
                    return invalid(block_name(block) +
                                   " contains a null block parameter");
                }
                if(parameter->is_detached())
                {
                    return invalid(block_name(block) + " contains detached " +
                                   instruction_name(parameter));
                }
                if(!is_parameter_kind(parameter->kind()))
                {
                    return invalid(instruction_name(parameter) + " in " +
                                   block_name(block) +
                                   " is not a block-parameter instruction");
                }
                if(!is_core_instruction(parameter->kind()))
                {
                    return invalid(instruction_name(parameter) +
                                   " is not legal in Core IR");
                }
                if(!instruction_set.insert(parameter).second)
                {
                    return invalid(instruction_name(parameter) +
                                   " belongs to more than one instruction "
                                   "position");
                }
            }

            const std::vector<Instruction *> &instructions =
                block->instructions();
            if(instructions.empty())
            {
                return invalid(block_name(block) + " has no instructions");
            }

            for(size_t index = 0; index < instructions.size(); ++index)
            {
                const Instruction *instruction = instructions[index];
                if(instruction == nullptr)
                {
                    return invalid(block_name(block) +
                                   " contains a null instruction");
                }
                if(instruction->is_detached())
                {
                    return invalid(block_name(block) + " contains detached " +
                                   instruction_name(instruction));
                }
                if(is_parameter_kind(instruction->kind()))
                {
                    return invalid(instruction_name(instruction) + " in " +
                                   block_name(block) +
                                   " is a block parameter in the instruction "
                                   "list");
                }
                if(!is_core_instruction(instruction->kind()))
                {
                    return invalid(instruction_name(instruction) +
                                   " is not legal in Core IR");
                }
                if(!instruction_set.insert(instruction).second)
                {
                    return invalid(instruction_name(instruction) +
                                   " belongs to more than one instruction "
                                   "position");
                }

                bool is_last = index + 1 == instructions.size();
                if(is_last && !instruction->is_block_terminator())
                {
                    return invalid(block_name(block) +
                                   " does not end in a block terminator");
                }
                if(!is_last && instruction->is_block_terminator())
                {
                    return invalid(block_name(block) +
                                   " contains a block terminator before its "
                                   "final instruction");
                }
            }

            TerminatorInstruction terminator(instructions.back());
            TerminatorInstruction::BlockSuccessorEdges successors =
                terminator.block_successor_edges();
            size_t expected = expected_successor_count(terminator.kind());
            if(successors.size() != expected)
            {
                return invalid(block_name(block) +
                               " terminator has the wrong number of block "
                               "successor edges");
            }
            if(terminator.kind() == InstructionKind::ConditionalBranch &&
               successors[0] == successors[1])
            {
                return invalid(block_name(block) +
                               " conditional branch reuses one block edge for "
                               "both semantic arms");
            }

            for(const BlockEdge *edge: successors)
            {
                if(edge == nullptr)
                {
                    return invalid(block_name(block) +
                                   " terminator contains a null block edge");
                }
                if(edge->source() != block)
                {
                    return invalid(edge_name(edge) + " names " +
                                   block_name(edge->source()) +
                                   " as its source but is referenced by " +
                                   block_name(block));
                }
                if(block_set.find(edge->target()) == block_set.end())
                {
                    return invalid(edge_name(edge) + " targets " +
                                   block_name(edge->target()) +
                                   " outside the control-flow graph");
                }

                size_t &outgoing_use_count = outgoing_edge_uses[edge];
                ++outgoing_use_count;
                if(outgoing_use_count != 1)
                {
                    return invalid(edge_name(edge) +
                                   " is referenced by more than one source "
                                   "terminator position");
                }

                size_t incoming_count = size_t(std::count(
                    edge->target()->predecessor_edges().begin(),
                    edge->target()->predecessor_edges().end(), edge));
                if(incoming_count != 1)
                {
                    return invalid(edge_name(edge) + " occurs " +
                                   std::to_string(incoming_count) +
                                   " times in its target predecessor index");
                }
            }
        }

        for(const Block *block: blocks)
        {
            absl::flat_hash_set<const BlockEdge *> predecessor_set;
            for(const BlockEdge *edge: block->predecessor_edges())
            {
                if(edge == nullptr)
                {
                    return invalid(block_name(block) +
                                   " predecessor index contains a null edge");
                }
                if(!predecessor_set.insert(edge).second)
                {
                    return invalid(edge_name(edge) +
                                   " occurs more than once "
                                   "in " +
                                   block_name(block) + " predecessor index");
                }
                if(edge->target() != block)
                {
                    return invalid(edge_name(edge) + " occurs in " +
                                   block_name(block) +
                                   " predecessor index but targets " +
                                   block_name(edge->target()));
                }
                if(block_set.find(edge->source()) == block_set.end())
                {
                    return invalid(edge_name(edge) + " has source " +
                                   block_name(edge->source()) +
                                   " outside the control-flow graph");
                }

                auto outgoing_use = outgoing_edge_uses.find(edge);
                if(outgoing_use == outgoing_edge_uses.end() ||
                   outgoing_use->second != 1)
                {
                    return invalid(edge_name(edge) + " in " +
                                   block_name(block) +
                                   " predecessor index is not referenced once "
                                   "by its source terminator");
                }
            }
        }

        for(const Block *block: blocks)
        {
            absl::flat_hash_set<const Instruction *> available_definitions;
            for(const Instruction *parameter: block->parameters())
            {
                available_definitions.insert(parameter);
            }

            for(const Instruction *instruction: block->instructions())
            {
                std::string reference_error;
                visit_operand_references(
                    *instruction,
                    [&](OperandClass operand_class,
                        ValueRepresentation required_representation,
                        Instruction *def) {
                        if(!reference_error.empty())
                        {
                            return;
                        }
                        if(available_definitions.find(def) ==
                           available_definitions.end())
                        {
                            reference_error = instruction_name(instruction) +
                                              " in " + block_name(block) +
                                              " references " +
                                              instruction_name(def) +
                                              " outside its block or before "
                                              "its definition";
                            return;
                        }
                        if(static_cast<uint8_t>(operand_class) !=
                           static_cast<uint8_t>(def->result_class()))
                        {
                            reference_error =
                                instruction_name(instruction) +
                                " has an operand with an incompatible result "
                                "class";
                            return;
                        }
                        if(operand_class == OperandClass::ProgramValue &&
                           required_representation !=
                               ValueRepresentation::None &&
                           def->value_representation() !=
                               required_representation)
                        {
                            reference_error =
                                instruction_name(instruction) +
                                " has a program-value operand with an "
                                "incompatible representation";
                        }
                    });
                if(!reference_error.empty())
                {
                    return invalid(std::move(reference_error));
                }
                available_definitions.insert(instruction);
            }
        }

        return {true, {}};
    }

}  // namespace cl::jit
