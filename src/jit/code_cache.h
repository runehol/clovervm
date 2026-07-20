#ifndef CL_JIT_CODE_CACHE_H
#define CL_JIT_CODE_CACHE_H

#include "jit/code_cache_types.h"
#include "jit/platform_code_memory.h"
#include "util/result.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace cl::jit
{
    class CodeCache;
    class CodeCacheSlab;
    class CodeAllocation;

    class JitCodeObject
    {
    public:
        JitCodeObject(CodeSlice code, ValuePoolSlice value_pool,
                      size_t encoded_size)
            : code_(code), value_pool_(value_pool), encoded_size_(encoded_size)
        {
        }

        JitCodeObject(JitCodeObject &&) = default;

        const CodeSlice &code() const { return code_; }
        const ValuePoolSlice &value_pool() const { return value_pool_; }
        MachineAddress entry() const { return code_.execute_address(); }
        size_t encoded_size() const { return encoded_size_; }

    private:
        CodeSlice code_;
        ValuePoolSlice value_pool_;
        size_t encoded_size_;
    };

    class [[nodiscard]] CodeAllocationProposal
    {
    public:
        CodeAllocationProposal(CodeAllocationProposal &&other) noexcept;

        MachineAddress code_address() const;
        MachineAddress value_pool_address() const;

        CodeAllocation commit(size_t final_code_size);

    private:
        friend class CodeCache;

        CodeAllocationProposal(CodeCacheSlab *slab, size_t code_offset,
                               size_t pessimistic_code_size, size_t pool_offset,
                               size_t pool_slot_count);

        CodeCacheSlab *slab_;
        size_t code_offset_;
        size_t pessimistic_code_size_;
        size_t pool_offset_;
        size_t pool_slot_count_;
    };

    class [[nodiscard]] CodeAllocation
    {
    public:
        CodeSlice code;
        ValuePoolSlice value_pool;

    private:
        friend class CodeCache;
        friend class CodeCacheSlab;

        CodeAllocation(CodeSlice code, ValuePoolSlice value_pool,
                       CodeCacheSlab *slab, size_t code_offset,
                       size_t final_code_size)
            : code(code), value_pool(value_pool), slab_(slab),
              code_offset_(code_offset), final_code_size_(final_code_size)
        {
        }

        CodeCacheSlab *slab_;
        size_t code_offset_;
        size_t final_code_size_;
    };

    class CodeCache
    {
    public:
        static constexpr size_t DefaultSlabSize = 1024 * 1024;

        explicit CodeCache(std::unique_ptr<PlatformCodeMemory> platform_memory,
                           size_t standard_slab_size = DefaultSlabSize);
        ~CodeCache();

        bool fits_within_span(size_t pessimistic_code_size,
                              size_t pool_slot_count,
                              size_t maximum_span) const;

        [[nodiscard]] Result<CodeAllocationProposal, CodeCacheError>
        propose(size_t pessimistic_code_size, size_t pool_slot_count);

        [[nodiscard]] Result<JitCodeObject *, CodeCacheError>
        publish(const CodeAllocation &allocation);

    private:
        size_t minimum_slab_size(size_t pessimistic_code_size,
                                 size_t pool_slot_count) const;
        size_t page_size() const { return platform_memory_->page_size(); }
        size_t code_allocation_granularity() const
        {
            return platform_memory_->code_allocation_granularity();
        }

        std::unique_ptr<PlatformCodeMemory> platform_memory_;
        size_t standard_slab_size_;
        std::vector<std::unique_ptr<CodeCacheSlab>> slabs_;
        std::vector<std::unique_ptr<JitCodeObject>> published_code_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_CODE_CACHE_H
