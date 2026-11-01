#include "gc/strong_remembered_set.h"

#include "runtime/fatal.h"

#include <new>
#include <utility>

namespace cl::gc
{
    StrongRememberedSet::~StrongRememberedSet()
    {
        // Dropping outstanding records would orphan their header markers.
        assert(empty());
        while(blocks_)
        {
            auto next = std::move(blocks_->next);
            blocks_ = std::move(next);
        }
    }

    std::unique_ptr<StrongRememberedSet::Block>
    StrongRememberedSet::allocate_block() noexcept
    {
        std::unique_ptr<Block> block(new(std::nothrow) Block);
        if(!block)
        {
            fatal("failed to allocate strong remembered-set block");
        }
        return block;
    }

    void StrongRememberedSet::advance_block_slow() noexcept
    {
        if(current_ == nullptr)
        {
            assert(!blocks_ && used_ == 0);
            blocks_ = allocate_block();
            current_ = blocks_.get();
            return;
        }
        assert(used_ == entries_per_block);
        if(!current_->next)
        {
            current_->next = allocate_block();
        }
        current_->sealed_count = used_;
        current_ = current_->next.get();
        used_ = 0;
    }

    void StrongRememberedSet::adopt_from(StrongRememberedSet &&source) noexcept
    {
        assert(this != &source);
        if(source.empty())
        {
            return;
        }

        std::unique_ptr<Block> destination_spares;
        if(empty())
        {
            destination_spares = std::move(blocks_);
            blocks_ = std::move(source.blocks_);
        }
        else
        {
            current_->sealed_count = used_;
            destination_spares = std::move(current_->next);
            current_->next = std::move(source.blocks_);
        }
        current_ = source.current_;
        used_ = source.used_;

        // Keep the incoming spare chain attached. Return our displaced cache
        // to the empty source, rather than accumulating both caches here.
        // No active receiver block or owner header is discarded/changed.
        source.blocks_ = std::move(destination_spares);
        source.current_ = source.blocks_.get();
        source.used_ = 0;
    }

    void StrongRememberedSet::clear_after_collection() noexcept
    {
        for_each_owner([](HeapObject *owner) {
            assert(!owner->is_forwarded());
            assert(owner->generation_state() ==
                   HeapGenerationState::OldRemembered);
            owner->set_generation_state(HeapGenerationState::Old);
        });
        current_ = blocks_.get();
        used_ = 0;
    }
}  // namespace cl::gc
