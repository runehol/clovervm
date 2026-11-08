#include "heap_runner.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <type_traits>

namespace
{
    namespace f = cl::gc::test::format;
    namespace d = cl::gc::test::differential;
    namespace native = cl::gc::test;
    using d::detail::NativeHeap;

    static_assert(!std::is_copy_constructible_v<NativeHeap>);
    static_assert(!std::is_move_constructible_v<NativeHeap>);

    f::HeapSnapshot fixture(const std::string &name)
    {
        std::ifstream file("tests/gc_differential/fixtures/" + name + ".json");
        if(!file)
            throw std::runtime_error("cannot open heap fixture");
        return f::parse_heap(std::string(std::istreambuf_iterator<char>(file),
                                         std::istreambuf_iterator<char>()));
    }
    f::Cell ref(uint64_t id) { return f::Reference{f::AllocationId{id}}; }
    f::Cell null_ref() { return f::Reference{}; }
    f::WeakUnit field(uint64_t id, f::Cell value)
    {
        return {f::UnitId{id}, f::Field{value}};
    }
    f::WeakUnit entry(uint64_t id, f::Reference first, f::Reference second,
                      f::Cell payload)
    {
        return {f::UnitId{id}, f::Entry{{first, second}, payload}};
    }
    f::WeakUnit reset_entry(uint64_t id)
    {
        return entry(id, std::nullopt, std::nullopt, int64_t{0});
    }

    TEST(GCHeapRunner, ConstructionRoundTripsUnsortedAllocationsAndCells)
    {
        auto input = fixture("before");
        input.roots.push_back(int64_t{std::numeric_limits<int64_t>::min()});
        input.roots.push_back(int64_t{std::numeric_limits<int64_t>::max()});
        input.roots.push_back(null_ref());
        input.objects[0].object_id = f::ObjectId{"obj-2"};
        input.objects[2].object_id = f::ObjectId{"obj-0"};
        auto expected = input;
        std::reverse(input.objects.begin(), input.objects.end());
        NativeHeap heap(input);
        EXPECT_EQ(heap.capture(), expected);
        EXPECT_EQ(heap.object({0}).logical_identity, 2);
        EXPECT_EQ(heap.object({2}).logical_identity, 0);
    }

    TEST(GCHeapRunner, DocumentedMarkResultRetainsAllStorageAndWeakValues)
    {
        auto input = fixture("before");
        auto result = d::run_collection(input, d::Algorithm::Mark);
        EXPECT_EQ(result, fixture("after_mark"));
        EXPECT_EQ(result.roots, input.roots);
        EXPECT_EQ(result.external_weak, input.external_weak);
        EXPECT_EQ(result.objects.size(), input.objects.size());
    }

    TEST(GCHeapRunner, DocumentedEvacuationResultRetainsOriginalStorage)
    {
        auto input = fixture("before");
        auto expected = fixture("after");
        EXPECT_EQ(d::run_collection(input, d::Algorithm::Evacuate), expected);
        NativeHeap heap(input);
        heap.collect(d::Algorithm::Evacuate);
        EXPECT_EQ(heap.capture(), expected);
        EXPECT_EQ(heap.object({1}).strong_cells[1],
                  native::TestCell::immediate(7));
        EXPECT_EQ(heap.object({1}).weak_cells[0],
                  native::TestCell::immediate(-7));
        EXPECT_EQ(heap.roots()[0],
                  native::TestCell::reference(&heap.object({3})));
    }

    TEST(GCHeapRunner, EmptyHeapAndNonpointerRootsDoNotDiscoverWeakTargets)
    {
        f::HeapSnapshot empty;
        for(auto algorithm: {d::Algorithm::Mark, d::Algorithm::Evacuate})
            EXPECT_EQ(d::run_collection(empty, algorithm), empty);
        empty.external_weak = {
            field(0, int64_t{7}),
            entry(1, std::nullopt, std::nullopt, int64_t{-7})};
        EXPECT_EQ(d::run_collection(empty, d::Algorithm::Mark), empty);
        auto empty_expected = empty;
        empty_expected.external_weak[1] = reset_entry(1);
        EXPECT_EQ(d::run_collection(empty, d::Algorithm::Evacuate),
                  empty_expected);
        auto input = fixture("before");
        input.roots = {int64_t{0}, null_ref(), int64_t{-7}};
        input.external_weak.push_back(field(2, int64_t{42}));
        input.external_weak.push_back(field(3, null_ref()));
        input.external_weak.push_back(
            entry(4, std::nullopt, f::AllocationId{0}, int64_t{9}));
        EXPECT_EQ(d::run_collection(input, d::Algorithm::Mark), input);
        auto expected = input;
        expected.external_weak[0] = reset_entry(1);
        expected.external_weak[3] = reset_entry(4);
        EXPECT_EQ(d::run_collection(input, d::Algorithm::Evacuate), expected);
    }

    f::HeapSnapshot mixed_heap(f::CopyTo first)
    {
        auto second = first == f::CopyTo::Ordinary ? f::CopyTo::Nonmoving
                                                   : f::CopyTo::Ordinary;
        return {{ref(3), int64_t{-5}, ref(3), null_ref()},
                {{f::ObjectId{"obj-2"},
                  f::AllocationId{0},
                  f::OldOriginal{second, f::NormalHeader{false}},
                  {ref(3), ref(0)},
                  {field(7, ref(2))}},
                 {f::ObjectId{"obj-0"},
                  f::AllocationId{1},
                  f::NonmovingOriginal{false},
                  {ref(3)},
                  {field(5, ref(3)), entry(6, f::AllocationId{3},
                                           f::AllocationId{0}, int64_t{0})}},
                 {f::ObjectId{"obj-3"},
                  f::AllocationId{2},
                  f::OldOriginal{f::CopyTo::Ordinary, f::NormalHeader{false}},
                  {ref(1)},
                  {field(8, ref(0)),
                   entry(9, std::nullopt, f::AllocationId{3}, ref(0))}},
                 {f::ObjectId{"obj-1"},
                  f::AllocationId{3},
                  f::YoungOriginal{first, f::NormalHeader{false}},
                  {ref(1), int64_t{7}, ref(0), null_ref()},
                  {field(2, int64_t{-7}), field(3, ref(0)),
                   entry(4, f::AllocationId{3}, f::AllocationId{1}, ref(2)),
                   entry(10, std::nullopt, f::AllocationId{2}, ref(0))}}},
                {entry(0, f::AllocationId{3}, f::AllocationId{1}, ref(0)),
                 field(1, int64_t{0})}};
    }
    f::HeapSnapshot mixed_expected(const f::HeapSnapshot &input)
    {
        auto result = input;
        auto first = input.objects[3];
        auto second = input.objects[0];
        auto first_ordinary = std::get<f::YoungOriginal>(first.state).copy_to ==
                              f::CopyTo::Ordinary;
        first.allocation_id = f::AllocationId{4};
        second.allocation_id = f::AllocationId{5};
        first.state = first_ordinary
                          ? f::ObjectState(f::OrdinaryDestination{})
                          : f::ObjectState(f::NonmovingDestination{});
        second.state = first_ordinary
                           ? f::ObjectState(f::NonmovingDestination{})
                           : f::ObjectState(f::OrdinaryDestination{});
        first.strong = {ref(1), int64_t{7}, ref(5), null_ref()};
        second.strong = {ref(4), ref(5)};
        first.weak = {field(2, int64_t{-7}), field(3, ref(5)), reset_entry(4),
                      reset_entry(10)};
        second.weak = {field(7, null_ref())};
        std::get<f::OldOriginal>(result.objects[0].state).header =
            f::ForwardedHeader{f::AllocationId{5}};
        result.objects[1].state = f::NonmovingOriginal{true};
        result.objects[1].strong = {ref(4)};
        result.objects[1].weak = {
            field(5, ref(4)),
            entry(6, f::AllocationId{4}, f::AllocationId{5}, int64_t{0})};
        std::get<f::YoungOriginal>(result.objects[3].state).header =
            f::ForwardedHeader{f::AllocationId{4}};
        result.objects.push_back(first);
        result.objects.push_back(second);
        result.roots = {ref(4), int64_t{-5}, ref(4), null_ref()};
        result.external_weak[0] =
            entry(0, f::AllocationId{4}, f::AllocationId{1}, ref(5));
        return result;
    }

    TEST(GCHeapRunner, PermutedIdentitiesMixedDestinationsCyclesAndWeakRepair)
    {
        for(auto first: {f::CopyTo::Ordinary, f::CopyTo::Nonmoving})
        {
            SCOPED_TRACE(static_cast<int>(first));
            auto input = mixed_heap(first);
            auto expected = mixed_expected(input);
            std::reverse(input.objects.begin(), input.objects.end());
            NativeHeap heap(input);
            heap.collect(d::Algorithm::Evacuate);
            EXPECT_EQ(heap.capture(), expected);
            EXPECT_EQ(heap.object({4}).logical_identity, 1);
            EXPECT_EQ(heap.object({5}).logical_identity, 2);
            // Copies own distinct weak storage; originals remain untouched.
            EXPECT_NE(heap.object({3}).weak_cells.data(),
                      heap.object({4}).weak_cells.data());
            EXPECT_EQ(heap.object({3}).weak_entries[0].repair_count, 0);
            EXPECT_GT(heap.object({4}).weak_entries[0].repair_count, 0);
            EXPECT_EQ(heap.object({2}).weak_entries[0].repair_count, 0);
        }
    }

    TEST(GCHeapRunner, AllocationOrdinalsContinueAcrossArenaChunkBoundaries)
    {
        constexpr uint64_t count = 140;
        f::HeapSnapshot input;
        input.roots = {ref(0), ref(0)};
        for(uint64_t i = 0; i < count; ++i)
            input.objects.push_back(
                {f::ObjectId{"obj-" + std::to_string(i)},
                 f::AllocationId{i},
                 f::YoungOriginal{i % 2 == 0 ? f::CopyTo::Ordinary
                                             : f::CopyTo::Nonmoving,
                                  f::NormalHeader{false}},
                 {ref((i + 1) % count), ref(i), int64_t{7}},
                 {}});
        auto expected = input;
        expected.roots = {ref(count), ref(count)};
        for(uint64_t i = 0; i < count; ++i)
        {
            std::get<f::YoungOriginal>(expected.objects[i].state).header =
                f::ForwardedHeader{f::AllocationId{count + i}};
            expected.objects.push_back(
                {input.objects[i].object_id,
                 f::AllocationId{count + i},
                 i % 2 == 0 ? f::ObjectState(f::OrdinaryDestination{})
                            : f::ObjectState(f::NonmovingDestination{}),
                 {ref(count + (i + 1) % count), ref(count + i), int64_t{7}},
                 {}});
        }
        EXPECT_EQ(d::run_collection(input, d::Algorithm::Evacuate), expected);
        auto marked = input;
        for(auto &a: marked.objects)
            std::get<f::YoungOriginal>(a.state).header = f::NormalHeader{true};
        EXPECT_EQ(d::run_collection(input, d::Algorithm::Mark), marked);
    }

    TEST(GCHeapRunner, RejectsInvalidInputAndCollectionReplay)
    {
        EXPECT_THROW(d::run_collection(fixture("after"), d::Algorithm::Mark),
                     f::FormatError);
        auto bad = fixture("before");
        bad.roots[0] = ref(3);
        EXPECT_THROW(d::run_collection(bad, d::Algorithm::Evacuate),
                     f::FormatError);
        NativeHeap heap(fixture("before"));
        heap.collect(d::Algorithm::Mark);
        EXPECT_THROW(heap.collect(d::Algorithm::Evacuate), d::RunnerError);
        EXPECT_EQ(heap.capture(), fixture("after_mark"));
        NativeHeap unknown(fixture("before"));
        EXPECT_THROW(unknown.collect(static_cast<d::Algorithm>(99)),
                     d::RunnerError);
        EXPECT_THROW(unknown.capture(), d::RunnerError);
    }

    TEST(GCHeapRunner, CaptureRejectsCorruptNativeStateAndLayouts)
    {
        auto check = [&](auto mutate) {
            NativeHeap heap(fixture("before"));
            heap.collect(d::Algorithm::Evacuate);
            mutate(heap);
            EXPECT_THROW(heap.capture(), d::RunnerError);
        };
        check([](auto &h) {
            h.object({3}).storage = native::TestStorage::Source;
        });
        check([](auto &h) {
            h.object({1}).storage = native::TestStorage::OrdinaryDestination;
        });
        check([](auto &h) {
            h.object({0}).storage = native::TestStorage::StableDestination;
        });
        check([](auto &h) {
            h.object({3}).storage = native::TestStorage::StableDestination;
        });
        check([](auto &h) {
            h.object({1}).generation = native::TestGeneration::Old;
        });
        check([](auto &h) {
            h.object({0}).generation = native::TestGeneration::Young;
        });
        check([](auto &h) {
            h.object({3}).generation = native::TestGeneration::Young;
        });
        check([](auto &h) {
            h.object({3}).header =
                native::TestHeader::forwarded(&h.object({0}));
        });
        check([](auto &h) {
            h.object({0}).header =
                native::TestHeader::forwarded(&h.object({3}));
        });
        check([](auto &h) { h.object({3}).header.target = &h.object({0}); });
        check([](auto &h) {
            h.object({1}).header = {native::TestHeaderKind::Forwarded, nullptr};
        });
        check([](auto &h) {
            h.object({1}).header = native::TestHeader::normal();
        });
        check([](auto &h) { h.object({1}).header.target = &h.object({0}); });
        check([](auto &h) {
            h.object({1}).header.kind = static_cast<native::TestHeaderKind>(99);
        });
        check([](auto &h) { h.adapter().try_mark(&h.object({1})); });
        check([](auto &h) { h.object({1}).logical_identity = 2; });
        check([](auto &h) { h.object({3}).logical_identity = 0; });
        check([](auto &h) {
            h.object({1}).preferred_destination =
                native::EvacuationDestination::Stable;
        });
        check([](auto &h) { h.object({3}).strong_cells.clear(); });
        check([](auto &h) { h.object({1}).weak_cells.clear(); });
        check([](auto &h) {
            h.object({3}).weak_entries.push_back(
                {{nullptr, nullptr}, native::TestCell::immediate(0)});
        });
        check([](auto &h) {
            h.external_weak()[0] = native::TestCell::immediate(0);
        });
    }

    TEST(GCHeapRunner, CaptureRejectsUnknownAddressesWithoutDereferencingThem)
    {
        native::TestObject outside{};
        auto check = [&](auto mutate) {
            NativeHeap heap(fixture("before"));
            heap.collect(d::Algorithm::Evacuate);
            mutate(heap);
            EXPECT_THROW(heap.capture(), d::RunnerError);
        };
        check([&](auto &h) {
            h.roots()[0] = native::TestCell::reference(&outside);
        });
        check([&](auto &h) {
            h.object({3}).strong_cells[0] =
                native::TestCell::reference(&outside);
        });
        check([&](auto &h) { h.object({1}).header.target = &outside; });
        check([&](auto &h) {
            h.object({1}).weak_cells[0] = native::TestCell::reference(&outside);
        });
        check([&](auto &h) {
            std::get<native::TestWeakEntry>(h.external_weak()[0]).guards[0] =
                &outside;
        });
        check([&](auto &h) {
            std::get<native::TestWeakEntry>(h.external_weak()[0]).payload =
                native::TestCell::reference(&outside);
        });
    }

    TEST(GCHeapRunner, CaptureRejectsWrongDestinationAssociation)
    {
        NativeHeap heap(mixed_heap(f::CopyTo::Ordinary));
        heap.collect(d::Algorithm::Evacuate);
        auto &original = heap.object({3});
        original.header.target = &heap.object({5});
        EXPECT_THROW(heap.capture(), d::RunnerError);
        original.header.target = &heap.object({4});
        heap.object({5}).storage = native::TestStorage::NonMoving;
        EXPECT_THROW(heap.capture(), d::RunnerError);
    }

    TEST(GCHeapRunner, CaptureRejectsUnmarkedDestination)
    {
        for(auto policy: {f::CopyTo::Ordinary, f::CopyTo::Nonmoving})
        {
            auto input = fixture("before");
            std::get<f::YoungOriginal>(input.objects[1].state).copy_to = policy;
            NativeHeap heap(input);
            auto *source = &heap.object({1});
            auto *copy = heap.adapter().copy_object(source);
            heap.adapter().install_forwarding(source, copy);
            EXPECT_THROW(heap.capture(), d::RunnerError);
        }
    }

    TEST(GCHeapRunner,
         CaptureReadsActualCellsAndExcludesUnusedPlacementMetadata)
    {
        NativeHeap heap(fixture("before"));
        heap.collect(d::Algorithm::Evacuate);
        heap.object({0}).preferred_destination =
            native::EvacuationDestination::Stable;
        heap.object({3}).preferred_destination =
            native::EvacuationDestination::Stable;
        EXPECT_EQ(heap.capture(), fixture("after"));
        // Do not reconstruct repaired values from the expected input graph.
        heap.roots()[0] = native::TestCell::reference(&heap.object({2}));
        heap.object({3}).strong_cells[0] =
            native::TestCell::reference(&heap.object({1}));
        heap.object({1}).weak_cells[0] = native::TestCell::immediate(99);
        std::get<native::TestWeakEntry>(heap.external_weak()[0]).payload =
            native::TestCell::immediate(42);
        auto expected = fixture("after");
        expected.roots[0] = ref(2);
        expected.objects[3].strong[0] = ref(1);
        expected.objects[1].weak[0] = field(0, int64_t{99});
        expected.external_weak[0] =
            entry(1, std::nullopt, std::nullopt, int64_t{42});
        EXPECT_EQ(heap.capture(), expected);
    }
}  // namespace
