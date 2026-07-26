#include "jit/control_flow_graph.h"

#include "jit/graph_queries.h"
#include "jit/use_lists.h"

#include <cassert>

namespace cl::jit
{
    CompilationStorage *Block::storage()
    {
        assert(graph_ != nullptr);
        return graph_->storage();
    }

    const CompilationStorage *Block::storage() const
    {
        assert(graph_ != nullptr);
        return graph_->storage();
    }

    ControlFlowGraph::ControlFlowGraph(Serial serial,
                                       CompilationStorage *storage)
        : serial_(serial), storage_(storage)
    {
        assert(storage_ != nullptr);
    }

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

    void ControlFlowGraph::rebuild_predecessor_edge_index()
    {
        for(Block *block: blocks_)
        {
            block->predecessor_edges_.clear();
        }

        for(Block *block: blocks_)
        {
            if(block->instructions_.empty())
            {
                continue;
            }
            Instruction *instruction = block->instructions_.back();
            if(instruction == nullptr || !instruction->is_block_terminator())
            {
                continue;
            }
            for(BlockEdge *edge:
                TerminatorInstruction(instruction).block_successor_edges())
            {
                if(edge != nullptr && edge->source() == block &&
                   owns_block(edge->target()))
                {
                    edge->target()->append_predecessor_edge(edge);
                }
            }
        }
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
