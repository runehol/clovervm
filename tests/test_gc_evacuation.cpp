#include "gc/evacuation.h"
#include "gc_test_heap.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace cl::gc::test
{
    namespace
    {
        static_assert(
            !std::is_copy_constructible_v<Evacuation<TestHeapAdapter>>);
        static_assert(
            !std::is_move_constructible_v<Evacuation<TestHeapAdapter>>);
        using Collection = Evacuation<TestHeapAdapter>;
        static_assert(
            std::is_nothrow_constructible_v<Collection, TestHeapAdapter &>);
        static_assert(noexcept(
            std::declval<Collection &>().add_root(std::declval<TestSlot>())));
        static_assert(noexcept(std::declval<Collection &>().drain()));
        static_assert(noexcept(std::declval<Collection &>().add_weak_root(
            std::declval<TestHeapAdapter::WeakUnit>())));
        static_assert(noexcept(std::declval<Collection &>().finish()));

        TestObject *pointer(const TestCell &cell)
        {
            auto value = std::get_if<TestObject *>(&cell.contents);
            return value == nullptr ? nullptr : *value;
        }

        void expect_evacuation(TestHeap &heap, std::span<TestCell> roots,
                               TestCollectionKind kind)
        {
            auto selected = [&](TestObject *object) {
                return object->storage == TestStorage::Source &&
                       (kind == TestCollectionKind::Full ||
                        object->generation == TestGeneration::Young);
            };
            std::vector<TestCell> original_roots(roots.begin(), roots.end());
            // Independent fixed-point oracle: inspect only the fixture's source
            // graph, never adapter enumeration, forwarding, or collector state.
            std::unordered_set<TestObject *> live;
            auto include = [&](const TestCell &cell) {
                TestObject *object = pointer(cell);
                return object != nullptr && selected(object) &&
                       live.insert(object).second;
            };
            for(const TestCell &root: roots)
            {
                include(root);
            }
            bool changed;
            do
            {
                changed = false;
                for(const auto &object: heap.objects())
                {
                    if(!live.contains(object.get()))
                    {
                        continue;
                    }
                    for(const TestCell &cell: object->strong_cells)
                    {
                        changed = include(cell) || changed;
                    }
                }
            }
            while(changed);

            // Snapshot all source payloads so accidental source-side repair is
            // detected, not hidden by comparing against a mutated source graph.
            std::vector<TestObject> originals;
            for(const auto &object: heap.objects())
            {
                originals.push_back(*object);
            }
            TestHeapAdapter adapter(heap, kind);
            Evacuation<TestHeapAdapter> collection(adapter);
            for(TestCell &root: roots)
            {
                collection.add_root(TestSlot(root));
            }
            collection.drain();

            std::unordered_set<TestObject *> destinations;
            for(const auto &object: heap.objects())
            {
                TestObject *copy = selected(object.get())
                                       ? adapter.forwarded_to(object.get())
                                       : nullptr;
                if(live.contains(object.get()))
                {
                    ASSERT_NE(nullptr, copy);
                    EXPECT_TRUE(destinations.insert(copy).second);
                    EXPECT_FALSE(adapter.is_collected_source(copy));
                    EXPECT_EQ(object->logical_identity, copy->logical_identity);
                    EXPECT_EQ(TestHeaderKind::Normal, copy->header.kind);
                    EXPECT_EQ(object->preferred_destination ==
                                      EvacuationDestination::Stable
                                  ? TestStorage::StableDestination
                                  : TestStorage::OrdinaryDestination,
                              copy->storage);
                    EXPECT_EQ(TestGeneration::Old, copy->generation);
                    EXPECT_EQ(1u, adapter.scan_count(copy));
                }
                else
                {
                    EXPECT_EQ(nullptr, copy);
                }
                EXPECT_EQ(0u, adapter.scan_count(object.get()));
            }
            EXPECT_EQ(live.size(), heap.destination_object_count(
                                       EvacuationDestination::Ordinary) +
                                       heap.destination_object_count(
                                           EvacuationDestination::Stable));

            auto expect_cell = [&](const TestCell &before,
                                   const TestCell &after) {
                TestObject *source = pointer(before);
                if(source == nullptr || !selected(source))
                {
                    EXPECT_EQ(before, after);
                    return;
                }
                ASSERT_TRUE(live.contains(source));
                EXPECT_EQ(adapter.forwarded_to(source), pointer(after));
                ASSERT_NE(nullptr, pointer(after));
                EXPECT_NE(TestStorage::Source, pointer(after)->storage);
            };
            for(size_t idx = 0; idx < roots.size(); ++idx)
            {
                expect_cell(original_roots[idx], roots[idx]);
            }
            size_t idx = 0;
            for(const auto &object: heap.objects())
            {
                const TestObject &original = originals[idx++];
                EXPECT_EQ(original.strong_cells, object->strong_cells);
                EXPECT_EQ(original.weak_targets, object->weak_targets);
                EXPECT_EQ(original.generation, object->generation);
                if(!live.contains(object.get()))
                {
                    continue;
                }
                TestObject *copy = adapter.forwarded_to(object.get());
                ASSERT_EQ(original.strong_cells.size(),
                          copy->strong_cells.size());
                for(size_t cell = 0; cell < original.strong_cells.size();
                    ++cell)
                {
                    expect_cell(original.strong_cells[cell],
                                copy->strong_cells[cell]);
                }
                // drain() may repair already-resolved weak targets early, but
                // leaves unresolved targets for finish(), even if strong work
                // subsequently discovers them during this drain.
                ASSERT_EQ(original.weak_targets.size(),
                          copy->weak_targets.size());
                for(size_t weak = 0; weak < original.weak_targets.size();
                    ++weak)
                {
                    TestObject *before = original.weak_targets[weak];
                    TestObject *after = copy->weak_targets[weak];
                    TestObject *forwarded =
                        before != nullptr && selected(before)
                            ? adapter.forwarded_to(before)
                            : nullptr;
                    EXPECT_TRUE(after == before ||
                                (forwarded != nullptr && forwarded == after));
                }
            }
            // Repeated references to both source and destination roots must not
            // recopy or rescan objects, even after the initial drain.
            for(TestCell &root: roots)
            {
                collection.add_root(TestSlot(root));
            }
            for(TestCell &root: original_roots)
            {
                collection.add_root(TestSlot(root));
            }
            collection.drain();
            for(TestObject *copy: destinations)
            {
                EXPECT_EQ(1u, adapter.scan_count(copy));
            }
            EXPECT_EQ(live.size(), heap.destination_object_count(
                                       EvacuationDestination::Ordinary) +
                                       heap.destination_object_count(
                                           EvacuationDestination::Stable));
            collection.finish();
            for(const auto &object: heap.objects())
            {
                if(!live.contains(object.get()))
                {
                    continue;
                }
                TestObject *copy = adapter.forwarded_to(object.get());
                EXPECT_EQ(1u, adapter.weak_scan_count(copy));
                ASSERT_EQ(object->weak_targets.size(),
                          copy->weak_targets.size());
                for(size_t weak = 0; weak < object->weak_targets.size(); ++weak)
                {
                    TestObject *target = object->weak_targets[weak];
                    EXPECT_EQ(target == nullptr || !selected(target)
                                  ? target
                                  : adapter.forwarded_to(target),
                              copy->weak_targets[weak]);
                }
            }
        }

        class NullCopyAdapter : public TestHeapAdapter
        {
        public:
            using TestHeapAdapter::TestHeapAdapter;
            Object *copy_object(Object *) { return nullptr; }
        };
    }  // namespace

    TEST(GCEvacuation,
         StrongEdgesRespectSelectionRootOrderAndIndependentPlacement)
    {
        enum class RootOrder
        {
            OwnerFirst,
            BackingFirst
        };
        for(auto kind:
            {TestCollectionKind::YoungOnly, TestCollectionKind::Full})
            for(auto owner_generation:
                {TestGeneration::Young, TestGeneration::Old})
                for(auto backing_generation:
                    {TestGeneration::Young, TestGeneration::Old})
                    for(auto order:
                        {RootOrder::OwnerFirst, RootOrder::BackingFirst})
                        for(auto owner_destination:
                            {EvacuationDestination::Ordinary,
                             EvacuationDestination::Stable})
                            for(auto backing_destination:
                                {EvacuationDestination::Ordinary,
                                 EvacuationDestination::Stable})
                            {
                                SCOPED_TRACE(static_cast<int>(kind));
                                SCOPED_TRACE(
                                    static_cast<int>(owner_generation));
                                SCOPED_TRACE(
                                    static_cast<int>(backing_generation));
                                SCOPED_TRACE(static_cast<int>(order));
                                SCOPED_TRACE(
                                    static_cast<int>(owner_destination));
                                SCOPED_TRACE(
                                    static_cast<int>(backing_destination));
                                TestHeap heap(1);
                                TestObject *owner = heap.make_object();
                                TestObject *backing = heap.make_object();
                                owner->strong_cells = {
                                    TestCell::reference(backing)};
                                backing->strong_cells = {
                                    TestCell::immediate(73)};
                                owner->generation = owner_generation;
                                backing->generation = backing_generation;
                                owner->preferred_destination =
                                    owner_destination;
                                backing->preferred_destination =
                                    backing_destination;
                                bool owner_selected =
                                    kind == TestCollectionKind::Full ||
                                    owner_generation == TestGeneration::Young;
                                bool backing_selected =
                                    kind == TestCollectionKind::Full ||
                                    backing_generation == TestGeneration::Young;
                                TestHeapAdapter adapter(heap, kind);
                                Evacuation<TestHeapAdapter> collection(adapter);
                                TestCell owner_root =
                                    TestCell::reference(owner);
                                TestCell backing_root =
                                    TestCell::reference(backing);
                                auto add_owner = [&] {
                                    collection.add_root(TestSlot(owner_root));
                                };
                                auto add_backing = [&] {
                                    if(!owner_selected)
                                    {
                                        // Supply the old owner's actual
                                        // remembered slot. Rooting its target
                                        // alone cannot repair this cell.
                                        collection.add_root(
                                            TestSlot(owner->strong_cells[0]));
                                    }
                                    collection.add_root(TestSlot(backing_root));
                                };
                                switch(order)
                                {
                                    case RootOrder::OwnerFirst:
                                        add_owner();
                                        add_backing();
                                        break;
                                    case RootOrder::BackingFirst:
                                        add_backing();
                                        add_owner();
                                        break;
                                }
                                collection.finish();
                                TestObject *owner_result = pointer(owner_root);
                                TestObject *backing_result =
                                    pointer(backing_root);
                                EXPECT_EQ(
                                    backing_result,
                                    pointer(owner_result->strong_cells[0]));
                                EXPECT_EQ(TestCell::immediate(73),
                                          backing_result->strong_cells[0]);
                                EXPECT_EQ(owner_generation, owner->generation);
                                EXPECT_EQ(backing_generation,
                                          backing->generation);
                                for(TestObject *source: {owner, backing})
                                {
                                    bool selected = source == owner
                                                        ? owner_selected
                                                        : backing_selected;
                                    TestObject *result = source == owner
                                                             ? owner_result
                                                             : backing_result;
                                    if(selected)
                                    {
                                        EXPECT_NE(source, result);
                                        EXPECT_TRUE(adapter.is_collected_source(
                                            source));
                                        EXPECT_EQ(result,
                                                  adapter.forwarded_to(source));
                                        EXPECT_EQ(TestGeneration::Old,
                                                  result->generation);
                                        EXPECT_FALSE(
                                            adapter.is_collected_source(
                                                result));
                                        EXPECT_EQ(
                                            source->preferred_destination ==
                                                    EvacuationDestination::
                                                        Stable
                                                ? TestStorage::StableDestination
                                                : TestStorage::
                                                      OrdinaryDestination,
                                            result->storage);
                                        EXPECT_EQ(1u,
                                                  adapter.scan_count(result));
                                        EXPECT_EQ(1u, adapter.weak_scan_count(
                                                          result));
                                    }
                                    else
                                    {
                                        EXPECT_EQ(source, result);
                                        EXPECT_FALSE(
                                            adapter.is_collected_source(
                                                source));
                                        EXPECT_EQ(TestHeaderKind::Normal,
                                                  source->header.kind);
                                        EXPECT_EQ(0u,
                                                  adapter.scan_count(source));
                                    }
                                }
                                for(auto destination:
                                    {EvacuationDestination::Ordinary,
                                     EvacuationDestination::Stable})
                                {
                                    EXPECT_EQ(size_t(owner_selected &&
                                                     owner_destination ==
                                                         destination) +
                                                  size_t(backing_selected &&
                                                         backing_destination ==
                                                             destination),
                                              heap.destination_object_count(
                                                  destination));
                                }
                            }
    }

    TEST(GCEvacuation, ChildIsDiscoveredOnlyWhenOwnerIsScanned)
    {
        TestHeap heap(1);
        TestObject *owner = heap.make_object();
        TestObject *backing = heap.make_object();
        owner->strong_cells = {TestCell::reference(backing)};
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        Evacuation<TestHeapAdapter> collection(adapter);
        TestCell root = TestCell::reference(owner);
        collection.add_root(TestSlot(root));
        ASSERT_NE(nullptr, adapter.forwarded_to(owner));
        EXPECT_EQ(nullptr, adapter.forwarded_to(backing));
        EXPECT_EQ(
            1u, heap.destination_object_count(EvacuationDestination::Ordinary));
        collection.finish();
        ASSERT_NE(nullptr, adapter.forwarded_to(backing));
        EXPECT_EQ(adapter.forwarded_to(backing),
                  pointer(pointer(root)->strong_cells[0]));
        EXPECT_EQ(1u, adapter.scan_count(adapter.forwarded_to(backing)));
    }

    TEST(GCEvacuation, RootedBackingDoesNotRetainItsUnreachableOwner)
    {
        for(auto kind:
            {TestCollectionKind::YoungOnly, TestCollectionKind::Full})
        {
            TestHeap heap;
            TestObject *owner = heap.make_object();
            TestObject *backing = heap.make_object();
            owner->strong_cells = {TestCell::reference(backing)};
            TestHeapAdapter adapter(heap, kind);
            Evacuation<TestHeapAdapter> collection(adapter);
            TestCell root = TestCell::reference(backing);
            collection.add_root(TestSlot(root));
            collection.finish();
            EXPECT_EQ(nullptr, adapter.forwarded_to(owner));
            EXPECT_EQ(pointer(root), adapter.forwarded_to(backing));
            EXPECT_EQ(1u, adapter.scan_count(pointer(root)));
            EXPECT_EQ(1u, heap.destination_object_count(
                              EvacuationDestination::Ordinary));
        }
    }

    TEST(GCEvacuation, StableOwnerIsNotCopiedEvenInFullSelection)
    {
        for(auto kind:
            {TestCollectionKind::YoungOnly, TestCollectionKind::Full})
        {
            TestHeap heap;
            TestObject *owner = heap.make_object();
            TestObject *backing = heap.make_object();
            owner->strong_cells = {TestCell::reference(backing)};
            owner->generation = TestGeneration::Old;
            owner->storage = TestStorage::StableDestination;
            TestHeapAdapter adapter(heap, kind);
            Evacuation<TestHeapAdapter> collection(adapter);
            TestCell root = TestCell::reference(owner);
            collection.add_root(TestSlot(root));
            collection.add_root(TestSlot(owner->strong_cells[0]));
            collection.finish();
            EXPECT_EQ(owner, pointer(root));
            EXPECT_FALSE(adapter.is_collected_source(owner));
            EXPECT_EQ(0u, adapter.scan_count(owner));
            EXPECT_EQ(adapter.forwarded_to(backing),
                      pointer(owner->strong_cells[0]));
            EXPECT_EQ(1u, heap.destination_object_count(
                              EvacuationDestination::Ordinary));
        }
    }

    TEST(GCEvacuation, OldWeakTargetsFollowCollectionSelection)
    {
        for(auto kind:
            {TestCollectionKind::YoungOnly, TestCollectionKind::Full})
        {
            TestHeap heap;
            TestObject *target = heap.make_object();
            target->generation = TestGeneration::Old;
            TestObject *weak = target;
            TestHeapAdapter adapter(heap, kind);
            Evacuation<TestHeapAdapter> collection(adapter);
            collection.add_weak_root(
                TestHeapAdapter::WeakUnit::bind<repair_test_weak_field>(weak));
            collection.finish();
            EXPECT_EQ(kind == TestCollectionKind::Full ? nullptr : target,
                      weak);
            EXPECT_EQ(TestHeaderKind::Normal, target->header.kind);
            EXPECT_EQ(0u, heap.destination_object_count(
                              EvacuationDestination::Ordinary));
        }
    }

    TEST(GCEvacuation, SelectedSourceRetirementPreservesOldAndStableRecords)
    {
        for(auto kind:
            {TestCollectionKind::YoungOnly, TestCollectionKind::Full})
        {
            TestHeap heap;
            TestObject *young = heap.make_object();
            TestObject *old = heap.make_object();
            old->generation = TestGeneration::Old;
            TestObject *stable = heap.make_object();
            stable->generation = TestGeneration::Old;
            stable->storage = TestStorage::StableDestination;
            TestHeapAdapter adapter(heap, kind);
            Evacuation<TestHeapAdapter> collection(adapter);
            TestCell root = TestCell::reference(young);
            collection.add_root(TestSlot(root));
            collection.finish();
            heap.discard_source_storage(kind);
            if(kind == TestCollectionKind::YoungOnly)
            {
                ASSERT_EQ(2u, heap.objects().size());
                EXPECT_EQ(old, heap.objects()[0].get());
                EXPECT_EQ(stable, heap.objects()[1].get());
            }
            else
            {
                ASSERT_EQ(1u, heap.objects().size());
                EXPECT_EQ(stable, heap.objects()[0].get());
            }
            EXPECT_EQ(TestGeneration::Old, pointer(root)->generation);
        }
    }

    TEST(GCEvacuation, MixedGenerationGraphsMatchOracleInBothModes)
    {
        for(auto kind:
            {TestCollectionKind::YoungOnly, TestCollectionKind::Full})
        {
            TestHeap heap(3);
            TestObject *head = heap.make_object();
            TestObject *old = heap.make_object();
            TestObject *young = heap.make_object();
            TestObject *dead_old = heap.make_object();
            old->generation = TestGeneration::Old;
            dead_old->generation = TestGeneration::Old;
            head->strong_cells = {TestCell::reference(old),
                                  TestCell::reference(young)};
            young->strong_cells = {TestCell::reference(head)};
            head->weak_targets = {old, dead_old};
            // The old node has no outgoing young edges, so this fixture needs
            // no remembered slots beyond its roots in YoungOnly mode.
            std::array roots = {TestCell::reference(head)};
            expect_evacuation(heap, roots, kind);
        }
    }

    TEST(GCEvacuation, EmptyNullAndImmediateRoots)
    {
        TestHeap heap;
        expect_evacuation(heap, {}, TestCollectionKind::Full);
        heap.make_object();
        std::array roots = {TestCell::reference(nullptr),
                            TestCell::immediate(42)};
        expect_evacuation(heap, roots, TestCollectionKind::Full);
    }

    TEST(GCEvacuation, CyclesSharingDuplicateRootsAndWeakOnlyGarbage)
    {
        TestHeap heap(1);
        TestObject *a = heap.make_object();
        TestObject *b = heap.make_object();
        TestObject *c = heap.make_object();
        TestObject *weak = heap.make_object();
        a->strong_cells = {TestCell::reference(a), TestCell::reference(b),
                           TestCell::reference(c)};
        b->strong_cells = {TestCell::reference(c), TestCell::reference(a)};
        c->strong_cells = {TestCell::immediate(-7),
                           TestCell::reference(nullptr)};
        a->weak_targets = {weak, c};
        std::array roots = {TestCell::reference(a), TestCell::reference(a),
                            TestCell::reference(c)};
        expect_evacuation(heap, roots, TestCollectionKind::Full);
    }

    TEST(GCEvacuation, OrdinaryAndStablePathsJoinSameClosure)
    {
        TestHeap heap(1);
        TestObject *first = heap.make_object();
        TestObject *first_backing = heap.make_object();
        TestObject *second = heap.make_object();
        TestObject *second_backing = heap.make_object();
        TestObject *child = heap.make_object();
        first->strong_cells = {TestCell::reference(first_backing)};
        first_backing->preferred_destination = EvacuationDestination::Stable;
        first_backing->strong_cells = {TestCell::reference(second)};
        second->strong_cells = {TestCell::reference(second_backing)};
        second_backing->strong_cells = {TestCell::reference(first),
                                        TestCell::reference(child)};
        child->preferred_destination = EvacuationDestination::Stable;
        child->strong_cells = {TestCell::reference(child)};
        std::array roots = {TestCell::reference(first)};
        expect_evacuation(heap, roots, TestCollectionKind::Full);
        EXPECT_EQ(2u,
                  heap.destination_object_count(EvacuationDestination::Stable));
        EXPECT_EQ(
            3u, heap.destination_object_count(EvacuationDestination::Ordinary));
    }

    TEST(GCEvacuation,
         OutsideSourceRootsAreUntouchedAndRememberedSlotsAreExplicit)
    {
        TestHeap heap(1);
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        TestObject *child = heap.make_object();
        TestObject *source = heap.make_object();
        source->preferred_destination = EvacuationDestination::Stable;
        TestObject *old = adapter.copy_object(source);
        old->strong_cells = {TestCell::reference(child)};
        TestCell root = TestCell::reference(old);
        Evacuation<TestHeapAdapter> collection(adapter);
        collection.add_root(TestSlot(root));
        collection.drain();
        EXPECT_EQ(old, pointer(root));
        EXPECT_EQ(0u, adapter.scan_count(old));
        EXPECT_EQ(nullptr, adapter.forwarded_to(child));
        collection.add_root(TestSlot(old->strong_cells[0]));
        collection.drain();
        EXPECT_EQ(adapter.forwarded_to(child), pointer(old->strong_cells[0]));
        ASSERT_NE(nullptr, adapter.forwarded_to(child));
        EXPECT_EQ(1u, adapter.scan_count(adapter.forwarded_to(child)));
    }

    TEST(GCEvacuation, WideFrontierAndChunkGrowthDuringScanning)
    {
        TestHeap heap(7);
        TestObject *root = heap.make_object();
        for(size_t idx = 0; idx < 4096; ++idx)
        {
            TestObject *child = heap.make_object();
            child->preferred_destination =
                idx % 2 == 0 ? EvacuationDestination::Stable
                             : EvacuationDestination::Ordinary;
            child->strong_cells = {TestCell::reference(root)};
            root->strong_cells.push_back(TestCell::reference(child));
        }
        std::array roots = {TestCell::reference(root)};
        expect_evacuation(heap, roots, TestCollectionKind::Full);
    }

    TEST(GCEvacuation, DeepChainUsesWorklistRatherThanRecursiveTraversal)
    {
        TestHeap heap(127);
        TestObject *head = heap.make_object();
        TestObject *tail = head;
        for(size_t idx = 0; idx < 30000; ++idx)
        {
            TestObject *next = heap.make_object();
            tail->strong_cells = {TestCell::reference(next)};
            tail = next;
        }
        std::array roots = {TestCell::reference(head)};
        expect_evacuation(heap, roots, TestCollectionKind::Full);
    }

    TEST(GCEvacuation, SeededGraphsMatchIndependentOracle)
    {
        std::mt19937 random(0x5eed);
        for(size_t trial = 0; trial < 24; ++trial)
        {
            SCOPED_TRACE(trial);
            TestHeap heap(3);
            std::vector<TestObject *> objects;
            for(size_t idx = 0; idx < 96; ++idx)
            {
                objects.push_back(heap.make_object());
            }
            for(TestObject *object: objects)
            {
                object->preferred_destination =
                    random() % 3 == 0 ? EvacuationDestination::Stable
                                      : EvacuationDestination::Ordinary;
                for(size_t edge = 0; edge < 3; ++edge)
                {
                    object->strong_cells.push_back(TestCell::reference(
                        objects[random() % objects.size()]));
                }
                object->strong_cells.push_back(TestCell::immediate(random()));
                object->weak_targets = {objects[random() % objects.size()]};
            }
            std::array roots = {
                TestCell::reference(objects[random() % objects.size()])};
            expect_evacuation(heap, roots, TestCollectionKind::Full);
        }
    }

    TEST(GCEvacuation, NullCopyIsFatalBeforePublishingForwarding)
    {
        TestHeap heap;
        TestCell root = TestCell::reference(heap.make_object());
        NullCopyAdapter failing_copy(heap, TestCollectionKind::Full);
        Evacuation<NullCopyAdapter> copying(failing_copy);
        EXPECT_DEATH(copying.add_root(TestSlot(root)),
                     "test GC allocation failed");
    }

    TEST(GCEvacuation, DestinationExhaustionDuringScanningIsFatal)
    {
        for(auto destination:
            {EvacuationDestination::Ordinary, EvacuationDestination::Stable})
        {
            TestHeap heap(1);
            TestHeapAdapter adapter(heap, TestCollectionKind::Full);
            TestObject *source = heap.make_object();
            TestObject *child = heap.make_object();
            child->preferred_destination = destination;
            source->strong_cells = {TestCell::reference(child)};
            TestCell root = TestCell::reference(source);
            Evacuation<TestHeapAdapter> collection(adapter);
            collection.add_root(TestSlot(root));
            ASSERT_NE(nullptr, adapter.forwarded_to(source));
            heap.limit_destination_chunks(
                destination, heap.destination_chunk_count(destination));
            EXPECT_DEATH(collection.drain(), "test GC allocation failed");
        }
    }
}  // namespace cl::gc::test
