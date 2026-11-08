#include "gc/evacuation.h"
#include "gc/weak_repair.h"
#include "gc_test_heap.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <type_traits>
#include <vector>

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

    TEST(GCWeakRepair, TaggedStandaloneNonPointersNeverReachResolver)
    {
        const std::array originals = {
            TestCell::immediate(0), TestCell::immediate(-7),
            TestCell::immediate(std::numeric_limits<int64_t>::min()),
            TestCell::immediate(std::numeric_limits<int64_t>::max()),
            TestCell::reference(nullptr)};
        for(TestCell cell: originals)
        {
            TestCell original = cell;
            EXPECT_EQ(WeakRepairResult::Complete,
                      Unit::bind<repair_test_weak_field>(cell).try_repair(
                          [](TestObject *) {
                              ADD_FAILURE()
                                  << "nonpointer weak cell reached resolver";
                              return WeakResolution<TestObject>::dead();
                          }));
            EXPECT_EQ(original, cell);
        }
    }

    TEST(GCWeakRepair, TaggedStandalonePointersPreservePendingAndRepairInPlace)
    {
        TestHeap heap;
        TestObject *source = heap.make_object();
        TestObject *replacement = heap.make_object();
        TestCell cell = TestCell::reference(source);
        Unit unit = Unit::bind<repair_test_weak_field>(cell);
        size_t calls = 0;
        EXPECT_EQ(WeakRepairResult::Pending,
                  unit.try_repair([&](TestObject *target) {
                      ++calls;
                      EXPECT_EQ(source, target);
                      return WeakResolution<TestObject>::pending();
                  }));
        EXPECT_EQ(TestCell::reference(source), cell);
        EXPECT_EQ(WeakRepairResult::Complete,
                  unit.try_repair([&](TestObject *target) {
                      ++calls;
                      EXPECT_EQ(source, target);
                      return WeakResolution<TestObject>::resolved(replacement);
                  }));
        EXPECT_EQ(TestCell::reference(replacement), cell);
        EXPECT_EQ(WeakRepairResult::Complete,
                  unit.try_repair([&](TestObject *target) {
                      ++calls;
                      EXPECT_EQ(replacement, target);
                      return WeakResolution<TestObject>::dead();
                  }));
        EXPECT_EQ(TestCell::reference(nullptr), cell);
        EXPECT_EQ(
            WeakRepairResult::Complete, unit.try_repair([&](TestObject *) {
                ++calls;
                ADD_FAILURE() << "cleared weak reference reached resolver";
                return WeakResolution<TestObject>::pending();
            }));
        EXPECT_EQ(3u, calls);
    }

    TEST(GCWeakRepair, OwnerAndExternalTaggedCellsRepairAfterStrongClosure)
    {
        for(auto destination:
            {EvacuationDestination::Ordinary, EvacuationDestination::Stable})
        {
            TestHeap heap(1);
            TestObject *owner = heap.make_object();
            TestObject *bridge = heap.make_object();
            TestObject *live = heap.make_object();
            TestObject *dead = heap.make_object();
            owner->preferred_destination = destination;
            live->preferred_destination = destination;
            owner->strong_cells = {TestCell::reference(bridge)};
            bridge->strong_cells = {TestCell::reference(live)};
            std::array external = {
                TestCell::immediate(0),
                TestCell::immediate(-7),
                TestCell::immediate(std::numeric_limits<int64_t>::min()),
                TestCell::immediate(std::numeric_limits<int64_t>::max()),
                TestCell::reference(nullptr),
                TestCell::reference(live),
                TestCell::reference(dead),
                TestCell::reference(live)};
            owner->weak_cells.assign(external.begin(), external.end());
            const auto original_external = external;
            const auto original_owner_cells = owner->weak_cells;
            TestHeapAdapter adapter(heap, TestCollectionKind::Full);
            Evacuation<TestHeapAdapter> collection(adapter);
            for(TestCell &cell: external)
            {
                collection.add_weak_root(
                    Unit::bind<repair_test_weak_field>(cell));
            }
            EXPECT_EQ(original_external, external);
            EXPECT_FALSE(adapter.is_marked(live));
            EXPECT_FALSE(adapter.is_marked(dead));
            TestObject *copy = copy_root(collection, owner);
            collection.drain();
            // The owner was visited before bridge discovered live. Its weak
            // cells and the external cells keep their Pending references until
            // late repair, despite live now having a forwarding destination.
            EXPECT_EQ(original_owner_cells, copy->weak_cells);
            EXPECT_EQ(original_external, external);
            TestObject *live_copy = adapter.forwarded_to(live);
            ASSERT_NE(nullptr, live_copy);
            EXPECT_FALSE(adapter.is_marked(dead));
            EXPECT_EQ(nullptr, adapter.forwarded_to(dead));
            EXPECT_EQ(0u, adapter.scan_count(dead));
            collection.finish();
            auto expected = original_external;
            expected[5] = TestCell::reference(live_copy);
            expected[6] = TestCell::reference(nullptr);
            expected[7] = TestCell::reference(live_copy);
            EXPECT_EQ(expected, external);
            EXPECT_EQ(std::vector<TestCell>(expected.begin(), expected.end()),
                      copy->weak_cells);
            EXPECT_EQ(original_owner_cells, owner->weak_cells);
            heap.discard_source_storage(TestCollectionKind::Full);
            EXPECT_EQ(expected, external);
            EXPECT_EQ(std::vector<TestCell>(expected.begin(), expected.end()),
                      copy->weak_cells);
        }
    }

    TEST(GCWeakRepair, StableAndAlreadyForwardedUnitsCompleteEarly)
    {
        TestHeap heap(1);
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        TestObject *stable = heap.make_non_moving_object();
        TestObject *target = heap.make_object();
        TestObject *source = heap.make_object();
        source->strong_cells = {TestCell::reference(target)};
        source->weak_entries = {
            TestWeakEntry::active(stable, stable, TestCell::immediate(17)),
            TestWeakEntry::active(target, stable, TestCell::reference(target))};
        Evacuation<TestHeapAdapter> collection(adapter);
        TestCell stable_root = TestCell::reference(stable);
        collection.add_root(TestSlot(stable_root));
        TestObject *copy = copy_root(collection, source);
        collection.drain();
        ASSERT_EQ(2u, copy->weak_entries.size());
        EXPECT_EQ(stable, copy->weak_entries[0].guards[0]);
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

    TEST(GCWeakRepair, WeakOnlyNonMovingCyclesAreClearedOnlyInFullMode)
    {
        for(auto kind:
            {TestCollectionKind::YoungOnly, TestCollectionKind::Full})
        {
            TestHeap heap;
            TestObject *first = heap.make_non_moving_object();
            TestObject *second = heap.make_non_moving_object();
            first->strong_cells = {TestCell::reference(second)};
            second->strong_cells = {TestCell::reference(first)};
            TestCell weak = TestCell::reference(first);
            TestHeapAdapter adapter(heap, kind);
            Evacuation<TestHeapAdapter> collection(adapter);
            collection.add_weak_root(Unit::bind<repair_test_weak_field>(weak));
            EXPECT_EQ(TestCell::reference(first), weak);
            EXPECT_FALSE(adapter.is_marked(first));
            collection.finish();
            EXPECT_EQ(TestCell::reference(
                          kind == TestCollectionKind::Full ? nullptr : first),
                      weak);
            EXPECT_FALSE(adapter.is_marked(first));
            EXPECT_FALSE(adapter.is_marked(second));
            EXPECT_EQ(0u, adapter.scan_count(first));
            EXPECT_EQ(0u, adapter.scan_count(second));
            EXPECT_EQ(0u, heap.destination_object_count(
                              EvacuationDestination::Ordinary));
        }
    }

    TEST(GCWeakRepair, NonMovingTargetCanBecomeLiveAfterEarlyWeakRepair)
    {
        TestHeap heap;
        TestObject *owner = heap.make_non_moving_object();
        TestObject *child = heap.make_object();
        owner->strong_cells = {TestCell::reference(child)};
        TestWeakEntry late =
            TestWeakEntry::active(owner, owner, TestCell::immediate(7));
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        Evacuation<TestHeapAdapter> collection(adapter);
        collection.add_weak_root(Unit::bind<repair_test_weak_entry>(late));
        EXPECT_EQ(1u, late.repair_count);
        EXPECT_FALSE(adapter.is_marked(owner));
        TestCell root = TestCell::reference(owner);
        collection.add_root(TestSlot(root));
        EXPECT_TRUE(adapter.is_marked(owner));
        TestWeakEntry early =
            TestWeakEntry::active(owner, owner, TestCell::immediate(9));
        collection.add_weak_root(Unit::bind<repair_test_weak_entry>(early));
        collection.finish();
        EXPECT_EQ(2u, late.repair_count);
        EXPECT_EQ(1u, early.repair_count);
        EXPECT_EQ(0u, late.reset_count);
        EXPECT_EQ(owner, late.guards[0]);
        EXPECT_EQ(1u, adapter.scan_count(owner));
        EXPECT_EQ(adapter.forwarded_to(child), pointer(owner->strong_cells[0]));
    }

    TEST(GCWeakRepair,
         RememberedOwnerVisitsBothKindsWithoutRetainingWeakTargets)
    {
        TestHeap heap;
        TestObject *owner = heap.make_object();
        owner->generation = TestGeneration::Old;
        TestObject *live = heap.make_object();
        TestObject *dead = heap.make_object();
        owner->strong_cells = {TestCell::reference(live)};
        owner->weak_cells = {TestCell::reference(live),
                             TestCell::reference(dead)};
        owner->weak_entries = {
            TestWeakEntry::active(owner, dead, TestCell::immediate(7))};
        TestHeapAdapter adapter(heap, TestCollectionKind::YoungOnly);
        Evacuation<TestHeapAdapter> collection(adapter);
        collection.add_remembered_owner(owner);
        collection.finish();
        EXPECT_FALSE(adapter.is_marked(owner));
        EXPECT_EQ(TestHeaderKind::Normal, owner->header.kind);
        EXPECT_NE(nullptr, adapter.forwarded_to(live));
        EXPECT_EQ(nullptr, adapter.forwarded_to(dead));
        EXPECT_EQ(TestCell::reference(adapter.forwarded_to(live)),
                  owner->weak_cells[0]);
        EXPECT_EQ(TestCell::reference(nullptr), owner->weak_cells[1]);
        EXPECT_EQ(1u, owner->weak_entries[0].reset_count);
        EXPECT_EQ(1u, adapter.weak_scan_count(owner));
    }

    TEST(GCWeakRepair, NonMovingOwnersRepairCoordinatedUnitsInPlace)
    {
        TestHeap heap;
        TestObject *owner = heap.make_non_moving_object();
        TestObject *live = heap.make_non_moving_object();
        TestObject *dead = heap.make_non_moving_object();
        TestObject *child = heap.make_object();
        owner->strong_cells = {TestCell::reference(live),
                               TestCell::reference(child)};
        owner->weak_entries = {
            TestWeakEntry::active(live, dead, TestCell::reference(child)),
            TestWeakEntry::active(live, live, TestCell::reference(child))};
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        Evacuation<TestHeapAdapter> collection(adapter);
        TestCell root = TestCell::reference(owner);
        collection.add_root(TestSlot(root));
        collection.drain();
        EXPECT_EQ(1u, owner->weak_entries[0].repair_count);
        EXPECT_EQ(dead, owner->weak_entries[0].guards[1]);
        collection.finish();
        EXPECT_EQ(owner, pointer(root));
        EXPECT_EQ(TestHeaderKind::Normal, owner->header.kind);
        EXPECT_TRUE(adapter.is_marked(live));
        EXPECT_FALSE(adapter.is_marked(dead));
        EXPECT_EQ(1u, adapter.weak_scan_count(owner));
        EXPECT_EQ(2u, owner->weak_entries[0].repair_count);
        EXPECT_EQ(1u, owner->weak_entries[0].reset_count);
        EXPECT_EQ(nullptr, owner->weak_entries[0].guards[0]);
        EXPECT_EQ(nullptr, owner->weak_entries[0].guards[1]);
        EXPECT_EQ(TestCell::immediate(0), owner->weak_entries[0].payload);
        EXPECT_EQ(1u, owner->weak_entries[1].repair_count);
        EXPECT_EQ(0u, owner->weak_entries[1].reset_count);
        EXPECT_EQ(live, owner->weak_entries[1].guards[0]);
        EXPECT_EQ(adapter.forwarded_to(child),
                  pointer(owner->weak_entries[1].payload));
    }

    TEST(GCWeakRepair, AdjacentEntriesDeferIndependentlyAndResetWholePayload)
    {
        TestHeap heap(1);
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        TestObject *stable = heap.make_non_moving_object();
        TestObject *source = heap.make_object();
        TestObject *dead_guard = heap.make_object();
        TestObject *dead_payload = heap.make_object();
        source->weak_entries = {
            TestWeakEntry::active(stable, stable, TestCell::immediate(19)),
            TestWeakEntry::active(dead_guard, stable, TestCell::immediate(23)),
            TestWeakEntry::active(stable, stable,
                                  TestCell::reference(dead_payload)),
            TestWeakEntry::active(stable, stable,
                                  TestCell::reference(nullptr))};
        Evacuation<TestHeapAdapter> collection(adapter);
        TestCell stable_root = TestCell::reference(stable);
        collection.add_root(TestSlot(stable_root));
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
        TestCell weak_root = TestCell::reference(target);
        TestCell weak_dead = TestCell::reference(dead);
        TestCell weak_null = TestCell::reference(nullptr);
        Evacuation<TestHeapAdapter> collection(adapter);
        collection.add_weak_root(Unit::bind<repair_test_weak_field>(weak_root));
        collection.add_weak_root(Unit::bind<repair_test_weak_field>(weak_dead));
        collection.add_weak_root(Unit::bind<repair_test_weak_field>(weak_null));
        EXPECT_EQ(TestCell::reference(target), weak_root);
        TestObject *copy = copy_root(collection, target);
        collection.finish();
        EXPECT_EQ(TestCell::reference(copy), weak_root);
        EXPECT_EQ(TestCell::reference(nullptr), weak_dead);
        EXPECT_EQ(TestCell::reference(nullptr), weak_null);
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
        owner->weak_cells = {TestCell::reference(child),
                             TestCell::reference(dead)};
        owner->weak_entries = {
            TestWeakEntry::active(child, backing, TestCell::reference(child))};
        child->preferred_destination = EvacuationDestination::Stable;
        TestCell weak_root = TestCell::reference(dead);
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
        EXPECT_EQ(TestCell::reference(nullptr), weak_root);
        EXPECT_EQ(backing_copy, pointer(copy->strong_cells[0]));
        EXPECT_EQ(child_copy, pointer(backing_copy->strong_cells[0]));
        EXPECT_EQ(copy, pointer(child_copy->strong_cells[0]));
        EXPECT_EQ(TestCell::reference(child_copy), copy->weak_cells[0]);
        EXPECT_EQ(TestCell::reference(nullptr), copy->weak_cells[1]);
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
        // Test-only native exceptions must terminate at the noexcept boundary,
        // not escape to a caller. The standard library's diagnostic varies.
        EXPECT_DEATH(collection.add_weak_root(Unit::bind<throw_early>(root)),
                     "");
        ThrowingUnit unit;
        collection.add_weak_root(Unit::bind<ThrowingUnit::repair>(unit));
        EXPECT_EQ(1u, unit.calls);
        EXPECT_DEATH(collection.finish(), "");
    }

#ifndef NDEBUG
    TEST(GCWeakRepair, FinishSealsCollectionAgainstNewStrongOrWeakRoots)
    {
        TestHeap heap;
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        Evacuation<TestHeapAdapter> collection(adapter);
        collection.finish();
        TestCell root = TestCell::reference(nullptr);
        TestCell weak = TestCell::reference(nullptr);
        EXPECT_DEATH(collection.add_root(TestSlot(root)), "phase_");
        EXPECT_DEATH(
            collection.add_weak_root(Unit::bind<repair_test_weak_field>(weak)),
            "phase_");
        EXPECT_DEATH(collection.drain(), "phase_");
        EXPECT_DEATH(collection.finish(), "phase_");
    }
#endif
}  // namespace cl::gc::test
