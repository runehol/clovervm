#include "jit/code_cache.h"
#include "jit/machine_address_internal.h"
#include "jit/standard_code_memory.h"
#include "jit_code_cache_test_support.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace cl::jit
{
    namespace
    {
        using test_support::CacheAndPlatform;

        CodeAllocationProposal
        take_proposal(Result<CodeAllocationProposal, CodeCacheError> result)
        {
            EXPECT_TRUE(result);
            return std::move(result).value();
        }

        CodeAllocation
        take_allocation(Result<CodeAllocation, CodeCacheError> result)
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

        CodeAllocation allocation = take_allocation(proposal.commit(17));
        EXPECT_EQ(32u, allocation.code.capacity());
        EXPECT_EQ(2u, allocation.value_pool.slot_count());
        EXPECT_EQ(0u, fixture.platform->last_slab->committed_code_offset);
        EXPECT_EQ(32u, fixture.platform->last_slab->committed_code_size);
        EXPECT_EQ(0xfff0u, fixture.platform->last_slab->committed_pool_offset);
        EXPECT_EQ(2 * sizeof(Value),
                  fixture.platform->last_slab->committed_pool_size);
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
        CodeAllocation allocation = take_allocation(proposal.commit(17));
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
            CodeAllocation allocation = take_allocation(proposal.commit(32));
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

    TEST(CodeCache, CommitFailureConsumesCodeAndPoolSpace)
    {
        CacheAndPlatform fixture(16);
        CodeAllocationProposal proposal =
            take_proposal(fixture.cache->propose(64, 1));
        MachineAddress failed_code = proposal.code_address();
        MachineAddress failed_pool = proposal.value_pool_address();
        fixture.platform->fail_commit = true;

        Result<CodeAllocation, CodeCacheError> allocation = proposal.commit(32);

        ASSERT_FALSE(allocation);
        EXPECT_EQ(CodeCacheError::AllocationFailure, allocation.error());

        fixture.platform->fail_commit = false;
        CodeAllocationProposal replacement =
            take_proposal(fixture.cache->propose(16, 1));
        EXPECT_EQ(failed_code.offset_by(32), replacement.code_address());
        EXPECT_EQ(
            -static_cast<int64_t>(sizeof(Value)),
            failed_pool.displacement_to(replacement.value_pool_address()));
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
        CodeAllocation first = take_allocation(first_proposal.commit(16));
        JitCodeObject *first_object =
            std::move(fixture.cache->publish(first)).value();
        CodeAllocationProposal second_proposal =
            take_proposal(fixture.cache->propose(16, 1));
        CodeAllocation second = take_allocation(second_proposal.commit(16));
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
        CodeAllocation first =
            take_allocation(first_proposal.commit(15 * 4096));
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
        CodeAllocation first = take_allocation(first_proposal.commit(5000));
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
        CodeAllocation allocation = take_allocation(proposal.commit(4));
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
