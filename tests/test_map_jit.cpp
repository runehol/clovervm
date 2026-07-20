#include "jit/code_cache.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

namespace cl::jit
{
    TEST(MapJit, PreferredCachePacksPublishedCodeAtSixteenBytes)
    {
        if(!pthread_jit_write_protect_supported_np())
        {
            GTEST_SKIP() << "per-thread JIT write protection is unavailable";
        }

        CodeCache cache;
        Result<CodeAllocationProposal, JitCodeError> first_proposal_result =
            cache.propose(4, 0);
        ASSERT_TRUE(first_proposal_result);
        CodeAllocationProposal first_proposal =
            std::move(first_proposal_result).value();
        MachineAddress first_address = first_proposal.code_address();

        Result<CodeAllocation, JitCodeError> first_allocation_result =
            first_proposal.commit(4);
        ASSERT_TRUE(first_allocation_result);
        CodeAllocation first_allocation =
            std::move(first_allocation_result).value();
        *static_cast<uint32_t *>(first_allocation.write_pointer()) =
            0xd65f03c0;  // ret
        ASSERT_TRUE(cache.publish(std::move(first_allocation)));

        Result<CodeAllocationProposal, JitCodeError> second_proposal_result =
            cache.propose(4, 0);
        ASSERT_TRUE(second_proposal_result);
        CodeAllocationProposal second_proposal =
            std::move(second_proposal_result).value();
        EXPECT_EQ(first_address.offset_by(16), second_proposal.code_address());
    }

    TEST(MapJit, ReplacesUnusedPageWithWritableDataAtSameAddress)
    {
        if(!pthread_jit_write_protect_supported_np())
        {
            GTEST_SKIP() << "per-thread JIT write protection is unavailable";
        }

        long page_size_result = sysconf(_SC_PAGESIZE);
        ASSERT_GT(page_size_result, 0);
        size_t page_size = static_cast<size_t>(page_size_result);
        size_t slab_size = 2 * page_size;

        void *mapping =
            mmap(nullptr, slab_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
        ASSERT_NE(MAP_FAILED, mapping);

        auto *code = static_cast<uint32_t *>(mapping);
        pthread_jit_write_protect_np(false);
        code[0] = 0xd2800540;  // mov x0, #42
        code[1] = 0xd65f03c0;  // ret
        pthread_jit_write_protect_np(true);
        __builtin___clear_cache(reinterpret_cast<char *>(code),
                                reinterpret_cast<char *>(code + 2));

        auto *pool_address = static_cast<uint8_t *>(mapping) + page_size;
        ASSERT_EQ(0, munmap(pool_address, page_size));
        void *pool = mmap(pool_address, page_size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
        ASSERT_EQ(pool_address, pool);

        auto *slot = static_cast<uint64_t *>(pool);
        *slot = 0x123456789abcdef0;
        EXPECT_EQ(0x123456789abcdef0u, *slot);

        using Function = uint64_t (*)();
        Function function = reinterpret_cast<Function>(mapping);
        EXPECT_EQ(42u, function());

        EXPECT_EQ(0, munmap(mapping, page_size));
        EXPECT_EQ(0, munmap(pool, page_size));
    }
}  // namespace cl::jit
