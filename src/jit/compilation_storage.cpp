#include "jit/compilation_storage.h"

#include "runtime/fatal.h"

#include <cassert>
#include <limits>

namespace cl::jit
{
    BlockEdge *CompilationStorage::make_block_edge(
        Block *source, Block *target,
        std::span<const ProgramValueRef> arguments)
    {
        BlockEdgeId id = next_block_edge_id();
        block_edges_.emplace_back(id, source, target, arguments);
        return &block_edges_.back();
    }

    BlockEdge *CompilationStorage::block_edge(BlockEdgeId id) const
    {
        assert(id.value() < block_edges_.size());
        return const_cast<BlockEdge *>(&block_edges_[id.value()]);
    }

    Instruction CompilationStorage::instruction(InstructionId id) const
    {
        assert(id.value() < instructions_.size());
        return Instruction(this, id);
    }

    const InstructionEntry &
    CompilationStorage::instruction_entry(InstructionId id) const
    {
        assert(id.value() < instructions_.size());
        return instructions_[id.value()];
    }

    std::span<const Instruction::Slot>
    CompilationStorage::instruction_side_data(uint32_t offset,
                                              size_t count) const
    {
        return instruction_side_data_.words(offset, count);
    }

    void CompilationStorage::detach_instruction(InstructionId id)
    {
        assert(id.value() < instructions_.size());
        instructions_[id.value()].detach_and_poison();
    }

    InstructionId CompilationStorage::next_instruction_id() const
    {
        if(instructions_.size() > std::numeric_limits<uint32_t>::max())
        {
            fatal("too many JIT instructions");
        }
        return InstructionId(static_cast<uint32_t>(instructions_.size()));
    }

    BlockEdgeId CompilationStorage::next_block_edge_id() const
    {
        if(block_edges_.size() > std::numeric_limits<uint32_t>::max())
        {
            fatal("too many JIT block edges");
        }
        return BlockEdgeId(static_cast<uint32_t>(block_edges_.size()));
    }

    BlockEdge *
    decode_instruction_attribute_BlockEdge(const CompilationStorage *storage,
                                           const uint32_t *words)
    {
        assert(storage != nullptr);
        return storage->block_edge(BlockEdgeId(*words));
    }

    void encode_instruction_attribute_BlockEdge(uint32_t *words,
                                                BlockEdge *edge)
    {
        assert(edge != nullptr);
        *words = edge->id().value();
    }

}  // namespace cl::jit
