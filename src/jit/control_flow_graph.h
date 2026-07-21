#ifndef CL_JIT_CONTROL_FLOW_GRAPH_H
#define CL_JIT_CONTROL_FLOW_GRAPH_H

#include "jit/instruction.h"
#include "jit/serial.h"

#include <vector>

namespace cl::jit
{
    class CompilationArena;
    class ControlFlowGraph;
    class BlockEdge;

    class Block
    {
    public:
        using Serial = TypedSerial<Block>;

        explicit Block(Serial serial) : serial_(serial) {}

        Serial serial() const { return serial_; }

        const std::vector<Instruction *> &instructions() const
        {
            return instructions_;
        }

        const std::vector<BlockEdge *> &predecessor_edges() const
        {
            return predecessor_edges_;
        }

        void append_instruction(Instruction *instruction)
        {
            instructions_.push_back(instruction);
        }

        TerminatorInstruction terminator() const;

        TerminatorInstruction::BlockSuccessorEdges block_successor_edges() const
        {
            return terminator().block_successor_edges();
        }

    private:
        friend class ControlFlowGraph;

        void add_predecessor_edge(BlockEdge *edge)
        {
            predecessor_edges_.push_back(edge);
        }

        Serial serial_;
        std::vector<Instruction *> instructions_;
        std::vector<BlockEdge *> predecessor_edges_;
    };

    class BlockEdge
    {
    public:
        using Serial = TypedSerial<BlockEdge>;

        BlockEdge(Serial serial, Block *source, Block *target)
            : serial_(serial), source_(source), target_(target)
        {
        }

        Serial serial() const { return serial_; }
        Block *source() const { return source_; }
        Block *target() const { return target_; }

    private:
        Serial serial_;
        Block *source_;
        Block *target_;
    };

    class ControlFlowGraph
    {
    public:
        explicit ControlFlowGraph(CompilationArena &arena) : arena_(&arena) {}

        ControlFlowGraph(const ControlFlowGraph &) = delete;
        ControlFlowGraph &operator=(const ControlFlowGraph &) = delete;
        ControlFlowGraph(ControlFlowGraph &&) = delete;
        ControlFlowGraph &operator=(ControlFlowGraph &&) = delete;

        Block *add_block();
        BlockEdge *make_block_edge(Block *source, Block *target);

        Block *entry_block() const { return entry_block_; }
        const std::vector<Block *> &blocks() const { return blocks_; }

        bool owns_block(const Block *block) const;

    private:
        CompilationArena *arena_;
        Block *entry_block_ = nullptr;
        std::vector<Block *> blocks_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_CONTROL_FLOW_GRAPH_H
