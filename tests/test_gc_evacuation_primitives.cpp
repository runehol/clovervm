#include "gc_test_heap.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace cl::gc::test
{
    TEST(GCCollectionParticipation, NonMovingFactoryCreatesAnExistingOldObject)
    {
        TestHeap heap;
        TestObject *before = heap.make_object();
        TestObject *fixed = heap.make_non_moving_object();
        TestObject *after = heap.make_object();
        EXPECT_EQ(TestStorage::NonMoving, fixed->storage);
        EXPECT_EQ(TestGeneration::Old, fixed->generation);
        EXPECT_EQ(TestHeaderKind::Normal, fixed->header.kind);
        EXPECT_TRUE(fixed->strong_cells.empty());
        EXPECT_TRUE(fixed->weak_targets.empty());
        EXPECT_TRUE(fixed->weak_entries.empty());
        EXPECT_NE(before->logical_identity, fixed->logical_identity);
        EXPECT_NE(after->logical_identity, fixed->logical_identity);
        ASSERT_EQ(3u, heap.objects().size());
        EXPECT_EQ(fixed, heap.objects()[1].get());
        EXPECT_EQ(0u,
                  heap.destination_object_count(EvacuationDestination::Stable));
    }

    TEST(GCCollectionParticipation, SeparatesParticipationFromMovability)
    {
        for(auto kind:
            {TestCollectionKind::YoungOnly, TestCollectionKind::Full})
        {
            TestHeap heap;
            TestObject *young = heap.make_object();
            TestObject *old = heap.make_object();
            old->generation = TestGeneration::Old;
            TestObject *fixed = heap.make_non_moving_object();
            TestObject *promoted_source = heap.make_object();
            promoted_source->preferred_destination =
                EvacuationDestination::Stable;
            TestHeapAdapter adapter(heap, kind);
            TestObject *ordinary_copy = adapter.copy_object(young);
            TestObject *stable_copy = adapter.copy_object(promoted_source);
            EXPECT_TRUE(adapter.is_collected(young));
            EXPECT_TRUE(adapter.is_movable(young));
            EXPECT_EQ(kind == TestCollectionKind::Full,
                      adapter.is_collected(old));
            EXPECT_TRUE(adapter.is_movable(old));
            EXPECT_EQ(kind == TestCollectionKind::Full,
                      adapter.is_collected(fixed));
            EXPECT_FALSE(adapter.is_movable(fixed));
            // Full collections treat existing and newly copied NonMoving
            // objects alike. Raw copying alone does not mark or scan either.
            EXPECT_EQ(TestGeneration::Old, ordinary_copy->generation);
            EXPECT_EQ(TestGeneration::Old, stable_copy->generation);
            for(TestObject *copy: {ordinary_copy, stable_copy})
            {
                EXPECT_EQ(kind == TestCollectionKind::Full &&
                              copy == stable_copy,
                          adapter.is_collected(copy));
                EXPECT_FALSE(adapter.is_marked(copy));
                EXPECT_EQ(0u, adapter.scan_count(copy));
            }
            EXPECT_TRUE(adapter.is_movable(ordinary_copy));
            EXPECT_FALSE(adapter.is_movable(stable_copy));
            EXPECT_FALSE(adapter.is_marked(fixed));
            EXPECT_EQ(0u, adapter.scan_count(fixed));
        }
    }

    TEST(GCCollectionParticipation, ForwardingDoesNotChangeInputParticipation)
    {
        TestHeap heap;
        TestHeapAdapter minor(heap, TestCollectionKind::YoungOnly);
        TestHeapAdapter full(heap, TestCollectionKind::Full);
        TestObject *young = heap.make_object();
        TestObject *old = heap.make_object();
        old->generation = TestGeneration::Old;
        old->preferred_destination = EvacuationDestination::Stable;
        for(TestObject *source: {young, old})
        {
            EXPECT_TRUE(full.is_collected(source));
            TestObject *copy = full.copy_object(source);
            full.install_forwarding(source, copy);
            EXPECT_TRUE(full.is_collected(source));
            EXPECT_TRUE(full.is_movable(source));
            EXPECT_EQ(source == young, minor.is_collected(source));
            EXPECT_TRUE(minor.is_movable(source));
            EXPECT_EQ(source == young, full.is_movable(copy));
            EXPECT_EQ(source == old, full.is_collected(copy));
            EXPECT_FALSE(minor.is_collected(copy));
        }
    }

    TEST(GCEvacuationPrimitives, CopyPreservesUnrepairedReferences)
    {
        TestHeap heap;
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        TestObject *source = heap.make_object();
        TestObject *child = heap.make_object();
        source->strong_cells = {TestCell::immediate(42),
                                TestCell::reference(child),
                                TestCell::reference(source)};
        source->weak_targets = {child, nullptr};
        TestObject *copy = adapter.copy_object(source);
        ASSERT_NE(nullptr, copy);
        EXPECT_EQ(0u, reinterpret_cast<uintptr_t>(copy) % alignof(TestObject));
        EXPECT_NE(source, copy);
        EXPECT_EQ(source->logical_identity, copy->logical_identity);
        EXPECT_EQ(source->strong_cells, copy->strong_cells);
        EXPECT_EQ(source->weak_targets, copy->weak_targets);
        EXPECT_EQ(TestHeaderKind::Normal, copy->header.kind);
        EXPECT_EQ(TestStorage::OrdinaryDestination, copy->storage);
        EXPECT_FALSE(adapter.is_collected(copy));
        EXPECT_TRUE(adapter.is_movable(copy));
        EXPECT_EQ(nullptr, adapter.forwarded_to(source));
        copy->strong_cells[0] = TestCell::immediate(99);
        EXPECT_EQ(TestCell::immediate(42), source->strong_cells[0]);
        adapter.install_forwarding(source, copy);
        EXPECT_TRUE(adapter.is_collected(source));
        EXPECT_TRUE(adapter.is_movable(source));
        EXPECT_EQ(copy, adapter.forwarded_to(source));
        TestCell root = TestCell::reference(source);
        TestSlot(root).rewrite_pointer(copy);
        EXPECT_EQ(copy, TestSlot(root).load_pointer());
    }

    TEST(GCEvacuationPrimitives, ChunkGrowthDoesNotMoveEarlierCopies)
    {
        TestHeap heap(2);
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        TestObject *first = heap.make_object();
        first->strong_cells = {TestCell::immediate(123)};
        TestObject *initial = adapter.copy_object(first);
        uintptr_t address = reinterpret_cast<uintptr_t>(initial);
        for(size_t idx = 0; idx < 20; ++idx)
        {
            adapter.copy_object(heap.make_object());
        }
        EXPECT_EQ(address, reinterpret_cast<uintptr_t>(initial));
        EXPECT_EQ(first->logical_identity, initial->logical_identity);
        EXPECT_EQ(TestCell::immediate(123), initial->strong_cells[0]);
        EXPECT_EQ(21u, heap.destination_object_count(
                           EvacuationDestination::Ordinary));
    }

    TEST(GCEvacuationPrimitives, PlacementIsChosenIndependentlyForEachObject)
    {
        TestHeap heap(1);
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        TestObject *source = heap.make_object();
        TestObject *child = heap.make_object();
        source->preferred_destination = EvacuationDestination::Stable;
        source->strong_cells = {TestCell::reference(child)};
        TestObject *stable = adapter.copy_object(source);
        EXPECT_EQ(TestStorage::StableDestination, stable->storage);
        EXPECT_TRUE(adapter.is_collected(stable));
        EXPECT_FALSE(adapter.is_movable(stable));
        EXPECT_EQ(child, TestSlot(stable->strong_cells[0]).load_pointer());
        EXPECT_EQ(
            0u, heap.destination_object_count(EvacuationDestination::Ordinary));

        TestObject *ordinary = adapter.copy_object(child);
        EXPECT_EQ(TestStorage::OrdinaryDestination, ordinary->storage);
        EXPECT_FALSE(adapter.is_collected(ordinary));
        EXPECT_TRUE(adapter.is_movable(ordinary));
        EXPECT_EQ(1u,
                  heap.destination_chunk_count(EvacuationDestination::Stable));
        EXPECT_EQ(
            1u, heap.destination_chunk_count(EvacuationDestination::Ordinary));
    }

    TEST(GCEvacuationPrimitives, DestinationExhaustionIsFatal)
    {
        for(auto destination:
            {EvacuationDestination::Ordinary, EvacuationDestination::Stable})
        {
            TestHeap heap(1);
            TestHeapAdapter adapter(heap, TestCollectionKind::Full);
            TestObject *source = heap.make_object();
            source->preferred_destination = destination;
            TestObject *copy = adapter.copy_object(source);
            adapter.install_forwarding(source, copy);
            heap.limit_destination_chunks(destination, 1);
            TestObject *next = heap.make_object();
            next->preferred_destination = destination;
            // Even after forwarding has been published, exhaustion must abort,
            // not throw back into a mutator with a partially moved heap.
            EXPECT_DEATH(adapter.copy_object(next),
                         "test GC allocation failed");
        }
    }
}  // namespace cl::gc::test
