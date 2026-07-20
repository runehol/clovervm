#include "jit/control_flow_graph.h"

#include "jit/compilation_arena.h"

#include <algorithm>
#include <cassert>

namespace cl::jit
{
    TerminatorInstruction *Block::terminator() const
    {
        assert(!instructions_.empty());
        Instruction *instruction = instructions_.back();
        assert(instruction != nullptr);
        assert(instruction->is_block_terminator());
        return static_cast<TerminatorInstruction *>(instruction);
    }

    Block *ControlFlowGraph::add_block()
    {
        Block *block = arena_->make_block();
        blocks_.push_back(block);
        if(entry_block_ == nullptr)
        {
            entry_block_ = block;
        }
        return block;
    }

    BlockEdge *ControlFlowGraph::make_block_edge(Block *source, Block *target)
    {
        assert(owns_block(source));
        assert(owns_block(target));

        BlockEdge *edge = arena_->make_block_edge(source, target);
        target->add_predecessor_edge(edge);
        return edge;
    }

    bool ControlFlowGraph::owns_block(const Block *block) const
    {
        return std::find(blocks_.begin(), blocks_.end(), block) !=
               blocks_.end();
    }

}  // namespace cl::jit
