#ifndef CL_GC_STRONG_REMEMBERED_SET_H
#define CL_GC_STRONG_REMEMBERED_SET_H

#include "object_model/heap_object.h"
#include "util/compiler.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>

namespace cl::gc
{
    // Single-writer, borrowed old owners. Header generation state deduplicates
    // records across all sets participating in the same collection. Owners
    // must remain alive and non-forwarded through clear_after_collection().
    // No RC ownership, safepoints, or runtime store integration is provided.
    class StrongRememberedSet
    {
    public:
        StrongRememberedSet() noexcept = default;
        ~StrongRememberedSet();

        StrongRememberedSet(const StrongRememberedSet &) = delete;
        StrongRememberedSet &operator=(const StrongRememberedSet &) = delete;
        StrongRememberedSet(StrongRememberedSet &&) = delete;
        StrongRememberedSet &operator=(StrongRememberedSet &&) = delete;

        // After a store: child is its decoded pointer, or null for an inline
        // value. No forwarded objects or concurrent header transitions.
        void record_store(HeapObject *owner, HeapObject *child) noexcept
        {
            assert(owner != nullptr && !owner->is_forwarded());
            if(owner->generation_state() != HeapGenerationState::Old ||
               child == nullptr)
            {
                return;
            }
            assert(!child->is_forwarded());
            if(child->generation_state() != HeapGenerationState::Young)
            {
                return;
            }
            record_unremembered_owner(owner);
        }

        // Caller has established exactly Old (not Young or OldRemembered),
        // and a non-forwarded owner. Child/tag checks and rejection ordering
        // belong to the caller; conservatively recording an owner is allowed.
        void record_unremembered_owner(HeapObject *owner) noexcept
        {
            assert(owner != nullptr && !owner->is_forwarded());
            assert(owner->generation_state() == HeapGenerationState::Old);
            append(owner);
            owner->set_generation_state(HeapGenerationState::OldRemembered);
        }

        bool empty() const noexcept { return used_ == 0; }

        // Do not record/reset during enumeration. The visitor may perform
        // collector-only allocation, but must preserve owner storage.
        template <typename Visitor> void for_each_owner(Visitor &&visitor) const
        {
            for(const Block *block = blocks_.get(); block != nullptr;
                block = block->next.get())
            {
                size_t count = block == current_ ? used_ : entries_per_block;
                for(size_t index = 0; index < count; ++index)
                {
                    std::invoke(visitor, block->entries[index]);
                }
                if(block == current_)
                {
                    break;
                }
            }
        }

        // Only after minor strong closure and weak repair, with all recorded
        // owners still valid. Retains blocks, ignores stale inactive cells.
        // Full moving/sweeping collections need a separate retirement protocol.
        void clear_after_collection() noexcept;

    private:
        static constexpr size_t entries_per_block = 511;
        struct Block
        {
            std::unique_ptr<Block> next;
            // Intentionally uninitialized until appended; only active cells
            // are read. The link plus pointers occupy 4 KiB on our targets.
            std::array<HeapObject *, entries_per_block> entries;
        };
        static_assert(sizeof(Block) == 4096);

        static std::unique_ptr<Block> allocate_block() noexcept;
        void advance_block_slow() noexcept;

        void append(HeapObject *owner) noexcept
        {
            if(unlikely(current_ == nullptr || used_ == entries_per_block))
            {
                advance_block_slow();
            }
            current_->entries[used_++] = owner;
        }

        // Blocks preceding current_ are full; blocks following it are spare.
        std::unique_ptr<Block> blocks_;
        Block *current_ = nullptr;
        size_t used_ = 0;
    };
}  // namespace cl::gc

#endif  // CL_GC_STRONG_REMEMBERED_SET_H
