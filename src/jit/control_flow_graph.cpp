#include "jit/control_flow_graph.h"

#include <cassert>

namespace cl::jit
{
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

}  // namespace cl::jit
