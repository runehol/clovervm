#include "jit/control_flow_graph.h"

#include "jit/graph_queries.h"
#include "jit/use_lists.h"

#include <cassert>

namespace cl::jit
{
    ControlFlowGraph::ControlFlowGraph(Serial serial) : serial_(serial) {}

    ControlFlowGraph::~ControlFlowGraph() = default;

    TerminatorInstruction Block::terminator() const
    {
        assert(!instructions_.empty());
        Instruction *instruction = instructions_.back();
        assert(instruction != nullptr);
        assert(instruction->is_block_terminator());
        return TerminatorInstruction(instruction);
    }

    bool ControlFlowGraph::owns_block(const Block *block) const
    {
        return block != nullptr && block->graph_ == this;
    }

    GraphQueries ControlFlowGraph::prepare_queries(GraphQuery queries) const
    {
        assert(is_published());

        const UseLists *prepared_use_lists = nullptr;
        if(has_graph_query(queries, GraphQuery::Uses))
        {
            if(use_lists_ == nullptr ||
               use_lists_->graph_generation() != mutation_generation_)
            {
                use_lists_.reset(new UseLists(*this));
            }
            prepared_use_lists = use_lists_.get();
        }

        return GraphQueries(this, queries, prepared_use_lists);
    }

}  // namespace cl::jit
