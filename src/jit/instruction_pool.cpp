#include "jit/instruction_pool.h"

#include "object_model/value.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <new>

namespace cl::jit
{
    void InstructionPool::add_slab()
    {
        Slab slab;
        slab.storage = std::make_unique<std::byte[]>(SlabBytes);

        uintptr_t base = reinterpret_cast<uintptr_t>(slab.storage.get());
        uintptr_t first = ((base + 15) & ~uintptr_t{15}) + 8;
        assert(first >= base);
        assert(first + RecordsPerSlab * sizeof(Instruction) <=
               base + SlabBytes);
        assert((first & 0xf) == value_interned_ptr_tag);

        slab.next = reinterpret_cast<std::byte *>(first);
        slab.remaining = RecordsPerSlab;
        slabs_.push_back(std::move(slab));
    }

    Instruction *
    InstructionPool::make(InstructionKind kind, uint16_t operand_count,
                          bool indirect_operands,
                          absl::Span<const Instruction::Slot> inline_slots)
    {
        assert(next_serial_ != std::numeric_limits<uint32_t>::max());
        const InstructionKindMetadata &metadata =
            instruction_kind_metadata(kind);
        assert(inline_slots.size() == metadata.inline_slot_count);
        assert(indirect_operands == metadata.has_variadic_operands);
        assert(operand_count <= Instruction::OperandCountMask);
        if(indirect_operands)
        {
            assert(operand_count >= metadata.fixed_operand_count);
        }
        else
        {
            assert(operand_count == metadata.fixed_operand_count);
        }
        if(slabs_.empty() || slabs_.back().remaining == 0)
        {
            add_slab();
        }

        Slab &slab = slabs_.back();
        void *storage = slab.next;
        slab.next += sizeof(Instruction);
        --slab.remaining;

        uintptr_t address = reinterpret_cast<uintptr_t>(storage);
        assert((address & value_interned_ptr_tag) != 0);
        assert((address & (alignof(Instruction) - 1)) == 0);

        return new(storage) Instruction(next_serial_++, kind, operand_count,
                                        indirect_operands, inline_slots);
    }

    absl::Span<uintptr_t> InstructionSideDataPool::allocate_words(size_t count)
    {
        if(count == 0)
        {
            return {};
        }

        if(slabs_.empty() ||
           slabs_.back().capacity - slabs_.back().used < count)
        {
            size_t capacity = std::max(WordsPerSlab, count);
            slabs_.push_back(
                {std::unique_ptr<uintptr_t[]>(new uintptr_t[capacity]),
                 capacity, 0});
        }

        Slab &slab = slabs_.back();
        uintptr_t *result = slab.storage.get() + slab.used;
        slab.used += count;
        return {result, count};
    }

}  // namespace cl::jit
