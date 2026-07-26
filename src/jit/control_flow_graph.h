#ifndef CL_JIT_CONTROL_FLOW_GRAPH_H
#define CL_JIT_CONTROL_FLOW_GRAPH_H

#include "jit/bytecode_state.h"
#include "jit/instruction.h"
#include "jit/serial.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace cl::jit
{
    class ControlFlowGraph;
    class CompilationStorage;
    class GraphQueries;
    class GraphBuilder;
    class GraphRewriter;
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
        uint32_t loop_depth() const { return loop_depth_; }
        CompilationStorage *storage();
        const CompilationStorage *storage() const;

        const std::vector<InstructionId> &instructions() const
        {
            return instructions_;
        }

        const std::vector<InstructionId> &parameters() const
        {
            return parameters_;
        }

        Instruction instruction_at(size_t index) const;
        Instruction parameter_at(size_t index) const;

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
        friend class GraphRewriter;

        void append_parameter(Instruction parameter)
        {
            parameters_.push_back(parameter.id());
        }

        void append_instruction(Instruction instruction)
        {
            instructions_.push_back(instruction.id());
        }

        void append_predecessor_edge(BlockEdge *edge)
        {
            predecessor_edges_.push_back(edge);
        }

        Serial serial_;
        ControlFlowGraph *graph_;
        uint32_t loop_depth_ = 0;
        std::vector<InstructionId> parameters_;
        std::vector<InstructionId> instructions_;
        std::vector<BlockEdge *> predecessor_edges_;
    };

    class BlockEdge
    {
    public:
        using Serial = TypedSerial<BlockEdge>;

        BlockEdge(Serial serial, Block *source, Block *target,
                  std::span<const ProgramValueRef> arguments)
            : serial_(serial), source_(source), target_(target),
              arguments_(arguments.begin(), arguments.end())
        {
        }

        BlockEdge(const BlockEdge &) = delete;
        BlockEdge &operator=(const BlockEdge &) = delete;
        BlockEdge(BlockEdge &&) = delete;
        BlockEdge &operator=(BlockEdge &&) = delete;

        Serial serial() const { return serial_; }
        Block *source() const { return source_; }
        Block *target() const { return target_; }
        const std::vector<ProgramValueRef> &arguments() const
        {
            return arguments_;
        }

    private:
        Serial serial_;
        Block *source_;
        Block *target_;
        std::vector<ProgramValueRef> arguments_;
    };

    class ControlFlowGraph
    {
    public:
        using Serial = TypedSerial<ControlFlowGraph>;

        ControlFlowGraph(Serial serial, CompilationStorage *storage);
        ~ControlFlowGraph();

        ControlFlowGraph(const ControlFlowGraph &) = delete;
        ControlFlowGraph &operator=(const ControlFlowGraph &) = delete;
        ControlFlowGraph(ControlFlowGraph &&) = delete;
        ControlFlowGraph &operator=(ControlFlowGraph &&) = delete;

        Serial serial() const { return serial_; }
        CompilationStorage *storage() { return storage_; }
        const CompilationStorage *storage() const { return storage_; }
        Block *entry_block() const { return entry_block_; }
        const std::vector<Block *> &blocks() const { return blocks_; }
        bool is_published() const { return published_; }
        uint64_t mutation_generation() const { return mutation_generation_; }
        const std::optional<BytecodeStateOrder> &bytecode_state_order() const
        {
            return bytecode_state_order_;
        }

        bool owns_block(const Block *block) const;
        GraphQueries prepare_queries(GraphQuery queries) const;

    private:
        friend class GraphBuilder;
        friend class GraphRewriter;

        void rebuild_predecessor_edge_index();

        Serial serial_;
        CompilationStorage *storage_;
        Block *entry_block_ = nullptr;
        std::vector<Block *> blocks_;
        std::optional<BytecodeStateOrder> bytecode_state_order_;
        bool published_ = false;
        uint64_t mutation_generation_ = 0;
        mutable std::unique_ptr<UseLists> use_lists_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_CONTROL_FLOW_GRAPH_H
