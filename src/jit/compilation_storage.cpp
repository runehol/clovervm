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

    SideExitRegion *CompilationStorage::make_side_exit_region(
        std::span<const InstructionId> parameter_ids,
        std::span<const InstructionId> instruction_ids)
    {
        SideExitRegionId id = next_side_exit_region_id();
        side_exit_regions_.emplace_back(*this, id, parameter_ids,
                                        instruction_ids);
        return &side_exit_regions_.back();
    }

    const SideExitRegion &
    CompilationStorage::side_exit_region(SideExitRegionId id) const
    {
        assert(id.value() < side_exit_regions_.size());
        return side_exit_regions_[id.value()];
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
    CompilationStorage::instruction_operands(uint32_t offset,
                                             size_t count) const
    {
        return instruction_operands_.words(offset, count);
    }

    void CompilationStorage::poison_instruction(InstructionId id)
    {
        assert(id.value() < instructions_.size());
        instructions_[id.value()].poison();
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

    SideExitRegionId CompilationStorage::next_side_exit_region_id() const
    {
        if(side_exit_regions_.size() > std::numeric_limits<uint32_t>::max())
        {
            fatal("too many JIT side-exit regions");
        }
        return SideExitRegionId(
            static_cast<uint32_t>(side_exit_regions_.size()));
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
