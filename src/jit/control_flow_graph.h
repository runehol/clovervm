#ifndef CL_JIT_CONTROL_FLOW_GRAPH_H
#define CL_JIT_CONTROL_FLOW_GRAPH_H

#include "jit/block_edge_id.h"
#include "jit/bytecode_state.h"
#include "jit/instruction.h"
#include "jit/serial.h"

#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

namespace cl::jit
{
    class ControlFlowGraph;
    class CompilationStorage;
    class GraphQueries;
    class GraphBuilder;
    class GraphRewriter;
    class RewriteContext;
    class BlockEdge;
    class UseLists;
    enum class GraphQuery : uint8_t;

    namespace detail
    {
        class InstructionResolver
        {
        public:
            explicit InstructionResolver(const CompilationStorage *storage)
                : storage_(storage)
            {
                assert(storage != nullptr);
            }

            Instruction operator()(InstructionId id) const;

        private:
            const CompilationStorage *storage_;
        };
    }  // namespace detail

    using InstructionRange =
        std::ranges::transform_view<std::span<const InstructionId>,
                                    detail::InstructionResolver>;

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

        InstructionRange instructions() const
        {
            return InstructionRange(
                std::span<const InstructionId>(instruction_ids_),
                detail::InstructionResolver(storage()));
        }

        InstructionRange parameters() const
        {
            return InstructionRange(
                std::span<const InstructionId>(parameter_ids_),
                detail::InstructionResolver(storage()));
        }

        const std::vector<InstructionId> &instruction_ids() const
        {
            return instruction_ids_;
        }

        const std::vector<InstructionId> &parameter_ids() const
        {
            return parameter_ids_;
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
            parameter_ids_.push_back(parameter.id());
        }

        void append_instruction(Instruction instruction)
        {
            instruction_ids_.push_back(instruction.id());
        }

        void append_predecessor_edge(BlockEdge *edge)
        {
            predecessor_edges_.push_back(edge);
        }

        Serial serial_;
        ControlFlowGraph *graph_;
        uint32_t loop_depth_ = 0;
        std::vector<InstructionId> parameter_ids_;
        std::vector<InstructionId> instruction_ids_;
        std::vector<BlockEdge *> predecessor_edges_;
    };

    class BlockEdge
    {
    public:
        BlockEdge(BlockEdgeId id, Block *source, Block *target,
                  std::span<const ProgramValueRef> arguments)
            : id_(id), source_(source), target_(target),
              arguments_(arguments.begin(), arguments.end())
        {
        }

        BlockEdge(const BlockEdge &) = delete;
        BlockEdge &operator=(const BlockEdge &) = delete;
        BlockEdge(BlockEdge &&) = delete;
        BlockEdge &operator=(BlockEdge &&) = delete;

        BlockEdgeId id() const { return id_; }
        Block *source() const { return source_; }
        Block *target() const { return target_; }
        const std::vector<ProgramValueRef> &arguments() const
        {
            return arguments_;
        }

    private:
        BlockEdgeId id_;
        Block *source_;
        Block *target_;
        std::vector<ProgramValueRef> arguments_;
    };

    class ControlFlowGraph
    {
    public:
        using Serial = TypedSerial<ControlFlowGraph>;

        ControlFlowGraph(Serial serial, CompilationStorage *storage,
                         IRLevel ir_level);
        ~ControlFlowGraph();

        ControlFlowGraph(const ControlFlowGraph &) = delete;
        ControlFlowGraph &operator=(const ControlFlowGraph &) = delete;
        ControlFlowGraph(ControlFlowGraph &&) = delete;
        ControlFlowGraph &operator=(ControlFlowGraph &&) = delete;

        Serial serial() const { return serial_; }
        CompilationStorage *storage() { return storage_; }
        const CompilationStorage *storage() const { return storage_; }
        IRLevel ir_level() const { return ir_level_; }
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
        friend class RewriteContext;

        void rebuild_predecessor_edge_index();

        Serial serial_;
        CompilationStorage *storage_;
        IRLevel ir_level_;
        Block *entry_block_ = nullptr;
        std::vector<Block *> blocks_;
        std::optional<BytecodeStateOrder> bytecode_state_order_;
        bool published_ = false;
        uint64_t mutation_generation_ = 0;
        mutable std::unique_ptr<UseLists> use_lists_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_CONTROL_FLOW_GRAPH_H
