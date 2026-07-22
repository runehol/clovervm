#include "jit/code_cache.h"

#include <bit>
#include <cassert>
#include <cstdint>
#include <limits>
#include <utility>

namespace cl::jit
{
    namespace
    {
        size_t align_up(size_t value, size_t alignment)
        {
            assert(std::has_single_bit(alignment));
            size_t mask = alignment - 1;
            assert(value <= std::numeric_limits<size_t>::max() - mask);
            return (value + mask) & ~mask;
        }

        constexpr size_t align_down(size_t value, size_t alignment)
        {
            assert(std::has_single_bit(alignment));
            return value & ~(alignment - 1);
        }

        size_t pool_size(size_t slot_count)
        {
            assert(slot_count <=
                   std::numeric_limits<size_t>::max() / sizeof(Value));
            return slot_count * sizeof(Value);
        }
    }  // namespace

    class CodeCacheSlab
    {
    public:
        CodeCacheSlab(std::unique_ptr<PlatformCodeSlab> platform_slab,
                      size_t page_size, size_t code_granularity, bool dedicated)
            : platform_slab_(std::move(platform_slab)), page_size_(page_size),
              code_granularity_(code_granularity), dedicated_(dedicated),
              pool_frontier_(platform_slab_->size())
        {
            assert(platform_slab_ != nullptr);
            assert(platform_slab_->size() % page_size == 0);
        }

        bool can_propose(size_t pessimistic_code_capacity,
                         size_t pool_slot_count) const
        {
            if(dedicated_)
            {
                return false;
            }
            return reservation_fits(pessimistic_code_capacity, pool_slot_count);
        }

        MachineAddress code_address(size_t offset) const
        {
            return platform_slab_->executable_address_at(offset);
        }

        MachineAddress pool_address(size_t offset) const
        {
            return platform_slab_->data_address_at(offset);
        }

        size_t code_frontier() const { return code_frontier_; }
        size_t pool_frontier() const { return pool_frontier_; }

        CodeSlice code_slice(size_t offset, size_t capacity) const
        {
            return CodeSlice(platform_slab_->executable_address_at(offset),
                             capacity);
        }

        ValuePoolSlice pool_slice(size_t offset, size_t slot_count) const
        {
            return ValuePoolSlice(
                static_cast<Value *>(platform_slab_->write_pointer_at(offset)),
                platform_slab_->data_address_at(offset), slot_count);
        }

        Result<CodeAllocation, JitCodeError> commit(size_t code_offset,
                                                    size_t final_code_size,
                                                    size_t pool_offset,
                                                    size_t pool_slot_count)
        {
            size_t committed_size =
                align_up(final_code_size, code_granularity_);
            size_t committed_pool_size = pool_size(pool_slot_count);

            assert(code_frontier_ == code_offset);
            assert(pool_offset <= pool_frontier_);
            code_frontier_ += committed_size;
            pool_frontier_ = pool_offset;

            CL_TRY(platform_slab_->commit(code_offset, committed_size,
                                          pool_offset, committed_pool_size));
            platform_slab_->begin_code_write();
            return Result<CodeAllocation, JitCodeError>::ok(
                CodeAllocation(platform_slab_->write_pointer_at(code_offset),
                               code_slice(code_offset, committed_size),
                               pool_slice(pool_offset, pool_slot_count), this,
                               code_offset, final_code_size));
        }

        void end_code_write() { platform_slab_->end_code_write(); }

        Result<JitCodeObject, JitCodeError>
        publish(const CodeAllocation &allocation)
        {
            CL_TRY(platform_slab_->publish(allocation.code_offset_,
                                           allocation.final_code_size_,
                                           allocation.code.capacity()));

            return Result<JitCodeObject, JitCodeError>::ok(
                JitCodeObject(allocation.code, allocation.value_pool,
                              allocation.final_code_size_));
        }

    private:
        bool reservation_fits(size_t reserved_code_size,
                              size_t pool_slot_count) const
        {
            size_t slab_size = platform_slab_->size();
            if(reserved_code_size > slab_size - code_frontier_)
            {
                return false;
            }

            size_t required_pool_size = pool_size(pool_slot_count);
            if(required_pool_size > pool_frontier_)
            {
                return false;
            }
            size_t new_pool_frontier = pool_frontier_ - required_pool_size;
            size_t new_code_frontier = code_frontier_ + reserved_code_size;

            size_t lowest_pool_page = align_down(new_pool_frontier, page_size_);
            return new_code_frontier <= lowest_pool_page;
        }

        std::unique_ptr<PlatformCodeSlab> platform_slab_;
        size_t page_size_;
        size_t code_granularity_;
        bool dedicated_;
        size_t code_frontier_ = 0;
        size_t pool_frontier_;
    };

    CodeAllocation::CodeAllocation(void *write_pointer, CodeSlice code,
                                   ValuePoolSlice value_pool,
                                   CodeCacheSlab *slab, size_t code_offset,
                                   size_t final_code_size)
        : code(code), value_pool(value_pool), write_pointer_(write_pointer),
          slab_(slab), code_offset_(code_offset),
          final_code_size_(final_code_size)
    {
        assert(write_pointer != nullptr);
        assert(reinterpret_cast<uintptr_t>(write_pointer) % 16 == 0);
        assert(slab != nullptr);
    }

    CodeAllocation::CodeAllocation(CodeAllocation &&other) noexcept
        : code(other.code), value_pool(other.value_pool),
          write_pointer_(other.write_pointer_), slab_(other.slab_),
          code_offset_(other.code_offset_),
          final_code_size_(other.final_code_size_)
    {
        other.write_pointer_ = nullptr;
        other.slab_ = nullptr;
    }

    CodeAllocation::~CodeAllocation()
    {
        if(write_pointer_ != nullptr)
        {
            end_code_write();
        }
    }

    void *CodeAllocation::write_pointer() const
    {
        assert(write_pointer_ != nullptr);
        return write_pointer_;
    }

    void CodeAllocation::end_code_write()
    {
        assert(write_pointer_ != nullptr);
        assert(slab_ != nullptr);
        slab_->end_code_write();
        write_pointer_ = nullptr;
    }

    CodeAllocationProposal::CodeAllocationProposal(CodeCacheSlab *slab,
                                                   size_t code_offset,
                                                   size_t pessimistic_code_size,
                                                   size_t pool_offset,
                                                   size_t pool_slot_count)
        : slab_(slab), code_offset_(code_offset),
          pessimistic_code_size_(pessimistic_code_size),
          pool_offset_(pool_offset), pool_slot_count_(pool_slot_count)
    {
        assert(slab != nullptr);
    }

    CodeAllocationProposal::CodeAllocationProposal(
        CodeAllocationProposal &&other) noexcept
        : slab_(other.slab_), code_offset_(other.code_offset_),
          pessimistic_code_size_(other.pessimistic_code_size_),
          pool_offset_(other.pool_offset_),
          pool_slot_count_(other.pool_slot_count_)
    {
        other.slab_ = nullptr;
    }

    MachineAddress CodeAllocationProposal::code_address() const
    {
        assert(slab_ != nullptr);
        return slab_->code_address(code_offset_);
    }

    MachineAddress CodeAllocationProposal::value_pool_address() const
    {
        assert(slab_ != nullptr);
        return slab_->pool_address(pool_offset_);
    }

    Result<CodeAllocation, JitCodeError>
    CodeAllocationProposal::commit(size_t final_code_size)
    {
        assert(slab_ != nullptr);
        assert(final_code_size != 0);
        assert(final_code_size <= pessimistic_code_size_);

        Result<CodeAllocation, JitCodeError> result = slab_->commit(
            code_offset_, final_code_size, pool_offset_, pool_slot_count_);
        slab_ = nullptr;
        return result;
    }

    CodeCache::CodeCache(size_t standard_slab_size)
        : CodeCache(make_preferred_code_memory(), standard_slab_size)
    {
    }

    CodeCache::CodeCache(std::unique_ptr<PlatformCodeMemory> platform_memory,
                         size_t standard_slab_size)
        : platform_memory_(std::move(platform_memory)),
          standard_slab_size_(standard_slab_size)
    {
        assert(platform_memory_ != nullptr);
        assert(std::has_single_bit(page_size()));
        assert(std::has_single_bit(code_allocation_granularity()));
        assert(code_allocation_granularity() >= 16);
        assert(page_size() % code_allocation_granularity() == 0);
        assert(standard_slab_size_ != 0);
        assert(standard_slab_size_ % page_size() == 0);
    }

    CodeCache::~CodeCache() = default;

    size_t CodeCache::minimum_slab_size(size_t pessimistic_code_size,
                                        size_t pool_slot_count) const
    {
        size_t reserved_code_size =
            align_up(pessimistic_code_size, code_allocation_granularity());

        if(pool_slot_count == 0)
        {
            return align_up(reserved_code_size, page_size());
        }

        size_t code_pages_size = align_up(reserved_code_size, page_size());
        size_t pool_pages_size =
            align_up(pool_size(pool_slot_count), page_size());
        assert(code_pages_size <=
               std::numeric_limits<size_t>::max() - pool_pages_size);
        return code_pages_size + pool_pages_size;
    }

    bool CodeCache::fits_within_span(size_t pessimistic_code_size,
                                     size_t pool_slot_count,
                                     size_t maximum_span) const
    {
        size_t minimum_size =
            minimum_slab_size(pessimistic_code_size, pool_slot_count);
        return pool_slot_count == 0 || minimum_size <= maximum_span;
    }

    Result<CodeAllocationProposal, JitCodeError>
    CodeCache::propose(size_t pessimistic_code_size, size_t pool_slot_count)
    {
        assert(pessimistic_code_size != 0);

        size_t reserved_code_size =
            align_up(pessimistic_code_size, code_allocation_granularity());
        size_t minimum_size =
            minimum_slab_size(pessimistic_code_size, pool_slot_count);

        bool use_standard_slab = minimum_size <= standard_slab_size_;
        CodeCacheSlab *slab = nullptr;
        if(use_standard_slab)
        {
            for(const std::unique_ptr<CodeCacheSlab> &candidate: slabs_)
            {
                if(candidate->can_propose(reserved_code_size, pool_slot_count))
                {
                    slab = candidate.get();
                    break;
                }
            }
        }
        if(slab == nullptr)
        {
            size_t slab_size =
                use_standard_slab ? standard_slab_size_ : minimum_size;
            std::unique_ptr<PlatformCodeSlab> platform_slab =
                CL_TRY(platform_memory_->allocate_slab(slab_size));
            slab = slabs_
                       .emplace_back(std::make_unique<CodeCacheSlab>(
                           std::move(platform_slab), page_size(),
                           code_allocation_granularity(), !use_standard_slab))
                       .get();
        }

        size_t code_offset = slab->code_frontier();
        size_t required_pool_size = pool_size(pool_slot_count);
        assert(required_pool_size <= slab->pool_frontier());
        size_t pool_offset = slab->pool_frontier() - required_pool_size;
        return Result<CodeAllocationProposal, JitCodeError>::ok(
            CodeAllocationProposal(slab, code_offset, pessimistic_code_size,
                                   pool_offset, pool_slot_count));
    }

    Result<JitCodeObject *, JitCodeError>
    CodeCache::publish(CodeAllocation &&allocation)
    {
        allocation.end_code_write();
        JitCodeObject object = CL_TRY(allocation.slab_->publish(allocation));
        JitCodeObject *published =
            published_code_
                .emplace_back(
                    std::make_unique<JitCodeObject>(std::move(object)))
                .get();
        return Result<JitCodeObject *, JitCodeError>::ok(published);
    }

}  // namespace cl::jit
