#include "jit/code_cache.h"
#include "jit/machine_address_internal.h"
#include "jit/standard_code_memory.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        class FakePlatformCodeSlab final : public PlatformCodeSlab
        {
        public:
            FakePlatformCodeSlab(size_t size, uintptr_t machine_base,
                                 bool *fail_publication)
                : storage_(std::make_unique<uint8_t[]>(size)), size_(size),
                  machine_base_(machine_base),
                  fail_publication_(fail_publication)
            {
            }

            size_t size() const override { return size_; }

            void *write_pointer_at(size_t offset) const override
            {
                EXPECT_LE(offset, size_);
                return storage_.get() + offset;
            }

            MachineAddress executable_address_at(size_t offset) const override
            {
                EXPECT_LE(offset, size_);
                return detail::MachineAddressAccess::from_bits(machine_base_ +
                                                               offset);
            }

            MachineAddress data_address_at(size_t offset) const override
            {
                EXPECT_LE(offset, size_);
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
                    return Result<std::unique_ptr<PlatformCodeSlab>,
                                  CodeCacheError>::
                        error(CodeCacheError::AllocationFailure);
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

        CodeAllocationProposal
        take_proposal(Result<CodeAllocationProposal, CodeCacheError> result)
        {
            EXPECT_TRUE(result);
            return std::move(result).value();
        }
    }  // namespace

    TEST(CodeCache, ComputesRoundedSpanWithoutTargetSpecificPolicy)
    {
        CacheAndPlatform fixture(16);

        EXPECT_TRUE(fixture.cache->fits_within_span(17, 1, 8192));
        EXPECT_FALSE(fixture.cache->fits_within_span(17, 1, 8191));
        EXPECT_TRUE(fixture.cache->fits_within_span(128 * 1024, 0, 1));
    }

    TEST(CodeCache, ProposalFixesCodeAndPoolAddressesWithoutWritableSlices)
    {
        CacheAndPlatform fixture(16);
        CodeAllocationProposal proposal =
            take_proposal(fixture.cache->propose(17, 2));

        EXPECT_EQ(0x10000000u,
                  proposal.code_address().bits_for_indirect_target());
        EXPECT_EQ(0x1000fff0u,
                  proposal.value_pool_address().bits_for_indirect_target());

        CodeAllocation allocation = proposal.commit(17);
        EXPECT_EQ(32u, allocation.code.capacity());
        EXPECT_EQ(2u, allocation.value_pool.slot_count());
        EXPECT_EQ(allocation.code.write_pointer(),
                  fixture.platform->last_slab->write_pointer_at(0));
        EXPECT_NE(reinterpret_cast<uintptr_t>(allocation.code.write_pointer()),
                  allocation.code.execute_address().bits_for_indirect_target());
        Result<JitCodeObject *, CodeCacheError> publication =
            fixture.cache->publish(allocation);
        ASSERT_TRUE(publication);
        EXPECT_EQ(allocation.code.execute_address(),
                  std::move(publication).value()->entry());
    }

    TEST(CodeCache, DroppingProposalPreservesBothFrontiers)
    {
        CacheAndPlatform fixture(16);
        MachineAddress first_code = detail::MachineAddressAccess::from_bits(1);
        MachineAddress first_pool = detail::MachineAddressAccess::from_bits(1);
        {
            CodeAllocationProposal proposal =
                take_proposal(fixture.cache->propose(33, 3));
            first_code = proposal.code_address();
            first_pool = proposal.value_pool_address();
        }

        CodeAllocationProposal replacement =
            take_proposal(fixture.cache->propose(33, 3));
        EXPECT_EQ(first_code, replacement.code_address());
        EXPECT_EQ(first_pool, replacement.value_pool_address());
        EXPECT_EQ(1u, fixture.platform->requested_sizes.size());
    }

    TEST(CodeCache, PublicationRecoversPessimisticCodeSlack)
    {
        CacheAndPlatform fixture(16);
        CodeAllocationProposal proposal =
            take_proposal(fixture.cache->propose(100, 1));
        CodeAllocation allocation = proposal.commit(17);
        EXPECT_EQ(32u, allocation.code.capacity());
        Result<JitCodeObject *, CodeCacheError> publication =
            fixture.cache->publish(allocation);
        ASSERT_TRUE(publication);
        JitCodeObject *object = std::move(publication).value();

        EXPECT_EQ(17u, object->encoded_size());
        EXPECT_EQ(32u, object->code().capacity());
        EXPECT_EQ(17u, fixture.platform->last_slab->published_encoded_size);
        EXPECT_EQ(32u, fixture.platform->last_slab->published_protected_size);

        CodeAllocationProposal next =
            take_proposal(fixture.cache->propose(16, 0));
        EXPECT_EQ(object->entry().offset_by(32), next.code_address());
    }

    TEST(CodeCache, PublicationFailureConsumesTheCommittedSpace)
    {
        CacheAndPlatform fixture(16);
        MachineAddress failed_address =
            detail::MachineAddressAccess::from_bits(1);
        fixture.platform->fail_publication = true;
        {
            CodeAllocationProposal proposal =
                take_proposal(fixture.cache->propose(64, 1));
            failed_address = proposal.code_address();
            CodeAllocation allocation = proposal.commit(32);
            Result<JitCodeObject *, CodeCacheError> publication =
                fixture.cache->publish(allocation);
            ASSERT_FALSE(publication);
            EXPECT_EQ(CodeCacheError::PublicationFailure, publication.error());
        }

        fixture.platform->fail_publication = false;
        CodeAllocationProposal replacement =
            take_proposal(fixture.cache->propose(64, 1));
        EXPECT_EQ(failed_address.offset_by(32), replacement.code_address());
    }

    TEST(CodeCache, AllocationFailureIsRecoverable)
    {
        CacheAndPlatform fixture(16);
        fixture.platform->fail_allocation = true;

        Result<CodeAllocationProposal, CodeCacheError> allocation =
            fixture.cache->propose(16, 1);

        ASSERT_FALSE(allocation);
        EXPECT_EQ(CodeCacheError::AllocationFailure, allocation.error());
    }

    TEST(CodeCache, UsesDedicatedSlabForAnOversizedUnit)
    {
        CacheAndPlatform fixture(16);
        EXPECT_TRUE(fixture.cache->propose(64 * 1024, 1));

        ASSERT_EQ(1u, fixture.platform->requested_sizes.size());
        EXPECT_EQ(68u * 1024, fixture.platform->requested_sizes[0]);
    }

    TEST(CodeCache, PacksPoolSlicesIntoSharedPoolPages)
    {
        CacheAndPlatform fixture(16);
        CodeAllocationProposal first_proposal =
            take_proposal(fixture.cache->propose(16, 1));
        CodeAllocation first = first_proposal.commit(16);
        JitCodeObject *first_object =
            std::move(fixture.cache->publish(first)).value();
        CodeAllocationProposal second_proposal =
            take_proposal(fixture.cache->propose(16, 1));
        CodeAllocation second = second_proposal.commit(16);
        JitCodeObject *second_object =
            std::move(fixture.cache->publish(second)).value();

        uintptr_t first_pool =
            first_object->value_pool().address().bits_for_indirect_target();
        uintptr_t second_pool =
            second_object->value_pool().address().bits_for_indirect_target();
        EXPECT_EQ(sizeof(Value), first_pool - second_pool);
        EXPECT_EQ(first_pool / CacheAndPlatform::PageSize,
                  second_pool / CacheAndPlatform::PageSize);
        EXPECT_EQ(1u, fixture.platform->requested_sizes.size());
    }

    TEST(CodeCache, NeverAllocatesCodeIntoAnExistingPoolPage)
    {
        CacheAndPlatform fixture(16);
        CodeAllocationProposal first_proposal =
            take_proposal(fixture.cache->propose(15 * 4096, 1));
        CodeAllocation first = first_proposal.commit(15 * 4096);
        JitCodeObject *first_object =
            std::move(fixture.cache->publish(first)).value();
        CodeAllocationProposal second =
            take_proposal(fixture.cache->propose(16, 0));

        EXPECT_EQ(2u, fixture.platform->requested_sizes.size());
        EXPECT_NE(first_object->entry(), second.code_address());
    }

    TEST(CodeCache, PageGranularityRecoversOnlyWholePages)
    {
        CacheAndPlatform fixture(CacheAndPlatform::PageSize);
        CodeAllocationProposal first_proposal =
            take_proposal(fixture.cache->propose(9000, 0));
        MachineAddress empty_pool_address = first_proposal.value_pool_address();
        CodeAllocation first = first_proposal.commit(5000);
        EXPECT_EQ(0u, first.value_pool.slot_count());
        EXPECT_EQ(empty_pool_address, first.value_pool.address());
        JitCodeObject *object =
            std::move(fixture.cache->publish(first)).value();
        CodeAllocationProposal second =
            take_proposal(fixture.cache->propose(1, 0));

        EXPECT_EQ(2 * CacheAndPlatform::PageSize, object->code().capacity());
        EXPECT_EQ(object->entry().offset_by(2 * CacheAndPlatform::PageSize),
                  second.code_address());
    }

    TEST(StandardCodeMemory, PublishesCodeWithoutProtectingPoolPages)
    {
        auto platform = std::make_unique<StandardCodeMemory>();
        size_t page_size = platform->page_size();
        CodeCache cache(std::move(platform));
        CodeAllocationProposal proposal = take_proposal(cache.propose(4, 1));
        CodeAllocation allocation = proposal.commit(4);
        auto *code = static_cast<uint8_t *>(allocation.code.write_pointer());
        code[0] = 0;
        code[1] = 0;
        code[2] = 0;
        code[3] = 0;
        allocation.value_pool.write_pointer()[0] = Value::None();

        Result<JitCodeObject *, CodeCacheError> publication =
            cache.publish(allocation);
        ASSERT_TRUE(publication);
        JitCodeObject *object = std::move(publication).value();
        object->value_pool().write_pointer()[0] = Value::True();

        EXPECT_EQ(Value::True(), object->value_pool().write_pointer()[0]);
        EXPECT_EQ(page_size, object->code().capacity());
    }

    TEST(CodeCacheLifetime, GivesEachThreadAVmOwnedCache)
    {
        VirtualMachine vm;
        ThreadState *first = vm.get_default_thread();
        ThreadState *second = vm.make_new_thread();

        EXPECT_NE(&first->code_cache(), &second->code_cache());
    }

}  // namespace cl::jit
