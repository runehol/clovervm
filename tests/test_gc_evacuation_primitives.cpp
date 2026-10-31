#include "gc/strong_closure.h"
#include "gc_test_heap.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace cl::gc::test
{
    TEST(GCEvacuationPrimitives, SingletonCopyPreservesUnrepairedReferences)
    {
        TestHeap heap;
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        TestObject *source = heap.make_object();
        TestObject *child = heap.make_object();
        source->strong_cells = {TestCell::immediate(42),
                                TestCell::reference(child),
                                TestCell::reference(source)};
        source->weak_targets = {child, nullptr};
        auto group = adapter.allocation_group(source);
        ASSERT_EQ(1u, group.count);
        EXPECT_EQ(source, group.members[0]);
        EXPECT_EQ(nullptr, group.members[1]);
        auto copies = adapter.copy_group(group, adapter.destination_for(group));
        ASSERT_NE(nullptr, copies[0]);
        EXPECT_EQ(nullptr, copies[1]);
        TestObject *copy = copies[0];
        EXPECT_NE(source, copy);
        EXPECT_EQ(source->logical_identity, copy->logical_identity);
        EXPECT_EQ(source->strong_cells, copy->strong_cells);
        EXPECT_EQ(source->weak_targets, copy->weak_targets);
        EXPECT_EQ(TestHeaderKind::Normal, copy->header.kind);
        EXPECT_EQ(TestStorage::OrdinaryDestination, copy->storage);
        EXPECT_FALSE(adapter.is_collected_source(copy));
        EXPECT_EQ(nullptr, adapter.forwarded_to(source));
        copy->strong_cells[0] = TestCell::immediate(99);
        EXPECT_EQ(TestCell::immediate(42), source->strong_cells[0]);
        adapter.install_forwarding(source, copy);
        EXPECT_TRUE(adapter.is_collected_source(source));
        EXPECT_EQ(copy, adapter.forwarded_to(source));
        TestCell root = TestCell::reference(source);
        TestSlot(root).rewrite_pointer(copy);
        EXPECT_EQ(copy, TestSlot(root).load_pointer());
    }

    TEST(GCEvacuationPrimitives, PairIsAdjacentAndForwardingIsExplicit)
    {
        TestHeap heap(1);
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        auto sources = heap.make_pair();
        auto group = adapter.allocation_group(sources.members[0]);
        ASSERT_EQ(2u, group.count);
        EXPECT_EQ(sources.members, group.members);
        auto copies =
            adapter.copy_group(group, EvacuationDestination::Ordinary);
        EXPECT_EQ(sizeof(TestObject),
                  reinterpret_cast<uintptr_t>(copies[1]) -
                      reinterpret_cast<uintptr_t>(copies[0]));
        EXPECT_EQ(0u,
                  reinterpret_cast<uintptr_t>(copies[0]) % alignof(TestObject));
        EXPECT_EQ(copies[0], copies[1]->exclusive_owner);
        // Copying is not scanning: the real companion cell remains a source
        // reference until the future traversal explicitly repairs it.
        EXPECT_EQ(sources.members[1],
                  TestSlot(copies[0]->companion).load_pointer());
        for(size_t idx = 0; idx < group.count; ++idx)
        {
            EXPECT_EQ(nullptr, adapter.forwarded_to(group.members[idx]));
            EXPECT_EQ(TestHeaderKind::Normal, copies[idx]->header.kind);
            adapter.install_forwarding(group.members[idx], copies[idx]);
            EXPECT_EQ(copies[idx], adapter.forwarded_to(group.members[idx]));
        }
        TestSlot(copies[0]->companion).rewrite_pointer(copies[1]);
        EXPECT_EQ(copies[1], TestSlot(copies[0]->companion).load_pointer());
        EXPECT_EQ(
            1u, heap.destination_chunk_count(EvacuationDestination::Ordinary));
        EXPECT_EQ(
            2u, heap.destination_object_count(EvacuationDestination::Ordinary));
    }

    TEST(GCEvacuationPrimitives,
         ChunkGrowthDoesNotSplitGroupsOrMoveEarlierCopies)
    {
        TestHeap heap(2);
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        TestObject *first = heap.make_object();
        first->strong_cells = {TestCell::immediate(123)};
        auto initial = adapter.copy_group(adapter.allocation_group(first),
                                          EvacuationDestination::Ordinary);
        uintptr_t address = reinterpret_cast<uintptr_t>(initial[0]);
        auto pair = heap.make_pair();
        auto copies = adapter.copy_group(pair, EvacuationDestination::Ordinary);
        EXPECT_EQ(sizeof(TestObject),
                  reinterpret_cast<uintptr_t>(copies[1]) -
                      reinterpret_cast<uintptr_t>(copies[0]));
        EXPECT_EQ(
            2u, heap.destination_chunk_count(EvacuationDestination::Ordinary));
        for(size_t idx = 0; idx < 20; ++idx)
        {
            adapter.copy_group(adapter.allocation_group(heap.make_object()),
                               EvacuationDestination::Ordinary);
        }
        EXPECT_EQ(address, reinterpret_cast<uintptr_t>(initial[0]));
        EXPECT_EQ(first->logical_identity, initial[0]->logical_identity);
        EXPECT_EQ(TestCell::immediate(123), initial[0]->strong_cells[0]);
        EXPECT_EQ(23u, heap.destination_object_count(
                           EvacuationDestination::Ordinary));
    }

    TEST(GCEvacuationPrimitives, StablePreferenceAppliesToWholeGroup)
    {
        TestHeap heap(1);
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        auto pair = heap.make_pair();
        pair.members[1]->preferred_destination = EvacuationDestination::Stable;
        EXPECT_EQ(EvacuationDestination::Stable, adapter.destination_for(pair));
        auto stable = adapter.copy_group(pair, adapter.destination_for(pair));
        auto ordinary =
            adapter.copy_group(adapter.allocation_group(heap.make_object()),
                               EvacuationDestination::Ordinary);
        for(TestObject *object: stable)
        {
            EXPECT_EQ(TestStorage::StableDestination, object->storage);
            EXPECT_FALSE(adapter.is_collected_source(object));
        }
        EXPECT_EQ(TestStorage::OrdinaryDestination, ordinary[0]->storage);
        EXPECT_EQ(1u,
                  heap.destination_chunk_count(EvacuationDestination::Stable));
        EXPECT_EQ(
            1u, heap.destination_chunk_count(EvacuationDestination::Ordinary));
    }

    TEST(GCEvacuationPrimitives, CompanionIsStrongButOwnershipBacklinkIsNot)
    {
        TestHeap heap;
        auto pair = heap.make_pair();
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        size_t references = 0;
        adapter.visit_strong_slots(pair.members[0], [&](TestSlot slot) {
            if(slot.load_pointer() != nullptr)
            {
                ++references;
                EXPECT_EQ(pair.members[1], slot.load_pointer());
            }
        });
        EXPECT_EQ(1u, references);
        adapter.visit_strong_slots(pair.members[1], [&](TestSlot slot) {
            EXPECT_EQ(nullptr, slot.load_pointer());
        });
    }

    TEST(GCEvacuationPrimitives, DestinationExhaustionIsFatal)
    {
        for(auto destination:
            {EvacuationDestination::Ordinary, EvacuationDestination::Stable})
        {
            TestHeap heap(1);
            TestHeapAdapter adapter(heap, TestCollectionKind::Full);
            auto group = adapter.allocation_group(heap.make_object());
            auto copy = adapter.copy_group(group, destination);
            adapter.install_forwarding(group.members[0], copy[0]);
            heap.limit_destination_chunks(destination, 1);
            auto next = adapter.allocation_group(heap.make_object());
            // Even after forwarding has been published, exhaustion must abort,
            // not throw back into a mutator with a partially moved heap.
            EXPECT_DEATH(adapter.copy_group(next, destination),
                         "test GC allocation failed");
        }
    }
}  // namespace cl::gc::test
