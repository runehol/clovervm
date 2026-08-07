#include "jit/graph_builder.h"

#include "jit/cfg_verifier.h"

#include <cassert>

namespace cl::jit
{
    GraphBuilder::GraphBuilder(CompilationSession &session, IRLevel ir_level)
        : session_(&session), storage_(session.storage()),
          graph_(storage_->make_graph(session.thread_state(), ir_level))
    {
    }

    Block *GraphBuilder::make_block()
    {
        assert_can_build();
        return storage_->make_block();
    }

    void GraphBuilder::append_block(Block *block)
    {
        assert_can_build();
        assert(block != nullptr);
        assert(block->graph_ == nullptr);
        block->graph_ = graph_;
        graph_->blocks_.push_back(block);
        if(graph_->entry_block_ == nullptr)
        {
            graph_->entry_block_ = block;
        }
    }

    Block *GraphBuilder::emplace_block()
    {
        Block *block = make_block();
        append_block(block);
        return block;
    }

    void GraphBuilder::emplace_n_blocks(size_t count)
    {
        assert_can_build();
        assert(count <= graph_->blocks_.max_size() - graph_->blocks_.size());
        graph_->blocks_.reserve(graph_->blocks_.size() + count);
        for(size_t index = 0; index < count; ++index)
        {
            emplace_block();
        }
    }

    Block *GraphBuilder::block_at(size_t index) const
    {
        assert_can_build();
        assert(index < graph_->blocks_.size());
        return graph_->blocks_[index];
    }

    size_t GraphBuilder::block_count() const
    {
        assert_can_build();
        return graph_->blocks_.size();
    }

    void GraphBuilder::set_loop_depth(Block *block, uint32_t loop_depth)
    {
        assert_can_mutate(block);
        block->loop_depth_ = loop_depth;
    }

    void GraphBuilder::set_bytecode_state_order(const BytecodeStateOrder &order)
    {
        assert_can_build();
        assert(!graph_->bytecode_state_order_.has_value());
        graph_->bytecode_state_order_ = order;
    }

    void GraphBuilder::append_instruction(Block *block, Instruction instruction)
    {
        assert_can_mutate(block);
        assert(!is_block_parameter_kind(instruction.kind()));
        block->append_instruction(instruction);
    }

    BlockEdge *
    GraphBuilder::make_block_edge(Block *source, Block *target,
                                  std::span<const ProgramValueRef> arguments)
    {
        assert_can_mutate(source);
        assert(target != nullptr);
        assert(graph_->owns_block(target));
        return storage_->make_block_edge(source, target, arguments);
    }

    SideExitRegion *GraphBuilder::make_side_exit_region(
        std::span<const InstructionId> parameter_ids,
        std::span<const InstructionId> instruction_ids)
    {
        assert_can_build();
        return storage_->make_side_exit_region(parameter_ids, instruction_ids);
    }

    ControlFlowGraph *GraphBuilder::finalize()
    {
        assert(graph_ != nullptr);
        assert(!graph_->is_published());
        graph_->rebuild_predecessor_edge_index();
        CfgVerificationResult result = verify_cfg(*graph_);
        if(!result.valid)
        {
            fatal("cannot publish invalid JIT CFG: " + result.message);
        }
        graph_->published_ = true;
        ControlFlowGraph *published_graph = graph_;
        graph_ = nullptr;
        return published_graph;
    }

    void GraphBuilder::assert_can_mutate(const Block *block) const
    {
        assert_can_build();
        assert(block != nullptr);
        assert(graph_->owns_block(block));
    }

    void GraphBuilder::assert_can_build() const
    {
        assert(graph_ != nullptr);
        assert(!graph_->is_published());
    }

}  // namespace cl::jit
