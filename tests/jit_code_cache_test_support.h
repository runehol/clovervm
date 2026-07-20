#ifndef CL_TESTS_JIT_CODE_CACHE_TEST_SUPPORT_H
#define CL_TESTS_JIT_CODE_CACHE_TEST_SUPPORT_H

#include "jit/code_cache.h"
#include "jit/machine_address_internal.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace cl::jit::test_support
{
    class FakePlatformCodeSlab final : public PlatformCodeSlab
    {
    public:
        FakePlatformCodeSlab(size_t size, uintptr_t machine_base,
                             bool *fail_publication)
            : storage_(std::make_unique<uint8_t[]>(size)), size_(size),
              machine_base_(machine_base), fail_publication_(fail_publication)
        {
        }

        size_t size() const override { return size_; }

        void *write_pointer_at(size_t offset) const override
        {
            assert(offset <= size_);
            return storage_.get() + offset;
        }

        MachineAddress executable_address_at(size_t offset) const override
        {
            assert(offset <= size_);
            return detail::MachineAddressAccess::from_bits(machine_base_ +
                                                           offset);
        }

        MachineAddress data_address_at(size_t offset) const override
        {
            assert(offset <= size_);
            return detail::MachineAddressAccess::from_bits(machine_base_ +
                                                           offset);
        }

        Result<void, CodeCacheError> publish(size_t, size_t encoded_size,
                                             size_t protected_size) override
        {
            published_encoded_size = encoded_size;
            published_protected_size = protected_size;
            if(*fail_publication_)
            {
                return Result<void, CodeCacheError>::error(
                    CodeCacheError::PublicationFailure);
            }
            return Result<void, CodeCacheError>::ok();
        }

        size_t published_encoded_size = 0;
        size_t published_protected_size = 0;

    private:
        std::unique_ptr<uint8_t[]> storage_;
        size_t size_;
        uintptr_t machine_base_;
        bool *fail_publication_;
    };

    class FakePlatformCodeMemory final : public PlatformCodeMemory
    {
    public:
        FakePlatformCodeMemory(size_t page_size, size_t code_granularity)
            : page_size_(page_size), code_granularity_(code_granularity)
        {
        }

        size_t page_size() const override { return page_size_; }
        size_t code_allocation_granularity() const override
        {
            return code_granularity_;
        }

        Result<std::unique_ptr<PlatformCodeSlab>, CodeCacheError>
        allocate_slab(size_t size) override
        {
            requested_sizes.push_back(size);
            if(fail_allocation)
            {
                return Result<
                    std::unique_ptr<PlatformCodeSlab>,
                    CodeCacheError>::error(CodeCacheError::AllocationFailure);
            }
            auto slab = std::make_unique<FakePlatformCodeSlab>(
                size, next_machine_base, &fail_publication);
            last_slab = slab.get();
            next_machine_base += size + page_size_;
            return Result<std::unique_ptr<PlatformCodeSlab>,
                          CodeCacheError>::ok(std::move(slab));
        }

        bool fail_allocation = false;
        bool fail_publication = false;
        std::vector<size_t> requested_sizes;
        FakePlatformCodeSlab *last_slab = nullptr;

    private:
        size_t page_size_;
        size_t code_granularity_;
        uintptr_t next_machine_base = 0x10000000;
    };

    struct CacheAndPlatform
    {
        explicit CacheAndPlatform(size_t code_granularity,
                                  size_t standard_slab_size = 64 * 1024)
        {
            auto owned_platform = std::make_unique<FakePlatformCodeMemory>(
                PageSize, code_granularity);
            platform = owned_platform.get();
            cache = std::make_unique<CodeCache>(std::move(owned_platform),
                                                standard_slab_size);
        }

        static constexpr size_t PageSize = 4096;
        FakePlatformCodeMemory *platform;
        std::unique_ptr<CodeCache> cache;
    };

}  // namespace cl::jit::test_support

#endif  // CL_TESTS_JIT_CODE_CACHE_TEST_SUPPORT_H
