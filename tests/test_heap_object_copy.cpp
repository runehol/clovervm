#include "builtin_types/float.h"
#include "memory/heap_constants.h"
#include "memory/heap_object_copy.h"
#include "memory/heap_space.h"
#include "object_model/overflow_slots.h"
#include "object_model/shape.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

using namespace cl;

TEST(HeapObjectCopy, PreservesExactFloatBitsAndHeaderWithoutChangingSource)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    HeapSpace destination;
    for(uint64_t bits:
        {uint64_t{0}, uint64_t{0x8000000000000000},
         uint64_t{0x3ff8000000000000}, uint64_t{0xbfe3333333333333},
         uint64_t{1}, uint64_t{0x7ff0000000000000},
         uint64_t{0xfff0000000000000}, uint64_t{0x7ff8000000000042},
         uint64_t{0xfff8000000000123}})
    {
        SCOPED_TRACE(bits);
        Owned<TValue<Float>> source(context.thread()->make_object_value<Float>(
            std::bit_cast<double>(bits)));
        std::array<std::byte, sizeof(Float)> before;
        std::memcpy(before.data(), source.extract(), before.size());

        // This is a borrowed byte copy, never an additional RC owner.
        Float *copy = static_cast<Float *>(
            copy_object_bytes(&destination, source.extract()));
        EXPECT_NE(source.extract(), copy);
        EXPECT_EQ(value_refcounted_ptr_tag,
                  reinterpret_cast<uintptr_t>(copy) & value_tag_mask);
        EXPECT_EQ(bits, std::bit_cast<uint64_t>(copy->value()));
        EXPECT_EQ(source.extract()->get_shape(), copy->get_shape());
        EXPECT_EQ(nullptr, copy->overflow_storage.get());
        EXPECT_EQ(0, std::memcmp(before.data(), copy, before.size()));
        EXPECT_EQ(0,
                  std::memcmp(before.data(), source.extract(), before.size()));
        EXPECT_FALSE(source.extract()->is_forwarded());
    }
}

TEST(HeapObjectCopy, CopiesMemberPointersWithoutRetainingOrReleasingThem)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Owned<TValue<Float>> source(
        context.thread()->make_object_value<Float>(2.5));
    OverflowSlots *overflow =
        context.thread()->make_internal_raw<OverflowSlots>(0u, 2u);
    // Exercise the inherited member's retaining-copy hazard without requiring
    // an attribute mutation on an immutable Float. Slot lookup is not involved.
    source.extract()->overflow_storage = overflow;
    int32_t source_refs = source.extract()->refcount();
    int32_t overflow_refs = overflow->refcount();
    Shape *shape = source.extract()->get_shape();
    int32_t shape_refs = shape->refcount();
    {
        HeapSpace destination;
        Float *first = static_cast<Float *>(
            copy_object_bytes(&destination, source.extract()));
        Float *second = static_cast<Float *>(
            copy_object_bytes(&destination, source.extract()));
        EXPECT_NE(first, second);
        EXPECT_EQ(overflow, first->overflow_storage.get());
        EXPECT_EQ(overflow, second->overflow_storage.get());
        EXPECT_EQ(shape, first->get_shape());
        EXPECT_EQ(source_refs, source.extract()->refcount());
        EXPECT_EQ(overflow_refs, overflow->refcount());
        EXPECT_EQ(shape_refs, shape->refcount());
    }
    // Discarding raw destination storage must not destroy copied members.
    EXPECT_EQ(source_refs, source.extract()->refcount());
    EXPECT_EQ(overflow_refs, overflow->refcount());
    EXPECT_EQ(shape_refs, shape->refcount());
    EXPECT_EQ(2.5, source.extract()->value());
}

TEST(HeapObjectCopy, CopiesSurviveDestinationSlabGrowth)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Owned<TValue<Float>> source(
        context.thread()->make_object_value<Float>(1.5));
    HeapSpace destination;
    Float *first =
        static_cast<Float *>(copy_object_bytes(&destination, source.extract()));
    // More than two slabs of Float copies, without constructing VM sources in
    // the loop or registering any destination with RC reclamation.
    for(size_t idx = 0; idx < 2 * DefaultSlabSize / sizeof(Float); ++idx)
    {
        Float *copy = static_cast<Float *>(
            copy_object_bytes(&destination, source.extract()));
        EXPECT_NE(first, copy);
        EXPECT_EQ(1.5, copy->value());
        EXPECT_EQ(source.extract()->get_shape(), copy->get_shape());
    }
    EXPECT_EQ(1.5, first->value());
    EXPECT_EQ(source.extract()->get_shape(), first->get_shape());
}
