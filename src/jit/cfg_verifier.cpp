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
                if(!instruction_set.insert(instruction).second)
                {
                    return invalid(
                        "instruction i" +
                        std::to_string(instruction->serial().value()) +
                        " belongs to more than one instruction position");
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

        return {true, {}};
    }

}  // namespace cl::jit
