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

        size_t aligned_pool_offset(size_t frontier, size_t pool_size,
                                   size_t pool_alignment)
        {
            assert(std::has_single_bit(pool_alignment));
            if(pool_size == 0)
            {
                return frontier;
            }
            assert(pool_size <= frontier);
            return align_down(frontier - pool_size, pool_alignment);
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

        bool can_propose(size_t pessimistic_code_capacity, size_t pool_size,
                         size_t pool_alignment) const
        {
            if(dedicated_)
            {
                return false;
            }
            return reservation_fits(pessimistic_code_capacity, pool_size,
                                    pool_alignment);
        }

        MachineAddress code_address(size_t offset) const
        {
            return platform_slab_->executable_address_at(offset);
        }

        MachineAddress constant_pool_address(size_t offset) const
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

        std::span<std::byte> constant_pool(size_t offset, size_t size) const
        {
            return {static_cast<std::byte *>(
                        platform_slab_->write_pointer_at(offset)),
                    size};
        }

        Result<CodeAllocation, JitCodeError> commit(size_t code_offset,
                                                    size_t encoded_code_size,
                                                    size_t pool_offset,
                                                    size_t pool_size)
        {
            size_t committed_size =
                align_up(encoded_code_size, code_granularity_);
            assert(code_frontier_ == code_offset);
            assert(pool_offset <= pool_frontier_);
            code_frontier_ += committed_size;
            pool_frontier_ = pool_offset;

            CL_TRY(platform_slab_->commit(code_offset, committed_size,
                                          pool_offset, pool_size));
            platform_slab_->begin_code_write();
            return Result<CodeAllocation, JitCodeError>::ok(CodeAllocation(
                {static_cast<std::byte *>(
                     platform_slab_->write_pointer_at(code_offset)),
                 committed_size},
                code_slice(code_offset, committed_size),
                constant_pool(pool_offset, pool_size),
                platform_slab_->data_address_at(pool_offset), this, code_offset,
                encoded_code_size));
        }

        void end_code_write() { platform_slab_->end_code_write(); }

        Result<void, JitCodeError> publish(CodeAllocation &allocation)
        {
            CL_TRY(platform_slab_->publish(allocation.code_offset_,
                                           allocation.encoded_code_size_,
                                           allocation.code.capacity()));

            return Result<void, JitCodeError>::ok();
        }

    private:
        bool reservation_fits(size_t reserved_code_size, size_t pool_size,
                              size_t pool_alignment) const
        {
            size_t slab_size = platform_slab_->size();
            if(reserved_code_size > slab_size - code_frontier_)
            {
                return false;
            }

            if(pool_size > pool_frontier_)
            {
                return false;
            }
            size_t new_pool_frontier =
                aligned_pool_offset(pool_frontier_, pool_size, pool_alignment);
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

    CodeAllocation::CodeAllocation(std::span<std::byte> writable_code,
                                   CodeSlice code,
                                   std::span<std::byte> constant_pool,
                                   MachineAddress constant_pool_address,
                                   CodeCacheSlab *slab, size_t code_offset,
                                   size_t encoded_code_size)
        : code(code), writable_code_(writable_code),
          constant_pool_(constant_pool),
          constant_pool_address_(constant_pool_address), slab_(slab),
          code_offset_(code_offset), encoded_code_size_(encoded_code_size)
    {
        assert(!writable_code.empty());
        assert(reinterpret_cast<uintptr_t>(writable_code.data()) % 16 == 0);
        assert(slab != nullptr);
    }

    CodeAllocation::CodeAllocation(CodeAllocation &&other) noexcept
        : code(other.code), writable_code_(other.writable_code_),
          constant_pool_(other.constant_pool_),
          constant_pool_address_(other.constant_pool_address_),
          slab_(other.slab_), code_offset_(other.code_offset_),
          encoded_code_size_(other.encoded_code_size_)
    {
        other.writable_code_ = {};
        other.constant_pool_ = {};
        other.slab_ = nullptr;
    }

    CodeAllocation::~CodeAllocation()
    {
        if(!writable_code_.empty())
        {
            end_code_write();
        }
    }

    std::span<std::byte> CodeAllocation::writable_code()
    {
        assert(!writable_code_.empty());
        return writable_code_;
    }

    void CodeAllocation::end_code_write()
    {
        assert(!writable_code_.empty());
        assert(slab_ != nullptr);
        slab_->end_code_write();
        writable_code_ = {};
    }

    CodeAllocationProposal::CodeAllocationProposal(CodeCacheSlab *slab,
                                                   size_t code_offset,
                                                   size_t pessimistic_code_size,
                                                   size_t pool_offset,
                                                   size_t pool_size)
        : slab_(slab), code_offset_(code_offset),
          pessimistic_code_size_(pessimistic_code_size),
          pool_offset_(pool_offset), pool_size_(pool_size)
    {
        assert(slab != nullptr);
    }

    CodeAllocationProposal::CodeAllocationProposal(
        CodeAllocationProposal &&other) noexcept
        : slab_(other.slab_), code_offset_(other.code_offset_),
          pessimistic_code_size_(other.pessimistic_code_size_),
          pool_offset_(other.pool_offset_), pool_size_(other.pool_size_)
    {
        other.slab_ = nullptr;
    }

    MachineAddress CodeAllocationProposal::code_address() const
    {
        assert(slab_ != nullptr);
        return slab_->code_address(code_offset_);
    }

    MachineAddress CodeAllocationProposal::constant_pool_address() const
    {
        assert(slab_ != nullptr);
        return slab_->constant_pool_address(pool_offset_);
    }

    Result<CodeAllocation, JitCodeError>
    CodeAllocationProposal::commit(size_t encoded_code_size)
    {
        assert(slab_ != nullptr);
        assert(encoded_code_size != 0);
        assert(encoded_code_size <= pessimistic_code_size_);

        Result<CodeAllocation, JitCodeError> result = slab_->commit(
            code_offset_, encoded_code_size, pool_offset_, pool_size_);
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
                                        size_t constant_pool_size,
                                        size_t constant_pool_alignment) const
    {
        assert(std::has_single_bit(constant_pool_alignment));
        assert(constant_pool_alignment <= page_size());
        size_t reserved_code_size =
            align_up(pessimistic_code_size, code_allocation_granularity());

        if(constant_pool_size == 0)
        {
            return align_up(reserved_code_size, page_size());
        }

        size_t code_pages_size = align_up(reserved_code_size, page_size());
        size_t pool_pages_size = align_up(constant_pool_size, page_size());
        assert(code_pages_size <=
               std::numeric_limits<size_t>::max() - pool_pages_size);
        return code_pages_size + pool_pages_size;
    }

    bool CodeCache::fits_within_span(size_t pessimistic_code_size,
                                     size_t constant_pool_size,
                                     size_t constant_pool_alignment,
                                     size_t maximum_span) const
    {
        size_t minimum_size = minimum_slab_size(
            pessimistic_code_size, constant_pool_size, constant_pool_alignment);
        return constant_pool_size == 0 || minimum_size <= maximum_span;
    }

    Result<CodeAllocationProposal, JitCodeError>
    CodeCache::propose(size_t pessimistic_code_size, size_t constant_pool_size,
                       size_t constant_pool_alignment)
    {
        assert(pessimistic_code_size != 0);
        assert(std::has_single_bit(constant_pool_alignment));
        assert(constant_pool_alignment <= page_size());

        size_t reserved_code_size =
            align_up(pessimistic_code_size, code_allocation_granularity());
        size_t minimum_size = minimum_slab_size(
            pessimistic_code_size, constant_pool_size, constant_pool_alignment);

        bool use_standard_slab = minimum_size <= standard_slab_size_;
        CodeCacheSlab *slab = nullptr;
        if(use_standard_slab)
        {
            for(const std::unique_ptr<CodeCacheSlab> &candidate: slabs_)
            {
                if(candidate->can_propose(reserved_code_size,
                                          constant_pool_size,
                                          constant_pool_alignment))
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
        size_t pool_offset = aligned_pool_offset(
            slab->pool_frontier(), constant_pool_size, constant_pool_alignment);
        return Result<CodeAllocationProposal, JitCodeError>::ok(
            CodeAllocationProposal(slab, code_offset, pessimistic_code_size,
                                   pool_offset, constant_pool_size));
    }

    Result<void, JitCodeError> CodeCache::publish(CodeAllocation &allocation)
    {
        allocation.end_code_write();
        return allocation.slab_->publish(allocation);
    }

}  // namespace cl::jit
