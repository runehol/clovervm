#include "memory/heap_constants.h"
#include "memory/heap_space.h"
#include "object_model/value_tags.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

using namespace cl;

namespace
{
    constexpr size_t stride = value_ptr_granularity;
    constexpr size_t default_usable =
        (DefaultSlabSize - value_refcounted_ptr_tag) / stride * stride;

    uintptr_t address(std::byte *pointer)
    {
        return reinterpret_cast<uintptr_t>(pointer);
    }

    size_t rounded(size_t size) { return (size + stride - 1) & ~(stride - 1); }

    void expect_storage_tag(std::byte *pointer)
    {
        ASSERT_NE(nullptr, pointer);
        EXPECT_EQ(value_refcounted_ptr_tag, address(pointer) & value_tag_mask);
    }
}  // namespace

TEST(HeapSpace, AllocationsHaveRoundedSpacingAndStorageTags)
{
    HeapSpace space;
    std::byte *expected_next = nullptr;
    for(size_t size: {size_t{1}, size_t{8}, size_t{31}, size_t{32}, size_t{33},
                      size_t{64}, size_t{65}, size_t{127}})
    {
        std::byte *allocation = space.allocate(size);
        ASSERT_NE(nullptr, allocation);
        expect_storage_tag(allocation);
        if(expected_next != nullptr)
        {
            EXPECT_EQ(expected_next, allocation);
        }
        allocation[0] = std::byte{0xa5};
        allocation[size - 1] = std::byte{0x5a};
        expected_next = allocation + rounded(size);
    }
}

TEST(HeapSpace, AllocationDoesNotSplitAcrossSlabBoundary)
{
    HeapSpace space;
    size_t first_size = default_usable - stride;
    std::byte *first = space.allocate(first_size);
    first[0] = std::byte{0x71};
    first[first_size - 1] = std::byte{0x72};
    uintptr_t mapping_start = address(first) - value_refcounted_ptr_tag;

    // The remaining tail fits one stride, but not this allocation.
    std::byte *next = space.allocate(stride * 2);
    EXPECT_TRUE(address(next) < mapping_start ||
                address(next) >= mapping_start + DefaultSlabSize);
    expect_storage_tag(next);
    next[stride * 2 - 1] = std::byte{0x73};
    EXPECT_EQ(std::byte{0x71}, first[0]);
    EXPECT_EQ(std::byte{0x72}, first[first_size - 1]);
}

TEST(HeapSpace, ExactSlabEndGrowsWithoutInvalidatingEarlierBytes)
{
    HeapSpace space;
    std::byte *first = space.allocate(default_usable);
    first[0] = std::byte{0x31};
    first[default_usable - 1] = std::byte{0x32};
    uintptr_t mapping_start = address(first) - value_refcounted_ptr_tag;

    std::byte *next = space.allocate(8);
    EXPECT_TRUE(address(next) < mapping_start ||
                address(next) >= mapping_start + DefaultSlabSize);
    expect_storage_tag(next);
    next[0] = std::byte{0x44};
    EXPECT_EQ(std::byte{0x31}, first[0]);
    EXPECT_EQ(std::byte{0x32}, first[default_usable - 1]);
}

TEST(HeapSpace, OversizedAllocationReceivesALargeSlab)
{
    HeapSpace space;
    size_t size = DefaultSlabSize + 7;
    std::byte *large = space.allocate(size);
    expect_storage_tag(large);
    large[0] = std::byte{0x61};
    large[size - 1] = std::byte{0x62};

    std::byte *next = space.allocate(8);
    EXPECT_EQ(large + rounded(size), next);
    expect_storage_tag(next);
    next[0] = std::byte{0x63};
    EXPECT_EQ(std::byte{0x61}, large[0]);
    EXPECT_EQ(std::byte{0x62}, large[size - 1]);
}

TEST(HeapSpace, ManySlabsKeepAllEarlierAllocationsAlive)
{
    HeapSpace space;
    std::array<std::byte *, 16> addresses{};
    for(size_t index = 0; index < addresses.size(); ++index)
    {
        addresses[index] = space.allocate(DefaultSlabSize);
        addresses[index][0] = static_cast<std::byte>(index);
        addresses[index][DefaultSlabSize - 1] =
            static_cast<std::byte>(index + 1);
    }
    for(size_t index = 0; index < addresses.size(); ++index)
    {
        EXPECT_EQ(static_cast<std::byte>(index), addresses[index][0]);
        EXPECT_EQ(static_cast<std::byte>(index + 1),
                  addresses[index][DefaultSlabSize - 1]);
        for(size_t other = 0; other < index; ++other)
        {
            EXPECT_NE(addresses[index], addresses[other]);
        }
    }
}

TEST(HeapSpace, DestroyingOneSpaceDoesNotAffectAnother)
{
    HeapSpace survivor;
    std::byte *outer = survivor.allocate(8);
    outer[0] = std::byte{0x7e};
    {
        HeapSpace temporary;
        std::byte *inner = temporary.allocate(DefaultSlabSize + 1);
        EXPECT_NE(outer, inner);
        inner[DefaultSlabSize] = std::byte{0x55};
    }
    EXPECT_EQ(std::byte{0x7e}, outer[0]);
}

TEST(HeapSpace, OwnershipCannotBeCopiedOrMoved)
{
    static_assert(!std::is_copy_constructible_v<HeapSpace>);
    static_assert(!std::is_copy_assignable_v<HeapSpace>);
    static_assert(!std::is_move_constructible_v<HeapSpace>);
    static_assert(!std::is_move_assignable_v<HeapSpace>);
    static_assert(std::is_nothrow_constructible_v<HeapSpace>);
}

TEST(HeapSpace, VisitsHalfOpenMappingsWithoutEnumeratingObjects)
{
    HeapSpace space;
    std::vector<std::pair<uintptr_t, uintptr_t>> ranges;
    auto record = [&](uintptr_t begin, uintptr_t end) {
        ranges.emplace_back(begin, end);
    };
    space.visit_memory_ranges(record);
    EXPECT_TRUE(ranges.empty());
    std::byte *small = space.allocate(8);
    space.visit_memory_ranges(record);
    ASSERT_EQ(1u, ranges.size());
    EXPECT_EQ(address(small) - value_refcounted_ptr_tag, ranges[0].first);
    EXPECT_EQ(ranges[0].first + DefaultSlabSize, ranges[0].second);

    size_t extent = DefaultSlabSize * 2 + 7;
    std::byte *large = space.allocate(extent);
    auto original = ranges[0];
    ranges.clear();
    space.visit_memory_ranges(record);
    ASSERT_EQ(2u, ranges.size());
    size_t small_matches = 0;
    size_t large_matches = 0;
    for(auto [begin, end]: ranges)
    {
        EXPECT_LT(begin, end);
        EXPECT_EQ(0u, begin % SlabLookupGranuleSize);
        EXPECT_EQ(0u, end % SlabLookupGranuleSize);
        if(begin == original.first)
        {
            EXPECT_EQ(original.second, end);
            ++small_matches;
        }
        if(begin <= address(large) && address(large) < end)
        {
            EXPECT_LE(address(large) + extent, end);
            ++large_matches;
        }
    }
    EXPECT_EQ(1u, small_matches);
    EXPECT_EQ(1u, large_matches);
}

TEST(HeapSpaceDeathTest, RejectsZeroExtent)
{
    HeapSpace space;
    EXPECT_DEATH((void)space.allocate(0), "must be positive");
}

TEST(HeapSpaceDeathTest, RejectsAllocationRoundingOverflow)
{
    HeapSpace space;
    EXPECT_DEATH((void)space.allocate(SIZE_MAX), "size overflow");
}

TEST(HeapSpaceDeathTest, RejectsMappingRoundingOverflow)
{
    HeapSpace space;
    constexpr size_t huge = SIZE_MAX & ~(stride - 1);
    EXPECT_DEATH((void)space.allocate(huge), "size overflow");
}

TEST(HeapSpaceDeathTest, RejectsUnrepresentableMappingBeforeMmap)
{
    HeapSpace space;
    constexpr size_t huge =
        static_cast<size_t>(std::numeric_limits<ptrdiff_t>::max());
    EXPECT_DEATH((void)space.allocate(huge), "mapping is too large");
}
