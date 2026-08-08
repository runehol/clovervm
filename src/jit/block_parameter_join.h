#ifndef CL_JIT_BLOCK_PARAMETER_JOIN_H
#define CL_JIT_BLOCK_PARAMETER_JOIN_H

#include "jit/control_flow_graph.h"

#include <cassert>
#include <cstddef>
#include <ranges>

namespace cl::jit
{
    struct IncomingArgument
    {
        BlockEdgeId edge;
        ProgramValueRef value;
    };

    class BlockParameterJoin
    {
    public:
        const Block &block() const;
        Instruction parameter() const;

        auto incoming_arguments() const
        {
            size_t argument_index = argument_index_;
            return block_->predecessor_edges() |
                   std::views::transform(
                       [argument_index](const BlockEdge *edge) {
                           assert(argument_index < edge->arguments().size());
                           return IncomingArgument{
                               edge->id(), edge->arguments()[argument_index]};
                       });
        }

        ProgramValueRef argument_from(BlockEdgeId edge) const;

    private:
        friend class ControlFlowGraph;

        BlockParameterJoin(const ControlFlowGraph *graph, const Block *block,
                           InstructionId parameter, size_t argument_index)
            : graph_(graph), block_(block), parameter_(parameter),
              argument_index_(argument_index)
        {
        }

        const ControlFlowGraph *graph_;
        const Block *block_;
        InstructionId parameter_;
        size_t argument_index_;
    };

    inline auto
    ControlFlowGraph::block_parameter_joins(const Block &block) const
    {
        assert(is_published());
        assert(owns_block(&block));
        size_t parameter_count = block.parameters().size();
        return std::views::iota(size_t{0}, parameter_count) |
               std::views::transform(
                   [this, block = &block](size_t parameter_index) {
                       assert(parameter_index < block->parameter_ids().size());
                       return BlockParameterJoin(
                           this, block, block->parameter_ids()[parameter_index],
                           parameter_index);
                   });
    }

}  // namespace cl::jit

#endif  // CL_JIT_BLOCK_PARAMETER_JOIN_H
