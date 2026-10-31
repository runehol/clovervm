#include "memory/heap_constants.h"
#include "memory/heap_space.h"
#include "object_model/value_tags.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

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
        EXPECT_NE(nullptr, pointer);
        EXPECT_EQ(value_refcounted_ptr_tag, address(pointer) & value_tag_mask);
    }
}  // namespace

TEST(HeapSpace, SingleMembersHaveRoundedSpacingAndNoCompanion)
{
    HeapSpace space;
    std::byte *expected_next = nullptr;
    for(size_t size: {size_t{1}, size_t{8}, size_t{31}, size_t{32}, size_t{33},
                      size_t{64}, size_t{65}, size_t{127}})
    {
        GroupReservation group = space.reserve_group(std::array{size});
        ASSERT_EQ(1u, group.count);
        ASSERT_NE(nullptr, group.members[0]);
        EXPECT_EQ(nullptr, group.members[1]);
        expect_storage_tag(group.members[0]);
        if(expected_next != nullptr)
        {
            EXPECT_EQ(expected_next, group.members[0]);
        }
        group.members[0][0] = std::byte{0xa5};
        group.members[0][size - 1] = std::byte{0x5a};
        expected_next = group.members[0] + rounded(size);
    }
}

TEST(HeapSpace, PairUsesAllocatedExtentsAndPreservesPadding)
{
    HeapSpace space;
    GroupReservation pair =
        space.reserve_group(std::array{size_t{33}, size_t{65}});
    ASSERT_EQ(2u, pair.count);
    ASSERT_NE(nullptr, pair.members[0]);
    ASSERT_NE(nullptr, pair.members[1]);
    expect_storage_tag(pair.members[0]);
    expect_storage_tag(pair.members[1]);
    EXPECT_EQ(pair.members[0] + rounded(33), pair.members[1]);
    pair.members[0][32] = std::byte{0x11};
    pair.members[1][64] = std::byte{0x22};

    GroupReservation next = space.reserve_group(std::array{size_t{8}});
    EXPECT_EQ(pair.members[0] + rounded(33) + rounded(65), next.members[0]);
    EXPECT_EQ(std::byte{0x11}, pair.members[0][32]);
    EXPECT_EQ(std::byte{0x22}, pair.members[1][64]);
}

TEST(HeapSpace, PairDoesNotSplitAcrossSlabBoundary)
{
    HeapSpace space;
    size_t first_size = default_usable - stride;
    GroupReservation first = space.reserve_group(std::array{first_size});
    first.members[0][0] = std::byte{0x71};
    first.members[0][first_size - 1] = std::byte{0x72};

    // One member could fit in the old slab's tail, but the complete pair
    // cannot.
    GroupReservation pair = space.reserve_group(std::array{stride, stride});
    ASSERT_EQ(2u, pair.count);
    EXPECT_NE(first.members[0] + first_size, pair.members[0]);
    EXPECT_EQ(pair.members[0] + stride, pair.members[1]);
    expect_storage_tag(pair.members[0]);
    expect_storage_tag(pair.members[1]);
    EXPECT_EQ(std::byte{0x71}, first.members[0][0]);
    EXPECT_EQ(std::byte{0x72}, first.members[0][first_size - 1]);
}

TEST(HeapSpace, ExactSlabEndGrowsWithoutInvalidatingEarlierBytes)
{
    HeapSpace space;
    GroupReservation first = space.reserve_group(std::array{default_usable});
    first.members[0][0] = std::byte{0x31};
    first.members[0][default_usable - 1] = std::byte{0x32};
    uintptr_t mapping_start =
        address(first.members[0]) - value_refcounted_ptr_tag;

    GroupReservation next = space.reserve_group(std::array{size_t{8}});
    EXPECT_TRUE(address(next.members[0]) < mapping_start ||
                address(next.members[0]) >= mapping_start + DefaultSlabSize);
    expect_storage_tag(next.members[0]);
    next.members[0][0] = std::byte{0x44};
    EXPECT_EQ(std::byte{0x31}, first.members[0][0]);
    EXPECT_EQ(std::byte{0x32}, first.members[0][default_usable - 1]);
}

TEST(HeapSpace, OversizedPairReceivesOneLargeReservation)
{
    HeapSpace space;
    size_t primary_size = DefaultSlabSize + 7;
    size_t companion_size = DefaultSlabSize + 5;
    GroupReservation pair =
        space.reserve_group(std::array{primary_size, companion_size});
    ASSERT_EQ(2u, pair.count);
    expect_storage_tag(pair.members[0]);
    expect_storage_tag(pair.members[1]);
    EXPECT_EQ(pair.members[0] + rounded(primary_size), pair.members[1]);
    pair.members[0][primary_size - 1] = std::byte{0x61};
    pair.members[1][companion_size - 1] = std::byte{0x62};

    GroupReservation next = space.reserve_group(std::array{size_t{8}});
    EXPECT_EQ(pair.members[0] + rounded(primary_size) + rounded(companion_size),
              next.members[0]);
    EXPECT_EQ(std::byte{0x61}, pair.members[0][primary_size - 1]);
    EXPECT_EQ(std::byte{0x62}, pair.members[1][companion_size - 1]);
}

TEST(HeapSpace, ManySlabsKeepAllEarlierReservationsAlive)
{
    HeapSpace space;
    std::array<std::byte *, 16> addresses{};
    for(size_t index = 0; index < addresses.size(); ++index)
    {
        auto group = space.reserve_group(std::array{DefaultSlabSize});
        addresses[index] = group.members[0];
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
    auto outer = survivor.reserve_group(std::array{size_t{8}});
    outer.members[0][0] = std::byte{0x7e};
    {
        HeapSpace temporary;
        auto inner = temporary.reserve_group(std::array{DefaultSlabSize + 1});
        EXPECT_NE(outer.members[0], inner.members[0]);
        inner.members[0][DefaultSlabSize] = std::byte{0x55};
    }
    EXPECT_EQ(std::byte{0x7e}, outer.members[0][0]);
}

TEST(HeapSpace, OwnershipCannotBeCopiedOrMoved)
{
    static_assert(!std::is_copy_constructible_v<HeapSpace>);
    static_assert(!std::is_copy_assignable_v<HeapSpace>);
    static_assert(!std::is_move_constructible_v<HeapSpace>);
    static_assert(!std::is_move_assignable_v<HeapSpace>);
    static_assert(std::is_nothrow_constructible_v<HeapSpace>);
}

TEST(HeapSpaceDeathTest, RejectsInvalidGroupCounts)
{
    HeapSpace space;
    EXPECT_DEATH((void)space.reserve_group({}), "one or two members");
    EXPECT_DEATH(
        (void)space.reserve_group(std::array{size_t{8}, size_t{8}, size_t{8}}),
        "one or two members");
}

TEST(HeapSpaceDeathTest, RejectsZeroExtent)
{
    HeapSpace space;
    EXPECT_DEATH((void)space.reserve_group(std::array{size_t{0}}),
                 "must be positive");
    EXPECT_DEATH((void)space.reserve_group(std::array{size_t{8}, size_t{0}}),
                 "must be positive");
}

TEST(HeapSpaceDeathTest, RejectsRoundingOverflow)
{
    HeapSpace space;
    EXPECT_DEATH((void)space.reserve_group(std::array{SIZE_MAX}),
                 "size overflow");
}

TEST(HeapSpaceDeathTest, RejectsGroupSumOverflow)
{
    HeapSpace space;
    constexpr size_t huge = SIZE_MAX & ~(stride - 1);
    EXPECT_DEATH((void)space.reserve_group(std::array{huge, huge}),
                 "size overflow");
}

TEST(HeapSpaceDeathTest, RejectsUnrepresentableMappingBeforeMmap)
{
    HeapSpace space;
    constexpr size_t huge =
        static_cast<size_t>(std::numeric_limits<ptrdiff_t>::max());
    EXPECT_DEATH((void)space.reserve_group(std::array{huge}),
                 "mapping is too large");
}
