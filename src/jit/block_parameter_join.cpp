#include "jit/block_parameter_join.h"

#include "jit/compilation_storage.h"
#include "runtime/fatal.h"

#include <algorithm>
#include <cassert>

namespace cl::jit
{
    const Block &BlockParameterJoin::block() const { return *block_; }

    Instruction BlockParameterJoin::parameter() const
    {
        return graph_->storage()->instruction(parameter_);
    }

    ProgramValueRef BlockParameterJoin::argument_from(BlockEdgeId edge_id) const
    {
        const BlockEdge *edge = graph_->storage()->block_edge(edge_id);
        if(std::find(block_->predecessor_edges().begin(),
                     block_->predecessor_edges().end(),
                     edge) == block_->predecessor_edges().end())
        {
            fatal("JIT block-parameter join queried with a non-incoming edge");
        }
        assert(argument_index_ < edge->arguments().size());
        return edge->arguments()[argument_index_];
    }

}  // namespace cl::jit
