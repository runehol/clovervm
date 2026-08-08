#ifndef CL_JIT_INSTRUCTION_TRAVERSAL_H
#define CL_JIT_INSTRUCTION_TRAVERSAL_H

#include "jit/block_order.h"
#include "jit/compilation_storage.h"
#include "jit/graph_queries.h"

#include <cassert>
#include <functional>

namespace cl::jit
{
    class InstructionTraversal
    {
    public:
        constexpr InstructionTraversal() = default;

        [[nodiscard]] constexpr InstructionTraversal
        with_block_order(BlockOrder order) const
        {
            InstructionTraversal result = *this;
            result.block_order_ = order;
            return result;
        }

        constexpr BlockOrder block_order() const { return block_order_; }

        [[nodiscard]] constexpr InstructionTraversal
        with_queries(GraphQuery queries) const
        {
            InstructionTraversal result = *this;
            result.queries_ = queries;
            return result;
        }

        constexpr GraphQuery queries() const { return queries_; }

    private:
        BlockOrder block_order_ = BlockOrder::Program;
        GraphQuery queries_ = GraphQuery::None;
    };

    template <typename Callback>
    void walk_instructions(const ControlFlowGraph &graph,
                           InstructionTraversal traversal, Callback &&callback)
    {
        assert(graph.is_published());
        GraphQueries queries = graph.prepare_queries(traversal.queries());
        for(const Block *block: ordered_blocks(graph, traversal.block_order()))
        {
            assert(block != nullptr);
            for(Instruction instruction: block->instructions())
            {
                std::invoke(callback, queries, *block, instruction);
            }
        }
    }

}  // namespace cl::jit

#endif  // CL_JIT_INSTRUCTION_TRAVERSAL_H
