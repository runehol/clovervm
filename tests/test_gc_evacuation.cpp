#include "gc/evacuation.h"
#include "gc_test_heap.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <random>
#include <span>
#include <type_traits>
#include <unordered_set>
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
                    changed = include(object->companion) || changed;
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
                EXPECT_EQ(original.companion, object->companion);
                EXPECT_EQ(original.weak_targets, object->weak_targets);
                EXPECT_EQ(original.generation, object->generation);
                if(!live.contains(object.get()))
                {
                    continue;
                }
                TestObject *copy = adapter.forwarded_to(object.get());
                expect_cell(original.companion, copy->companion);
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

        class FailingCopyAdapter : public TestHeapAdapter
        {
        public:
            using TestHeapAdapter::TestHeapAdapter;
            std::array<Object *, 2> copy_group(const Group &,
                                               EvacuationDestination)
            {
                throw std::bad_alloc();
            }
        };

        class FailingScanAdapter : public TestHeapAdapter
        {
        public:
            using TestHeapAdapter::TestHeapAdapter;
            template <typename Visitor>
            void visit_strong_slots(Object *, Visitor &&)
            {
                throw std::bad_alloc();
            }
        };
    }  // namespace

    TEST(GCEvacuation, ConditionalGroupsRespectSelectionAndRootOrder)
    {
        enum class RootOrder
        {
            OwnerFirst,
            CompanionFirst,
        };
        for(auto kind:
            {TestCollectionKind::YoungOnly, TestCollectionKind::Full})
            for(auto owner_generation:
                {TestGeneration::Young, TestGeneration::Old})
                for(auto companion_generation:
                    {TestGeneration::Young, TestGeneration::Old})
                    for(auto order:
                        {RootOrder::OwnerFirst, RootOrder::CompanionFirst})
                        for(auto destination: {EvacuationDestination::Ordinary,
                                               EvacuationDestination::Stable})
                        {
                            SCOPED_TRACE(static_cast<int>(kind));
                            SCOPED_TRACE(static_cast<int>(owner_generation));
                            SCOPED_TRACE(
                                static_cast<int>(companion_generation));
                            SCOPED_TRACE(static_cast<int>(order));
                            SCOPED_TRACE(static_cast<int>(destination));
                            TestHeap heap(1);
                            auto group = heap.make_pair();
                            TestObject *owner = group.members[0];
                            TestObject *companion = group.members[1];
                            owner->generation = owner_generation;
                            companion->generation = companion_generation;
                            owner->preferred_destination = destination;
                            companion->preferred_destination = destination;
                            companion->strong_cells = {TestCell::immediate(73)};
                            bool owner_selected =
                                kind == TestCollectionKind::Full ||
                                owner_generation == TestGeneration::Young;
                            bool companion_selected =
                                kind == TestCollectionKind::Full ||
                                companion_generation == TestGeneration::Young;
                            TestHeapAdapter adapter(heap, kind);
                            Evacuation<TestHeapAdapter> collection(adapter);
                            TestCell owner_root = TestCell::reference(owner);
                            TestCell companion_root =
                                TestCell::reference(companion);
                            auto add_owner = [&] {
                                collection.add_root(TestSlot(owner_root));
                            };
                            auto add_companion = [&] {
                                if(!owner_selected)
                                {
                                    // The old owner's actual cell is a supplied
                                    // remembered slot. Merely rooting the
                                    // companion would not patch it.
                                    collection.add_root(
                                        TestSlot(owner->companion));
                                }
                                collection.add_root(TestSlot(companion_root));
                            };
                            switch(order)
                            {
                                case RootOrder::OwnerFirst:
                                    add_owner();
                                    add_companion();
                                    break;
                                case RootOrder::CompanionFirst:
                                    add_companion();
                                    add_owner();
                                    break;
                            }
                            collection.finish();
                            TestObject *owner_result = pointer(owner_root);
                            TestObject *companion_result =
                                pointer(companion_root);
                            EXPECT_EQ(companion_result,
                                      pointer(owner_result->companion));
                            EXPECT_EQ(TestCell::immediate(73),
                                      companion_result->strong_cells[0]);
                            EXPECT_EQ(owner_generation, owner->generation);
                            EXPECT_EQ(companion_generation,
                                      companion->generation);
                            for(size_t idx = 0; idx < group.count; ++idx)
                            {
                                TestObject *source = group.members[idx];
                                bool selected = idx == 0 ? owner_selected
                                                         : companion_selected;
                                TestObject *result =
                                    idx == 0 ? owner_result : companion_result;
                                if(selected)
                                {
                                    EXPECT_NE(source, result);
                                    EXPECT_TRUE(
                                        adapter.is_collected_source(source));
                                    EXPECT_EQ(result,
                                              adapter.forwarded_to(source));
                                    EXPECT_EQ(TestGeneration::Old,
                                              result->generation);
                                    EXPECT_FALSE(
                                        adapter.is_collected_source(result));
                                    EXPECT_EQ(1u, adapter.scan_count(result));
                                    EXPECT_EQ(1u,
                                              adapter.weak_scan_count(result));
                                }
                                else
                                {
                                    EXPECT_EQ(source, result);
                                    EXPECT_FALSE(
                                        adapter.is_collected_source(source));
                                    EXPECT_EQ(TestHeaderKind::Normal,
                                              source->header.kind);
                                    EXPECT_EQ(0u, adapter.scan_count(source));
                                }
                            }
                            EXPECT_EQ(
                                size_t(owner_selected) +
                                    size_t(companion_selected),
                                heap.destination_object_count(destination));
                            if(owner_selected && companion_selected &&
                               order == RootOrder::OwnerFirst)
                            {
                                EXPECT_EQ(sizeof(TestObject),
                                          reinterpret_cast<uintptr_t>(
                                              companion_result) -
                                              reinterpret_cast<uintptr_t>(
                                                  owner_result));
                            }
                        }
    }

    TEST(GCEvacuation, StandaloneCompanionDoesNotRetainItsOwner)
    {
        for(auto kind:
            {TestCollectionKind::YoungOnly, TestCollectionKind::Full})
        {
            TestHeap heap;
            auto group = heap.make_pair();
            TestHeapAdapter adapter(heap, kind);
            Evacuation<TestHeapAdapter> collection(adapter);
            TestCell root = TestCell::reference(group.members[1]);
            collection.add_root(TestSlot(root));
            collection.finish();
            EXPECT_EQ(nullptr, adapter.forwarded_to(group.members[0]));
            EXPECT_EQ(pointer(root), adapter.forwarded_to(group.members[1]));
            EXPECT_EQ(nullptr, pointer(root)->exclusive_owner);
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
            auto group = heap.make_pair();
            TestObject *owner = group.members[0];
            owner->generation = TestGeneration::Old;
            owner->storage = TestStorage::StableDestination;
            TestHeapAdapter adapter(heap, kind);
            Evacuation<TestHeapAdapter> collection(adapter);
            TestCell root = TestCell::reference(owner);
            collection.add_root(TestSlot(root));
            collection.add_root(TestSlot(owner->companion));
            collection.finish();
            EXPECT_EQ(owner, pointer(root));
            EXPECT_FALSE(adapter.is_collected_source(owner));
            EXPECT_EQ(0u, adapter.scan_count(owner));
            EXPECT_EQ(adapter.forwarded_to(group.members[1]),
                      pointer(owner->companion));
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

    TEST(GCEvacuation, CompanionReferencesAndStablePathsJoinSameClosure)
    {
        TestHeap heap(1);
        auto first = heap.make_pair();
        auto second = heap.make_pair();
        TestObject *child = heap.make_object();
        first.members[1]->preferred_destination = EvacuationDestination::Stable;
        first.members[1]->strong_cells = {
            TestCell::reference(second.members[0])};
        second.members[1]->strong_cells = {
            TestCell::reference(first.members[0]), TestCell::reference(child)};
        child->preferred_destination = EvacuationDestination::Stable;
        child->strong_cells = {TestCell::reference(child)};
        std::array roots = {TestCell::reference(first.members[0])};
        expect_evacuation(heap, roots, TestCollectionKind::Full);
        EXPECT_EQ(3u,
                  heap.destination_object_count(EvacuationDestination::Stable));
        EXPECT_EQ(
            2u, heap.destination_object_count(EvacuationDestination::Ordinary));
    }

    TEST(GCEvacuation,
         OutsideSourceRootsAreUntouchedAndRememberedSlotsAreExplicit)
    {
        TestHeap heap(1);
        TestHeapAdapter adapter(heap, TestCollectionKind::Full);
        TestObject *child = heap.make_object();
        TestObject *old =
            adapter.copy_group(adapter.allocation_group(heap.make_object()),
                               EvacuationDestination::Stable)[0];
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

    TEST(GCEvacuation, NativeAllocationExceptionsAreFatalAtPublicEntryPoints)
    {
        TestHeap heap;
        TestCell root = TestCell::reference(heap.make_object());
        FailingCopyAdapter failing_copy(heap, TestCollectionKind::Full);
        Evacuation<FailingCopyAdapter> copying(failing_copy);
        EXPECT_DEATH(copying.add_root(TestSlot(root)),
                     "test GC allocation failed");
        FailingScanAdapter failing_scan(heap, TestCollectionKind::Full);
        Evacuation<FailingScanAdapter> scanning(failing_scan);
        scanning.add_root(TestSlot(root));
        EXPECT_DEATH(scanning.drain(), "test GC allocation failed");
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
