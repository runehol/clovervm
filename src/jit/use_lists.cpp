#include "jit/use_lists.h"

#include "jit/compilation_storage.h"
#include "runtime/fatal.h"

#include <cassert>

namespace cl::jit
{
    UseLists::UseLists(const ControlFlowGraph &graph)
        : graph_generation_(graph.mutation_generation())
    {
        assert(graph.is_published());

        for(const Block *block: graph.blocks())
        {
            assert(block != nullptr);

            // Phase one: establish every block parameter as a def before
            // examining uses in the body.
            for(const Instruction *parameter: block->parameters())
            {
                assert(parameter != nullptr);
                add_def(*block, *parameter);
            }

            // Phase two: record body defs and instruction operand uses in
            // definition order.
            for(const Instruction *instruction: block->instructions())
            {
                assert(instruction != nullptr);
                add_def(*block, *instruction);
                add_instruction_uses(*block, *instruction);
            }

            // Phase three: record uses at each outgoing edge after every body
            // definition is available.
            for(const BlockEdge *edge: block->block_successor_edges())
            {
                assert(edge != nullptr);
                assert(edge->source() == block);
                add_block_argument_uses(*block, *edge);
            }
        }
    }

    const Uses &UseLists::uses_of(const Instruction &def) const
    {
        auto found = index_by_def_.find(&def);
        if(found == index_by_def_.end())
        {
            fatal("JIT use lists were queried for a non-definition");
        }
        return uses_[found->second];
    }

    void UseLists::add_def(const Block &block, const Instruction &def)
    {
        if(def.result_class() == ResultClass::None)
        {
            return;
        }
        auto [position, inserted] =
            index_by_def_.try_emplace(&def, uses_.size());
        assert(inserted);
        (void)position;
        uses_.push_back(Uses(&def, &block));
    }

    void UseLists::add_instruction_uses(const Block &block,
                                        const Instruction &instruction)
    {
        visit_operand_references(
            instruction, [&](uint32_t operand_index, OperandClass,
                             ValueRepresentation, InstructionId definition_id) {
                const Instruction *def =
                    block.storage()->instruction(definition_id);
                auto found = index_by_def_.find(def);
                assert(found != index_by_def_.end());
                Uses &uses = uses_[found->second];
                assert(uses.block_ == &block);
                uses.instruction_uses_.push_back(
                    InstructionUse{&instruction, operand_index});
            });
    }

    void UseLists::add_block_argument_uses(const Block &block,
                                           const BlockEdge &edge)
    {
        const std::vector<ProgramValueRef> &arguments = edge.arguments();
        for(size_t index = 0; index < arguments.size(); ++index)
        {
            const Instruction *def =
                block.storage()->instruction(arguments[index].instruction_id());
            auto found = index_by_def_.find(def);
            assert(found != index_by_def_.end());
            Uses &uses = uses_[found->second];
            assert(uses.block_ == &block);
            uses.block_argument_uses_.push_back(BlockArgumentUse{&edge, index});
        }
    }

}  // namespace cl::jit
