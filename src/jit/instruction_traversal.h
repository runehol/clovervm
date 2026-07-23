#ifndef CL_JIT_INSTRUCTION_TRAVERSAL_H
#define CL_JIT_INSTRUCTION_TRAVERSAL_H

#include "jit/graph_queries.h"

#include <cassert>
#include <cstdint>
#include <functional>

namespace cl::jit
{
    enum class BlockWalkOrder : uint8_t
    {
        ProgramOrder,
    };

    class InstructionTraversal
    {
    public:
        constexpr InstructionTraversal() = default;

        [[nodiscard]] constexpr InstructionTraversal
        with_block_order(BlockWalkOrder order) const
        {
            InstructionTraversal result = *this;
            result.block_order_ = order;
            return result;
        }

        constexpr BlockWalkOrder block_order() const { return block_order_; }

        [[nodiscard]] constexpr InstructionTraversal
        with_queries(GraphQuery queries) const
        {
            InstructionTraversal result = *this;
            result.queries_ = queries;
            return result;
        }

        constexpr GraphQuery queries() const { return queries_; }

    private:
        BlockWalkOrder block_order_ = BlockWalkOrder::ProgramOrder;
        GraphQuery queries_ = GraphQuery::None;
    };

    template <typename Callback>
    void walk_instructions(const ControlFlowGraph &graph,
                           InstructionTraversal traversal, Callback &&callback)
    {
        assert(graph.is_published());
        GraphQueries queries = graph.prepare_queries(traversal.queries());
        switch(traversal.block_order())
        {
            case BlockWalkOrder::ProgramOrder:
                for(const Block *block: graph.blocks())
                {
                    assert(block != nullptr);
                    for(const Instruction *instruction: block->instructions())
                    {
                        assert(instruction != nullptr);
                        std::invoke(callback, queries, *block, *instruction);
                    }
                }
                return;
        }
        assert(false);
    }

}  // namespace cl::jit

#endif  // CL_JIT_INSTRUCTION_TRAVERSAL_H
