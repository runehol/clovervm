#include "jit/graph_builder.h"

#include "jit/cfg_verifier.h"
#include "runtime/fatal.h"

#include <cassert>

namespace cl::jit
{
    namespace
    {
        bool is_parameter_kind(InstructionKind kind)
        {
            return kind == InstructionKind::Parameter ||
                   kind == InstructionKind::ParameterF64;
        }
    }  // namespace

    GraphBuilder::GraphBuilder(CompilationSession &session)
        : arena_(&session.arena()), graph_(arena_->make_graph())
    {
    }

    Block *GraphBuilder::make_block()
    {
        assert_can_build();
        return arena_->make_block();
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

    void GraphBuilder::append_instruction(Block *block,
                                          Instruction *instruction)
    {
        assert_can_mutate(block);
        assert(instruction != nullptr);
        assert(!is_parameter_kind(instruction->kind()));
        block->append_instruction(instruction);
    }

    BlockEdge *GraphBuilder::make_block_edge(Block *source, Block *target)
    {
        assert_can_mutate(source);
        assert(target != nullptr);
        assert(graph_->owns_block(target));
        return arena_->make_block_edge(source, target);
    }

    ControlFlowGraph *GraphBuilder::finalize()
    {
        assert(graph_ != nullptr);
        assert(!graph_->is_published());
        build_predecessor_edges();
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

    void GraphBuilder::build_predecessor_edges()
    {
        for(Block *block: graph_->blocks_)
        {
            assert(block->predecessor_edges_.empty());
        }

        for(Block *block: graph_->blocks_)
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
                   graph_->owns_block(edge->target()))
                {
                    edge->target()->append_predecessor_edge(edge);
                }
            }
        }
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
