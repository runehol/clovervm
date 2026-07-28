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
        take_proposal(Result<CodeAllocationProposal, JitCodeError> result)
        {
            EXPECT_TRUE(result);
            return std::move(result).value();
        }

        CodeAllocation
        take_allocation(Result<CodeAllocation, JitCodeError> result)
        {
            EXPECT_TRUE(result);
            return std::move(result).value();
        }
    }  // namespace

    TEST(CodeCache, ComputesRoundedSpanWithoutTargetSpecificPolicy)
    {
        CacheAndPlatform fixture(16);

        EXPECT_TRUE(fixture.cache->fits_within_span(17, 1, 1, 8192));
        EXPECT_FALSE(fixture.cache->fits_within_span(17, 1, 1, 8191));
        EXPECT_TRUE(fixture.cache->fits_within_span(128 * 1024, 0, 1, 1));
    }

    TEST(CodeCache, ProposalFixesCodeAndPoolAddressesWithoutWritableSlices)
    {
        CacheAndPlatform fixture(16);
        CodeAllocationProposal proposal =
            take_proposal(fixture.cache->propose(17, 2, 16));

        EXPECT_EQ(0x10000000u,
                  proposal.code_address().bits_for_indirect_target());
        EXPECT_EQ(0x1000fff0u,
                  proposal.constant_pool_address().bits_for_indirect_target());

        CodeAllocation allocation = take_allocation(proposal.commit(17));
        EXPECT_EQ(32u, allocation.code.capacity());
        EXPECT_EQ(2u, allocation.constant_pool().size());
        EXPECT_EQ(
            0u, reinterpret_cast<uintptr_t>(allocation.constant_pool().data()) %
                    16);
        EXPECT_EQ(0u, fixture.platform->last_slab->committed_code_offset);
        EXPECT_EQ(32u, fixture.platform->last_slab->committed_code_size);
        EXPECT_EQ(0xfff0u, fixture.platform->last_slab->committed_pool_offset);
        EXPECT_EQ(2u, fixture.platform->last_slab->committed_pool_size);
        EXPECT_EQ(allocation.writable_code().data(),
                  fixture.platform->last_slab->write_pointer_at(0));
        EXPECT_NE(
            reinterpret_cast<uintptr_t>(allocation.writable_code().data()),
            allocation.code.execute_address().bits_for_indirect_target());
        MachineAddress execute_address = allocation.code.execute_address();
        EXPECT_TRUE(fixture.platform->last_slab->code_write_active);
        EXPECT_EQ(1u, fixture.platform->last_slab->begin_code_write_count);
        Result<void, JitCodeError> publication =
            fixture.cache->publish(allocation);
        ASSERT_TRUE(publication);
        EXPECT_EQ(execute_address, allocation.code.execute_address());
        EXPECT_FALSE(fixture.platform->last_slab->code_write_active);
        EXPECT_EQ(1u, fixture.platform->last_slab->end_code_write_count);
    }

    TEST(CodeCache, DroppingProposalPreservesBothFrontiers)
    {
        CacheAndPlatform fixture(16);
        MachineAddress first_code = detail::MachineAddressAccess::from_bits(1);
        MachineAddress first_pool = detail::MachineAddressAccess::from_bits(1);
        {
            CodeAllocationProposal proposal =
                take_proposal(fixture.cache->propose(33, 3, 8));
            first_code = proposal.code_address();
            first_pool = proposal.constant_pool_address();
        }

        CodeAllocationProposal replacement =
            take_proposal(fixture.cache->propose(33, 3, 8));
        EXPECT_EQ(first_code, replacement.code_address());
        EXPECT_EQ(first_pool, replacement.constant_pool_address());
        EXPECT_EQ(1u, fixture.platform->requested_sizes.size());
    }

    TEST(CodeCache, AbandoningAllocationRestoresCodeWriteProtection)
    {
        CacheAndPlatform fixture(16);
        {
            CodeAllocationProposal proposal =
                take_proposal(fixture.cache->propose(16, 0, 1));
            CodeAllocation allocation = take_allocation(proposal.commit(16));
            EXPECT_TRUE(fixture.platform->last_slab->code_write_active);
        }

        EXPECT_FALSE(fixture.platform->last_slab->code_write_active);
        EXPECT_EQ(1u, fixture.platform->last_slab->begin_code_write_count);
        EXPECT_EQ(1u, fixture.platform->last_slab->end_code_write_count);
    }

    TEST(CodeCache, PublicationRecoversPessimisticCodeSlack)
    {
        CacheAndPlatform fixture(16);
        CodeAllocationProposal proposal =
            take_proposal(fixture.cache->propose(100, 8, 8));
        MachineAddress expected_pool_address = proposal.constant_pool_address();
        CodeAllocation allocation = take_allocation(proposal.commit(17));
        EXPECT_EQ(32u, allocation.code.capacity());
        ASSERT_TRUE(fixture.cache->publish(allocation));

        EXPECT_EQ(17u, allocation.encoded_code_size());
        EXPECT_EQ(32u, allocation.code.capacity());
        EXPECT_EQ(expected_pool_address, allocation.constant_pool_address());
        EXPECT_EQ(17u,
                  fixture.platform->last_slab->published_encoded_code_size);
        EXPECT_EQ(32u, fixture.platform->last_slab->published_protected_size);

        CodeAllocationProposal next =
            take_proposal(fixture.cache->propose(16, 0, 1));
        EXPECT_EQ(allocation.code.execute_address().offset_by(32),
                  next.code_address());
    }

    TEST(CodeCache, PublicationFailureConsumesTheCommittedSpace)
    {
        CacheAndPlatform fixture(16);
        MachineAddress failed_address =
            detail::MachineAddressAccess::from_bits(1);
        fixture.platform->fail_publication = true;
        {
            CodeAllocationProposal proposal =
                take_proposal(fixture.cache->propose(64, 8, 8));
            failed_address = proposal.code_address();
            CodeAllocation allocation = take_allocation(proposal.commit(32));
            Result<void, JitCodeError> publication =
                fixture.cache->publish(allocation);
            ASSERT_FALSE(publication);
            EXPECT_EQ(JitCodeError::PublicationFailure, publication.error());
            EXPECT_FALSE(fixture.platform->last_slab->code_write_active);
        }

        fixture.platform->fail_publication = false;
        CodeAllocationProposal replacement =
            take_proposal(fixture.cache->propose(64, 8, 8));
        EXPECT_EQ(failed_address.offset_by(32), replacement.code_address());
    }

    TEST(CodeCache, AllocationFailureIsRecoverable)
    {
        CacheAndPlatform fixture(16);
        fixture.platform->fail_allocation = true;

        Result<CodeAllocationProposal, JitCodeError> allocation =
            fixture.cache->propose(16, 8, 8);

        ASSERT_FALSE(allocation);
        EXPECT_EQ(JitCodeError::AllocationFailure, allocation.error());
    }

    TEST(CodeCache, CommitFailureConsumesCodeAndPoolSpace)
    {
        CacheAndPlatform fixture(16);
        CodeAllocationProposal proposal =
            take_proposal(fixture.cache->propose(64, 8, 8));
        MachineAddress failed_code = proposal.code_address();
        MachineAddress failed_pool = proposal.constant_pool_address();
        fixture.platform->fail_commit = true;

        Result<CodeAllocation, JitCodeError> allocation = proposal.commit(32);

        ASSERT_FALSE(allocation);
        EXPECT_EQ(JitCodeError::AllocationFailure, allocation.error());

        fixture.platform->fail_commit = false;
        CodeAllocationProposal replacement =
            take_proposal(fixture.cache->propose(16, 8, 8));
        EXPECT_EQ(failed_code.offset_by(32), replacement.code_address());
        EXPECT_EQ(-int64_t{8}, failed_pool.displacement_to(
                                   replacement.constant_pool_address()));
    }

    TEST(CodeCache, UsesDedicatedSlabForAnOversizedUnit)
    {
        CacheAndPlatform fixture(16);
        EXPECT_TRUE(fixture.cache->propose(64 * 1024, 8, 8));

        ASSERT_EQ(1u, fixture.platform->requested_sizes.size());
        EXPECT_EQ(68u * 1024, fixture.platform->requested_sizes[0]);
    }

    TEST(CodeCache, PacksPoolSlicesIntoSharedPoolPages)
    {
        CacheAndPlatform fixture(16);
        CodeAllocationProposal first_proposal =
            take_proposal(fixture.cache->propose(16, 8, 8));
        MachineAddress first_pool_address =
            first_proposal.constant_pool_address();
        CodeAllocation first = take_allocation(first_proposal.commit(16));
        ASSERT_TRUE(fixture.cache->publish(first));
        CodeAllocationProposal second_proposal =
            take_proposal(fixture.cache->propose(16, 8, 8));
        MachineAddress second_pool_address =
            second_proposal.constant_pool_address();
        CodeAllocation second = take_allocation(second_proposal.commit(16));
        ASSERT_TRUE(fixture.cache->publish(second));

        uintptr_t first_pool = first_pool_address.bits_for_indirect_target();
        uintptr_t second_pool = second_pool_address.bits_for_indirect_target();
        EXPECT_EQ(8u, first_pool - second_pool);
        EXPECT_EQ(first_pool / CacheAndPlatform::PageSize,
                  second_pool / CacheAndPlatform::PageSize);
        EXPECT_EQ(1u, fixture.platform->requested_sizes.size());
    }

    TEST(CodeCache, NeverAllocatesCodeIntoAnExistingPoolPage)
    {
        CacheAndPlatform fixture(16);
        CodeAllocationProposal first_proposal =
            take_proposal(fixture.cache->propose(15 * 4096, 8, 8));
        CodeAllocation first =
            take_allocation(first_proposal.commit(15 * 4096));
        ASSERT_TRUE(fixture.cache->publish(first));
        CodeAllocationProposal second =
            take_proposal(fixture.cache->propose(16, 0, 1));

        EXPECT_EQ(2u, fixture.platform->requested_sizes.size());
        EXPECT_NE(first.code.execute_address(), second.code_address());
    }

    TEST(CodeCache, PageGranularityRecoversOnlyWholePages)
    {
        CacheAndPlatform fixture(CacheAndPlatform::PageSize);
        CodeAllocationProposal first_proposal =
            take_proposal(fixture.cache->propose(9000, 0, 1));
        MachineAddress empty_pool_address =
            first_proposal.constant_pool_address();
        CodeAllocation first = take_allocation(first_proposal.commit(5000));
        EXPECT_TRUE(first.constant_pool().empty());
        EXPECT_EQ(empty_pool_address, first.constant_pool_address());
        ASSERT_TRUE(fixture.cache->publish(first));
        CodeAllocationProposal second =
            take_proposal(fixture.cache->propose(1, 0, 1));

        EXPECT_EQ(2 * CacheAndPlatform::PageSize, first.code.capacity());
        EXPECT_EQ(first.code.execute_address().offset_by(
                      2 * CacheAndPlatform::PageSize),
                  second.code_address());
    }

    TEST(StandardCodeMemory, PublishesCodeWithoutProtectingPoolPages)
    {
        auto platform = std::make_unique<StandardCodeMemory>();
        size_t page_size = platform->page_size();
        CodeCache cache(std::move(platform));
        CodeAllocationProposal proposal = take_proposal(cache.propose(4, 1, 1));
        CodeAllocation allocation = take_allocation(proposal.commit(4));
        auto *code =
            reinterpret_cast<uint8_t *>(allocation.writable_code().data());
        code[0] = 0;
        code[1] = 0;
        code[2] = 0;
        code[3] = 0;
        allocation.constant_pool()[0] = std::byte{0x11};

        ASSERT_TRUE(cache.publish(allocation));
        allocation.constant_pool()[0] = std::byte{0x22};

        EXPECT_EQ(std::byte{0x22}, allocation.constant_pool()[0]);
        EXPECT_EQ(page_size, allocation.code.capacity());
    }

    TEST(CodeCacheLifetime, GivesEachThreadAVmOwnedCache)
    {
        VirtualMachine vm;
        ThreadState *first = vm.get_default_thread();
        ThreadState *second = vm.make_new_thread();

        EXPECT_NE(&first->code_cache(), &second->code_cache());
    }

}  // namespace cl::jit
