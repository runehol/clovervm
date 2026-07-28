#ifndef CL_JIT_CODE_CACHE_H
#define CL_JIT_CODE_CACHE_H

#include "jit/code_cache_types.h"
#include "jit/platform_code_memory.h"
#include "object_model/value.h"
#include "util/result.h"

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace cl::jit
{
    class CodeCache;
    class CodeCacheSlab;
    class CodeAllocation;

    class PublishedCode
    {
    public:
        PublishedCode(CodeSlice code, std::span<std::byte> constant_pool,
                      MachineAddress constant_pool_address,
                      size_t tagged_value_count, size_t encoded_code_size)
            : code_(code), constant_pool_(constant_pool),
              constant_pool_address_(constant_pool_address),
              tagged_value_count_(tagged_value_count),
              encoded_code_size_(encoded_code_size)
        {
            assert(encoded_code_size != 0);
            assert(encoded_code_size <= code.capacity());
            assert(tagged_value_count <= constant_pool.size() / sizeof(Value));
            assert(reinterpret_cast<uintptr_t>(constant_pool.data()) %
                       alignof(Value) ==
                   0);
        }

        const CodeSlice &code() const { return code_; }
        std::span<std::byte> constant_pool() { return constant_pool_; }
        std::span<const std::byte> constant_pool() const
        {
            return constant_pool_;
        }
        MachineAddress entry() const { return code_.execute_address(); }
        MachineAddress constant_pool_address() const
        {
            return constant_pool_address_;
        }
        size_t tagged_value_count() const { return tagged_value_count_; }
        std::span<Value> tagged_values()
        {
            return {reinterpret_cast<Value *>(constant_pool_.data()),
                    tagged_value_count_};
        }
        std::span<const Value> tagged_values() const
        {
            return {reinterpret_cast<const Value *>(constant_pool_.data()),
                    tagged_value_count_};
        }
        size_t encoded_code_size() const { return encoded_code_size_; }

    private:
        CodeSlice code_;
        std::span<std::byte> constant_pool_;
        MachineAddress constant_pool_address_;
        size_t tagged_value_count_;
        size_t encoded_code_size_;
    };

    class [[nodiscard]] CodeAllocationProposal
    {
    public:
        CodeAllocationProposal(CodeAllocationProposal &&other) noexcept;

        MachineAddress code_address() const;
        MachineAddress constant_pool_address() const;

        [[nodiscard]] Result<CodeAllocation, JitCodeError>
        commit(size_t encoded_code_size);

    private:
        friend class CodeCache;

        CodeAllocationProposal(CodeCacheSlab *slab, size_t code_offset,
                               size_t pessimistic_code_size, size_t pool_offset,
                               size_t pool_size);

        CodeCacheSlab *slab_;
        size_t code_offset_;
        size_t pessimistic_code_size_;
        size_t pool_offset_;
        size_t pool_size_;
    };

    class [[nodiscard]] CodeAllocation
    {
    public:
        CodeAllocation(const CodeAllocation &) = delete;
        CodeAllocation &operator=(const CodeAllocation &) = delete;
        CodeAllocation(CodeAllocation &&other) noexcept;
        CodeAllocation &operator=(CodeAllocation &&) = delete;
        ~CodeAllocation();

        std::span<std::byte> writable_code();
        std::span<std::byte> constant_pool() { return constant_pool_; }
        MachineAddress constant_pool_address() const
        {
            return constant_pool_address_;
        }
        size_t encoded_code_size() const { return encoded_code_size_; }

        CodeSlice code;

    private:
        friend class CodeCache;
        friend class CodeCacheSlab;

        CodeAllocation(std::span<std::byte> writable_code, CodeSlice code,
                       std::span<std::byte> constant_pool,
                       MachineAddress constant_pool_address,
                       CodeCacheSlab *slab, size_t code_offset,
                       size_t encoded_code_size);

        void end_code_write();

        std::span<std::byte> writable_code_;
        std::span<std::byte> constant_pool_;
        MachineAddress constant_pool_address_;
        CodeCacheSlab *slab_;
        size_t code_offset_;
        size_t encoded_code_size_;
    };

    class CodeCache
    {
    public:
        static constexpr size_t DefaultSlabSize = 1024 * 1024;

        explicit CodeCache(size_t standard_slab_size = DefaultSlabSize);
        explicit CodeCache(std::unique_ptr<PlatformCodeMemory> platform_memory,
                           size_t standard_slab_size = DefaultSlabSize);
        ~CodeCache();

        bool fits_within_span(size_t pessimistic_code_size,
                              size_t constant_pool_size,
                              size_t constant_pool_alignment,
                              size_t maximum_span) const;

        [[nodiscard]] Result<CodeAllocationProposal, JitCodeError>
        propose(size_t pessimistic_code_size, size_t constant_pool_size,
                size_t constant_pool_alignment);

        [[nodiscard]] Result<void, JitCodeError>
        publish(CodeAllocation &allocation);

    private:
        size_t minimum_slab_size(size_t pessimistic_code_size,
                                 size_t constant_pool_size,
                                 size_t constant_pool_alignment) const;
        size_t page_size() const { return platform_memory_->page_size(); }
        size_t code_allocation_granularity() const
        {
            return platform_memory_->code_allocation_granularity();
        }

        std::unique_ptr<PlatformCodeMemory> platform_memory_;
        size_t standard_slab_size_;
        std::vector<std::unique_ptr<CodeCacheSlab>> slabs_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_CODE_CACHE_H
