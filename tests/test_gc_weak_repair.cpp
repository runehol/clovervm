#include "gc/evacuation.h"
#include "gc/weak_repair.h"
#include "gc_test_heap.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <new>
#include <type_traits>

namespace cl::gc::test
{
    namespace
    {
        using Unit = WeakRepairUnit<TestObject>;
        static_assert(sizeof(Unit) == 2 * sizeof(void *));
        static_assert(sizeof(Unit) == 16);
        static_assert(std::is_trivially_copyable_v<Unit>);

        TestObject *pointer(const TestCell &cell)
        {
            auto value = std::get_if<TestObject *>(&cell.contents);
            return value == nullptr ? nullptr : *value;
        }

        TestObject *copy_root(Evacuation<TestHeapAdapter> &collection,
                              TestObject *source)
        {
            TestCell root = TestCell::reference(source);
            collection.add_root(TestSlot(root));
            return pointer(root);
        }

        struct ThrowingUnit
        {
            size_t calls = 0;
            static WeakRepairResult repair(ThrowingUnit &unit,
                                           WeakResolver<TestObject>)
            {
                if(++unit.calls == 1)
                {
                    return WeakRepairResult::Pending;
                }
                throw std::bad_alloc();
            }
        };

        WeakRepairResult throw_early(TestObject *&, WeakResolver<TestObject>)
        {
            throw std::bad_alloc();
        }
    }  // namespace

    TEST(GCWeakRepair, StableAndAlreadyForwardedUnitsCompleteEarly)
    {
        TestHeap heap(1);
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        TestObject stable{};
        stable.storage = TestStorage::StableDestination;
        TestObject *target = heap.make_object();
        TestObject *source = heap.make_object();
        source->strong_cells = {TestCell::reference(target)};
        source->weak_entries = {
            TestWeakEntry::active(&stable, &stable, TestCell::immediate(17)),
            TestWeakEntry::active(target, &stable,
                                  TestCell::reference(target))};
        Evacuation<TestHeapAdapter> collection(adapter);
        TestObject *copy = copy_root(collection, source);
        collection.drain();
        ASSERT_EQ(2u, copy->weak_entries.size());
        EXPECT_EQ(&stable, copy->weak_entries[0].guards[0]);
        EXPECT_EQ(TestCell::immediate(17), copy->weak_entries[0].payload);
        EXPECT_EQ(adapter.forwarded_to(target),
                  copy->weak_entries[1].guards[0]);
        EXPECT_EQ(adapter.forwarded_to(target),
                  pointer(copy->weak_entries[1].payload));
        collection.finish();
        for(const TestWeakEntry &entry: copy->weak_entries)
        {
            EXPECT_EQ(1u, entry.repair_count);
            EXPECT_EQ(0u, entry.reset_count);
        }
        EXPECT_EQ(1u, adapter.weak_scan_count(copy));
        // Source units are never repaired; only final destination units are.
        EXPECT_EQ(0u, source->weak_entries[0].repair_count);
        EXPECT_EQ(target, source->weak_entries[1].guards[0]);
    }

    TEST(GCWeakRepair, PendingFieldDoesNotPreventOtherFieldsFromResolving)
    {
        TestHeap heap(1);
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        TestObject *source = heap.make_object();
        TestObject *child = heap.make_object();
        TestObject *later = heap.make_object();
        source->strong_cells = {TestCell::reference(child)};
        child->strong_cells = {TestCell::reference(later)};
        source->weak_entries = {
            TestWeakEntry::active(later, child, TestCell::reference(child))};
        Evacuation<TestHeapAdapter> collection(adapter);
        TestObject *copy = copy_root(collection, source);
        collection.drain();
        TestWeakEntry &entry = copy->weak_entries[0];
        EXPECT_EQ(later, entry.guards[0]);
        EXPECT_EQ(adapter.forwarded_to(child), entry.guards[1]);
        EXPECT_EQ(adapter.forwarded_to(child), pointer(entry.payload));
        EXPECT_EQ(1u, entry.repair_count);
        ASSERT_NE(nullptr, adapter.forwarded_to(later));
        collection.finish();
        EXPECT_EQ(adapter.forwarded_to(later), entry.guards[0]);
        EXPECT_EQ(2u, entry.repair_count);
        EXPECT_EQ(0u, entry.reset_count);
        EXPECT_EQ(1u, adapter.weak_scan_count(copy));
    }

    TEST(GCWeakRepair, AdjacentEntriesDeferIndependentlyAndResetWholePayload)
    {
        TestHeap heap(1);
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        TestObject stable{};
        stable.storage = TestStorage::StableDestination;
        TestObject *source = heap.make_object();
        TestObject *dead_guard = heap.make_object();
        TestObject *dead_payload = heap.make_object();
        source->weak_entries = {
            TestWeakEntry::active(&stable, &stable, TestCell::immediate(19)),
            TestWeakEntry::active(dead_guard, &stable, TestCell::immediate(23)),
            TestWeakEntry::active(&stable, &stable,
                                  TestCell::reference(dead_payload)),
            TestWeakEntry::active(&stable, &stable,
                                  TestCell::reference(nullptr))};
        Evacuation<TestHeapAdapter> collection(adapter);
        TestObject *copy = copy_root(collection, source);
        collection.drain();
        EXPECT_EQ(1u, copy->weak_entries[1].repair_count);
        EXPECT_EQ(dead_guard, copy->weak_entries[1].guards[0]);
        EXPECT_EQ(TestCell::immediate(23), copy->weak_entries[1].payload);
        collection.finish();
        EXPECT_EQ(1u, copy->weak_entries[0].repair_count);
        EXPECT_EQ(TestCell::immediate(19), copy->weak_entries[0].payload);
        EXPECT_EQ(1u, copy->weak_entries[3].repair_count);
        for(size_t idx: {size_t{1}, size_t{2}})
        {
            const TestWeakEntry &entry = copy->weak_entries[idx];
            EXPECT_EQ(2u, entry.repair_count);
            EXPECT_EQ(1u, entry.reset_count);
            EXPECT_EQ(nullptr, entry.guards[0]);
            EXPECT_EQ(nullptr, entry.guards[1]);
            EXPECT_EQ(TestCell::immediate(0), entry.payload);
        }
        EXPECT_EQ(nullptr, adapter.forwarded_to(dead_guard));
        EXPECT_EQ(nullptr, adapter.forwarded_to(dead_payload));
        EXPECT_EQ(1u, adapter.weak_scan_count(copy));
        EXPECT_EQ(
            1u, heap.destination_object_count(EvacuationDestination::Ordinary));
    }

    TEST(GCWeakRepair, ExplicitUnitsAllowRootsToArriveBeforeFinish)
    {
        TestHeap heap;
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        TestObject *target = heap.make_object();
        TestObject *dead = heap.make_object();
        TestObject *weak_root = target;
        TestObject *weak_dead = dead;
        TestObject *weak_null = nullptr;
        Evacuation<TestHeapAdapter> collection(adapter);
        collection.add_weak_root(Unit::bind<repair_test_weak_field>(weak_root));
        collection.add_weak_root(Unit::bind<repair_test_weak_field>(weak_dead));
        collection.add_weak_root(Unit::bind<repair_test_weak_field>(weak_null));
        EXPECT_EQ(target, weak_root);
        TestObject *copy = copy_root(collection, target);
        collection.finish();
        EXPECT_EQ(copy, weak_root);
        EXPECT_EQ(nullptr, weak_dead);
        EXPECT_EQ(nullptr, weak_null);
        EXPECT_EQ(nullptr, adapter.forwarded_to(dead));
    }

    TEST(GCWeakRepair, InvalidGuardClearsPayloadWithoutDeferral)
    {
        TestHeap heap;
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        TestObject *dead = heap.make_object();
        TestWeakEntry entry =
            TestWeakEntry::active(nullptr, dead, TestCell::reference(dead));
        Evacuation<TestHeapAdapter> collection(adapter);
        collection.add_weak_root(Unit::bind<repair_test_weak_entry>(entry));
        EXPECT_EQ(1u, entry.reset_count);
        EXPECT_EQ(nullptr, entry.guards[1]);
        EXPECT_EQ(TestCell::immediate(0), entry.payload);
        collection.finish();
        EXPECT_EQ(1u, entry.repair_count);
        EXPECT_EQ(nullptr, adapter.forwarded_to(dead));
    }

    TEST(GCWeakRepair, DeferredUnitsSurviveQueueAndDestinationChunkGrowth)
    {
        TestHeap heap(1);
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        TestObject *source = heap.make_object();
        TestObject *branch = heap.make_object();
        source->strong_cells = {TestCell::reference(branch)};
        for(size_t idx = 0; idx < 512; ++idx)
        {
            TestObject *target = heap.make_object();
            branch->strong_cells.push_back(TestCell::reference(target));
            source->weak_entries.push_back(TestWeakEntry::active(
                target, branch, TestCell::immediate(idx)));
        }
        Evacuation<TestHeapAdapter> collection(adapter);
        TestObject *copy = copy_root(collection, source);
        collection.drain();
        for(size_t idx = 0; idx < copy->weak_entries.size(); ++idx)
        {
            EXPECT_EQ(source->weak_entries[idx].guards[0],
                      copy->weak_entries[idx].guards[0]);
            EXPECT_EQ(adapter.forwarded_to(branch),
                      copy->weak_entries[idx].guards[1]);
        }
        collection.finish();
        for(size_t idx = 0; idx < copy->weak_entries.size(); ++idx)
        {
            const TestWeakEntry &entry = copy->weak_entries[idx];
            EXPECT_EQ(adapter.forwarded_to(source->weak_entries[idx].guards[0]),
                      entry.guards[0]);
            EXPECT_EQ(TestCell::immediate(idx), entry.payload);
            EXPECT_EQ(2u, entry.repair_count);
            EXPECT_EQ(0u, entry.reset_count);
        }
        EXPECT_EQ(514u, heap.destination_chunk_count(
                            EvacuationDestination::Ordinary));
        EXPECT_EQ(1u, adapter.weak_scan_count(copy));
    }

    TEST(GCWeakRepair, ExternalOldOwnerNeedsOnlyItsExactRememberedUnit)
    {
        TestHeap heap;
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        TestObject *target = heap.make_object();
        TestObject *dead = heap.make_object();
        std::array entries = {
            TestWeakEntry::active(target, target, TestCell::immediate(1)),
            TestWeakEntry::active(dead, dead, TestCell::immediate(2))};
        Evacuation<TestHeapAdapter> collection(adapter);
        collection.add_weak_root(
            Unit::bind<repair_test_weak_entry>(entries[1]));
        collection.finish();
        EXPECT_EQ(0u, entries[0].repair_count);
        EXPECT_EQ(target, entries[0].guards[0]);
        EXPECT_EQ(1u, entries[1].reset_count);
        EXPECT_EQ(
            0u, heap.destination_object_count(EvacuationDestination::Ordinary));
    }

    TEST(GCWeakRepair, UnitCallbackInspectsAllFieldsBeforeCoordinatedReset)
    {
        TestObject first{};
        TestObject second{};
        TestObject payload{};
        TestWeakEntry entry = TestWeakEntry::active(
            &first, &second, TestCell::reference(&payload));
        size_t calls = 0;
        auto resolver = [&](TestObject *) {
            ++calls;
            return WeakResolution<TestObject>::pending();
        };
        EXPECT_EQ(WeakRepairResult::Pending,
                  repair_test_weak_entry(entry, resolver));
        EXPECT_EQ(3u, calls);
        calls = 0;
        auto late = [&](TestObject *target) {
            ++calls;
            return target == &first
                       ? WeakResolution<TestObject>::dead()
                       : WeakResolution<TestObject>::resolved(target);
        };
        EXPECT_EQ(WeakRepairResult::Complete,
                  repair_test_weak_entry(entry, late));
        EXPECT_EQ(3u, calls);
        EXPECT_EQ(1u, entry.reset_count);
        EXPECT_EQ(TestCell::immediate(0), entry.payload);
    }

    TEST(GCWeakRepair,
         FinishAllowsExplicitSourceDisposalWithoutDanglingHeapEdges)
    {
        TestHeap heap(1);
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        TestObject *owner = heap.make_object();
        TestObject *backing = heap.make_object();
        TestObject *child = heap.make_object();
        TestObject *dead = heap.make_object();
        owner->strong_cells = {TestCell::reference(backing)};
        backing->strong_cells = {TestCell::reference(child)};
        child->strong_cells = {TestCell::reference(owner)};
        owner->weak_targets = {child, dead};
        owner->weak_entries = {
            TestWeakEntry::active(child, backing, TestCell::reference(child))};
        child->preferred_destination = EvacuationDestination::Stable;
        TestObject *weak_root = dead;
        Evacuation<TestHeapAdapter> collection(adapter);
        collection.add_weak_root(Unit::bind<repair_test_weak_field>(weak_root));
        TestObject *copy = copy_root(collection, owner);
        collection.finish();
        TestObject *backing_copy = adapter.forwarded_to(backing);
        TestObject *child_copy = adapter.forwarded_to(child);
        ASSERT_NE(nullptr, backing_copy);
        ASSERT_NE(nullptr, child_copy);
        heap.discard_source_storage(TestCollectionKind::Full);
        EXPECT_TRUE(heap.objects().empty());
        EXPECT_EQ(nullptr, weak_root);
        EXPECT_EQ(backing_copy, pointer(copy->strong_cells[0]));
        EXPECT_EQ(child_copy, pointer(backing_copy->strong_cells[0]));
        EXPECT_EQ(copy, pointer(child_copy->strong_cells[0]));
        EXPECT_EQ(child_copy, copy->weak_targets[0]);
        EXPECT_EQ(nullptr, copy->weak_targets[1]);
        EXPECT_EQ(child_copy, copy->weak_entries[0].guards[0]);
        EXPECT_EQ(backing_copy, copy->weak_entries[0].guards[1]);
        EXPECT_EQ(child_copy, pointer(copy->weak_entries[0].payload));
        EXPECT_EQ(0u, copy->weak_entries[0].reset_count);
    }

    TEST(GCWeakRepair, NativeAllocationFailureDuringEarlyAndLateRepairIsFatal)
    {
        TestHeap heap;
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        TestObject *root = nullptr;
        Evacuation<TestHeapAdapter> collection(adapter);
        EXPECT_DEATH(collection.add_weak_root(Unit::bind<throw_early>(root)),
                     "test GC allocation failed");
        ThrowingUnit unit;
        collection.add_weak_root(Unit::bind<ThrowingUnit::repair>(unit));
        EXPECT_EQ(1u, unit.calls);
        EXPECT_DEATH(collection.finish(), "test GC allocation failed");
    }

#ifndef NDEBUG
    TEST(GCWeakRepair, FinishSealsCollectionAgainstNewStrongOrWeakRoots)
    {
        TestHeap heap;
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        Evacuation<TestHeapAdapter> collection(adapter);
        collection.finish();
        TestCell root = TestCell::reference(nullptr);
        TestObject *weak = nullptr;
        EXPECT_DEATH(collection.add_root(TestSlot(root)), "phase_");
        EXPECT_DEATH(
            collection.add_weak_root(Unit::bind<repair_test_weak_field>(weak)),
            "phase_");
        EXPECT_DEATH(collection.drain(), "phase_");
        EXPECT_DEATH(collection.finish(), "phase_");
    }
#endif
}  // namespace cl::gc::test
