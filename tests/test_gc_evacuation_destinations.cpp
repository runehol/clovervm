#include "gc/evacuation_destinations.h"

#include "builtin_types/float.h"
#include "builtin_types/tuple.h"
#include "memory/heap_constants.h"
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
using namespace cl::gc;

namespace
{
    bool contains_address(const HeapSpace &space, const HeapObject *object)
    {
        uintptr_t address = reinterpret_cast<uintptr_t>(object);
        bool found = false;
        space.visit_memory_ranges([&](uintptr_t begin, uintptr_t end) {
            found |= begin <= address && address < end;
        });
        return found;
    }
}  // namespace

TEST(GCEvacuationDestinations, PreparesHeadersAndPreservesExactFloatPayloads)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    HeapSpace old;
    HeapSpace non_moving;
    EvacuationDestinations destinations(old, non_moving);
    for(uint64_t bits:
        {uint64_t{0}, uint64_t{0x8000000000000000},
         uint64_t{0x3ff8000000000000}, uint64_t{1},
         uint64_t{0x7ff0000000000000}, uint64_t{0xfff0000000000000},
         uint64_t{0x7ff8000000000042}, uint64_t{0xfff8000000000123}})
    {
        Owned<TValue<Float>> source(context.thread()->make_object_value<Float>(
            std::bit_cast<double>(bits)));
        for(auto generation:
            {HeapGenerationState::Young, HeapGenerationState::Old,
             HeapGenerationState::OldRemembered})
        {
            source.extract()->set_generation_state(generation);
            std::array<std::byte, sizeof(Float)> before;
            std::memcpy(before.data(), source.extract(), before.size());
            auto *ordinary = static_cast<Float *>(
                destinations.copy_to_old(source.extract()));
            auto *fixed = static_cast<Float *>(
                destinations.copy_to_non_moving(source.extract()));
            for(Float *copy: {ordinary, fixed})
            {
                EXPECT_NE(source.extract(), copy);
                EXPECT_EQ(bits, std::bit_cast<uint64_t>(copy->value()));
                EXPECT_EQ(HeapGenerationState::Old, copy->generation_state());
                EXPECT_EQ(copy == fixed, copy->is_non_moving());
                EXPECT_FALSE(copy->is_forwarded());
                EXPECT_EQ(source.extract()->lifecycle_state(),
                          copy->lifecycle_state());
                EXPECT_EQ(source.extract()->native_layout_id(),
                          copy->native_layout_id());
                EXPECT_EQ(source.extract()->native_layout_aux_count_value(),
                          copy->native_layout_aux_count_value());
                EXPECT_EQ(source.extract()->refcount(), copy->refcount());
                EXPECT_EQ(0, std::memcmp(before.data() + sizeof(HeapObject),
                                         reinterpret_cast<std::byte *>(copy) +
                                             sizeof(HeapObject),
                                         sizeof(Float) - sizeof(HeapObject)));
            }
            EXPECT_TRUE(contains_address(old, ordinary));
            EXPECT_FALSE(contains_address(non_moving, ordinary));
            EXPECT_TRUE(contains_address(non_moving, fixed));
            EXPECT_FALSE(contains_address(old, fixed));
            EXPECT_EQ(
                0, std::memcmp(before.data(), source.extract(), before.size()));
            // Restore runtime state before normal RC teardown. No runtime
            // allocation or safepoint occurs while the test labels it Young.
            source.extract()->set_generation_state(HeapGenerationState::Old);
        }
    }
}

TEST(GCEvacuationDestinations, CopiesDynamicExtentsWithoutRCOrZctTraffic)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Owned<TValue<Float>> child(context.thread()->make_object_value<Float>(3.5));
    for(size_t count: {size_t{0}, size_t{1}, size_t{5}})
    {
        Owned<TValue<Tuple>> source(
            context.thread()->make_object_value<Tuple>(count));
        for(size_t index = 0; index < count; ++index)
        {
            source.extract()->initialize_item_unchecked(index,
                                                        child.raw_value());
        }
        int32_t child_refs = child.extract()->refcount();
        int32_t source_refs = source.extract()->refcount();
        int32_t shape_refs = source.extract()->get_shape()->refcount();
        size_t zct_size = context.thread()->zero_count_table_size();
        {
            HeapSpace old;
            HeapSpace non_moving;
            EvacuationDestinations destinations(old, non_moving);
            auto *ordinary = static_cast<Tuple *>(
                destinations.copy_to_old(source.extract()));
            auto *fixed = static_cast<Tuple *>(
                destinations.copy_to_non_moving(source.extract()));
            for(Tuple *copy: {ordinary, fixed})
            {
                EXPECT_EQ(count, copy->size());
                EXPECT_EQ(source.extract()->get_shape(), copy->get_shape());
                for(size_t index = 0; index < count; ++index)
                {
                    EXPECT_EQ(child.raw_value(), copy->item_unchecked(index));
                }
            }
            size_t allocated = Tuple::allocation_size_for(count);
            size_t rounded = (allocated + value_ptr_granularity - 1) &
                             ~(value_ptr_granularity - 1);
            EXPECT_EQ(reinterpret_cast<uintptr_t>(ordinary) + rounded,
                      reinterpret_cast<uintptr_t>(old.allocate(1)));
            EXPECT_EQ(reinterpret_cast<uintptr_t>(fixed) + rounded,
                      reinterpret_cast<uintptr_t>(non_moving.allocate(1)));
            EXPECT_EQ(child_refs, child.extract()->refcount());
            EXPECT_EQ(source_refs, source.extract()->refcount());
            EXPECT_EQ(shape_refs, source.extract()->get_shape()->refcount());
            EXPECT_EQ(zct_size, context.thread()->zero_count_table_size());
        }
        // Destroying destination storage must not release copied members.
        EXPECT_EQ(child_refs, child.extract()->refcount());
        EXPECT_EQ(source_refs, source.extract()->refcount());
        EXPECT_EQ(shape_refs, source.extract()->get_shape()->refcount());
        EXPECT_EQ(zct_size, context.thread()->zero_count_table_size());
        EXPECT_FALSE(source.extract()->is_forwarded());
    }
}

TEST(GCEvacuationDestinations, BorrowsPopulatedSpacesAcrossGrowthAndTeardown)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Owned<TValue<Float>> source(
        context.thread()->make_object_value<Float>(1.5));
    OverflowSlots *overflow =
        context.thread()->make_internal_raw<OverflowSlots>(0u, 2u);
    source.extract()->overflow_storage = overflow;
    HeapSpace old;
    HeapSpace non_moving;
    Float *first_old;
    Float *first_fixed;
    {
        EvacuationDestinations first(old, non_moving);
        first_old = static_cast<Float *>(first.copy_to_old(source.extract()));
        first_fixed =
            static_cast<Float *>(first.copy_to_non_moving(source.extract()));
    }
    std::array<std::byte, sizeof(Float)> before_old;
    std::array<std::byte, sizeof(Float)> before_fixed;
    std::memcpy(before_old.data(), first_old, before_old.size());
    std::memcpy(before_fixed.data(), first_fixed, before_fixed.size());
    int32_t overflow_refs = overflow->refcount();
    {
        EvacuationDestinations next(old, non_moving);
        for(size_t index = 0; index < 3; ++index)
        {
            // Force new mappings, without constructing additional VM objects.
            (void)old.allocate(DefaultSlabSize + 1);
            (void)non_moving.allocate(DefaultSlabSize + 1);
            HeapObject *ordinary = next.copy_to_old(source.extract());
            HeapObject *fixed = next.copy_to_non_moving(source.extract());
            EXPECT_NE(first_old, ordinary);
            EXPECT_NE(first_fixed, fixed);
            EXPECT_TRUE(contains_address(old, ordinary));
            EXPECT_TRUE(contains_address(non_moving, fixed));
        }
    }
    EXPECT_EQ(0, std::memcmp(before_old.data(), first_old, before_old.size()));
    EXPECT_EQ(
        0, std::memcmp(before_fixed.data(), first_fixed, before_fixed.size()));
    EXPECT_EQ(overflow_refs, overflow->refcount());
    EXPECT_EQ(overflow, first_old->overflow_storage.get());
    EXPECT_EQ(overflow, first_fixed->overflow_storage.get());
}

TEST(GCEvacuationDestinationsDeathTest, RejectsAliasedDestinationSpaces)
{
    HeapSpace space;
    EXPECT_DEATH((void)EvacuationDestinations(space, space), "distinct spaces");
}
