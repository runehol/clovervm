#ifndef CL_JIT_CONTROL_FLOW_GRAPH_H
#define CL_JIT_CONTROL_FLOW_GRAPH_H

#include "jit/instruction.h"
#include "jit/serial.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace cl::jit
{
    class ControlFlowGraph;
    class GraphQueries;
    class GraphBuilder;
    class BlockEdge;
    class UseLists;
    enum class GraphQuery : uint8_t;

    class Block
    {
    public:
        using Serial = TypedSerial<Block>;

        explicit Block(Serial serial, ControlFlowGraph *graph = nullptr)
            : serial_(serial), graph_(graph)
        {
        }

        Serial serial() const { return serial_; }

        const std::vector<Instruction *> &instructions() const
        {
            return instructions_;
        }

        const std::vector<Instruction *> &parameters() const
        {
            return parameters_;
        }

        const std::vector<BlockEdge *> &predecessor_edges() const
        {
            return predecessor_edges_;
        }

        TerminatorInstruction terminator() const;

        TerminatorInstruction::BlockSuccessorEdges block_successor_edges() const
        {
            return terminator().block_successor_edges();
        }

    private:
        friend class ControlFlowGraph;
        friend class GraphBuilder;

        void append_parameter(Instruction *parameter)
        {
            parameters_.push_back(parameter);
        }

        void append_instruction(Instruction *instruction)
        {
            instructions_.push_back(instruction);
        }

        void append_predecessor_edge(BlockEdge *edge)
        {
            predecessor_edges_.push_back(edge);
        }

        Serial serial_;
        ControlFlowGraph *graph_;
        std::vector<Instruction *> parameters_;
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
        using Serial = TypedSerial<ControlFlowGraph>;

        explicit ControlFlowGraph(Serial serial);
        ~ControlFlowGraph();

        ControlFlowGraph(const ControlFlowGraph &) = delete;
        ControlFlowGraph &operator=(const ControlFlowGraph &) = delete;
        ControlFlowGraph(ControlFlowGraph &&) = delete;
        ControlFlowGraph &operator=(ControlFlowGraph &&) = delete;

        Serial serial() const { return serial_; }
        Block *entry_block() const { return entry_block_; }
        const std::vector<Block *> &blocks() const { return blocks_; }
        bool is_published() const { return published_; }
        uint64_t mutation_generation() const { return mutation_generation_; }

        bool owns_block(const Block *block) const;
        GraphQueries prepare_queries(GraphQuery queries) const;

    private:
        friend class GraphBuilder;

        Serial serial_;
        Block *entry_block_ = nullptr;
        std::vector<Block *> blocks_;
        bool published_ = false;
        uint64_t mutation_generation_ = 0;
        mutable std::unique_ptr<UseLists> use_lists_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_CONTROL_FLOW_GRAPH_H
