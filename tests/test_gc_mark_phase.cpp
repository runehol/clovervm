#include "gc/strong_closure.h"
#include "gc_test_heap.h"

#include "builtin_types/float.h"
#include "gc/clover_heap_adapter.h"
#include "object_model/overflow_slots.h"
#include "runtime/expanded_call.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <span>
#include <unordered_set>
#include <variant>
#include <vector>

namespace
{
    namespace fixture = cl::gc::test;
    using fixture::TestCell;
    using fixture::TestHeap;
    using fixture::TestHeapAdapter;
    using fixture::TestObject;
    using fixture::TestSlot;

    // Independent fixed-point oracle over the fixture's declared graph. It
    // neither calls adapter/slot methods nor reads the collector's mark state.
    std::unordered_set<TestObject *>
    expected_reachable(const TestHeap &heap, std::span<const TestCell> roots)
    {
        std::unordered_set<TestObject *> reachable;
        auto include = [&](const TestCell &cell) {
            auto pointer = std::get_if<TestObject *>(&cell.contents);
            return pointer != nullptr && *pointer != nullptr &&
                   reachable.insert(*pointer).second;
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
                if(!reachable.contains(object.get()))
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
        return reachable;
    }

    void expect_closure(TestHeap &heap, std::span<TestCell> roots)
    {
        auto expected = expected_reachable(heap, roots);
        std::vector<TestCell> original_roots(roots.begin(), roots.end());
        std::vector<std::vector<TestCell>> original_cells;
        std::vector<std::vector<TestObject *>> original_weak;
        std::vector<TestCell> original_companions;
        for(const auto &object: heap.objects())
        {
            original_cells.push_back(object->strong_cells);
            original_weak.push_back(object->weak_targets);
            original_companions.push_back(object->companion);
        }

        TestHeapAdapter adapter(heap);
        cl::gc::StrongClosure<TestHeapAdapter> mark(adapter);
        for(TestCell &root: roots)
        {
            mark.add_root(TestSlot(root));
        }
        mark.drain();

        EXPECT_FALSE(adapter.is_marked(nullptr));
        EXPECT_EQ(original_roots,
                  std::vector<TestCell>(roots.begin(), roots.end()));
        size_t idx = 0;
        for(const auto &object: heap.objects())
        {
            SCOPED_TRACE(object->logical_identity);
            bool live = expected.contains(object.get());
            EXPECT_EQ(live, adapter.is_marked(object.get()));
            EXPECT_EQ(live ? 1u : 0u, adapter.scan_count(object.get()));
            EXPECT_EQ(original_cells[idx], object->strong_cells);
            EXPECT_EQ(original_weak[idx], object->weak_targets);
            EXPECT_EQ(original_companions[idx], object->companion);
            ++idx;
        }
    }
}  // namespace

TEST(GCMarkPhase, EmptyRootsAndNonReferenceCellsDoNotDiscoverObjects)
{
    TestHeap heap;
    expect_closure(heap, {});
    heap.make_object();
    expect_closure(heap, {});
    std::array roots = {TestCell::immediate(17), TestCell::reference(nullptr)};
    expect_closure(heap, roots);
}

TEST(GCMarkPhase, CyclesSharingDuplicateRootsAndWeakEdges)
{
    TestHeap heap;
    TestObject *a = heap.make_object();
    TestObject *b = heap.make_object();
    TestObject *c = heap.make_object();
    TestObject *shared = heap.make_object();
    TestObject *weak_only = heap.make_object();
    TestObject *garbage = heap.make_object();
    a->strong_cells = {TestCell::reference(a),  TestCell::reference(b),
                       TestCell::reference(b),  TestCell::reference(shared),
                       TestCell::immediate(-1), TestCell::reference(nullptr)};
    b->strong_cells = {TestCell::reference(c), TestCell::reference(shared)};
    c->strong_cells = {TestCell::reference(a)};
    a->weak_targets = {weak_only};
    // A disconnected object pointing into the live graph must not itself
    // become reachable. Nor may the weak-only object's strong children.
    garbage->strong_cells = {TestCell::reference(a)};
    weak_only->strong_cells = {TestCell::reference(garbage)};
    std::array roots = {TestCell::reference(a), TestCell::reference(a),
                        TestCell::reference(b)};
    expect_closure(heap, roots);
}

TEST(GCMarkPhase, LongChainDoesNotRequireRecursiveTraversal)
{
    TestHeap heap;
    constexpr size_t length = 50000;
    TestObject *head = heap.make_object();
    TestObject *last = head;
    for(size_t idx = 1; idx < length; ++idx)
    {
        TestObject *next = heap.make_object();
        last->strong_cells.push_back(TestCell::reference(next));
        last = next;
    }
    std::array roots = {TestCell::reference(head)};
    expect_closure(heap, roots);
}

TEST(GCMarkPhase, FreshContextsDoNotInheritMarks)
{
    TestHeap heap;
    TestObject *a = heap.make_object();
    TestObject *b = heap.make_object();
    TestObject *c = heap.make_object();
    a->strong_cells = {TestCell::reference(b)};
    std::array roots = {TestCell::reference(a)};
    expect_closure(heap, roots);
    expect_closure(heap, roots);
    roots[0] = TestCell::reference(c);
    expect_closure(heap, roots);
}

TEST(GCMarkPhase, GeneratedGraphsMatchIndependentOracle)
{
    std::mt19937 random(0x6c6f7665);
    for(size_t trial = 0; trial < 160; ++trial)
    {
        SCOPED_TRACE(trial);
        TestHeap heap;
        size_t count = random() % 33;
        for(size_t idx = 0; idx < count; ++idx)
        {
            heap.make_object();
        }
        auto random_cell = [&]() {
            size_t choice = random() % (count + 2);
            if(choice < count)
            {
                return TestCell::reference(heap.objects()[choice].get());
            }
            return choice == count ? TestCell::reference(nullptr)
                                   : TestCell::immediate(random());
        };
        for(const auto &object: heap.objects())
        {
            size_t cells = random() % 9;
            for(size_t idx = 0; idx < cells; ++idx)
            {
                object->strong_cells.push_back(random_cell());
            }
            object->weak_targets.push_back(
                heap.objects()[random() % count].get());
        }
        std::vector<TestCell> roots;
        size_t root_count = random() % 9;
        for(size_t idx = 0; idx < root_count; ++idx)
        {
            roots.push_back(random_cell());
        }
        expect_closure(heap, roots);
    }
}

TEST(GCCloverHeapAdapter, OrdinaryCellsAreMarkedWithoutOwnershipChanges)
{
    cl::test::VmTestContext context;
    cl::ThreadState *thread = context.thread();
    cl::ThreadState::ActivationScope activation(thread);
    cl::Owned<cl::TValue<cl::Float>> child(
        thread->make_object_value<cl::Float>(3.0));
    cl::OwnedHeapPtr<cl::OverflowSlots> parent(
        thread->make_internal_raw<cl::OverflowSlots>(3, 3));
    cl::OwnedHeapPtr<cl::OverflowSlots> disconnected(
        thread->make_internal_raw<cl::OverflowSlots>(0, 0));
    parent.get()->set(0, child.raw_value());
    parent.get()->set(1, child.raw_value());
    parent.get()->set(2, cl::Value::from_smi(42));
    cl::HeapObject *parent_root = parent.get();
    std::array roots = {cl::Value::None(), cl::Value::not_present(),
                        cl::Value::from_smi(3)};
    auto original_roots = roots;
    int32_t parent_refs = parent.get()->refcount;
    int32_t child_refs = child.extract()->refcount;
    auto parent_lifecycle = parent.get()->lifecycle_state;
    auto child_lifecycle = child.extract()->lifecycle_state;
    size_t zct_size = thread->zero_count_table_size();

    cl::gc::CloverHeapAdapter adapter;
    cl::gc::StrongClosure<cl::gc::CloverHeapAdapter> mark(adapter);
    mark.add_root(cl::HeapReferenceSlot(&parent_root));
    for(cl::Value &root: roots)
    {
        mark.add_root(cl::HeapReferenceSlot(&root));
    }
    mark.drain();

    EXPECT_TRUE(adapter.is_marked(parent.get()));
    EXPECT_TRUE(adapter.is_marked(child.extract()));
    EXPECT_FALSE(adapter.is_marked(disconnected.get()));
    EXPECT_FALSE(adapter.is_marked(nullptr));
    EXPECT_EQ(original_roots, roots);
    EXPECT_EQ(parent.get(), parent_root);
    EXPECT_EQ(child.raw_value(), parent.get()->get(0));
    EXPECT_EQ(child.raw_value(), parent.get()->get(1));
    EXPECT_EQ(cl::Value::from_smi(42), parent.get()->get(2));
    EXPECT_EQ(parent_refs, parent.get()->refcount);
    EXPECT_EQ(child_refs, child.extract()->refcount);
    EXPECT_EQ(parent_lifecycle, parent.get()->lifecycle_state);
    EXPECT_EQ(child_lifecycle, child.extract()->lifecycle_state);
    EXPECT_EQ(zct_size, thread->zero_count_table_size());
}

TEST(GCCloverHeapAdapter, CustomStorageIsStrongButCacheTargetsAreWeak)
{
    cl::test::VmTestContext context;
    cl::ThreadState *thread = context.thread();
    cl::ThreadState::ActivationScope activation(thread);
    cl::OwnedHeapPtr<cl::CodeObject> code(context.compile_file(L"pass"));
    cl::Owned<cl::TValue<cl::Float>> constant(
        thread->make_object_value<cl::Float>(5.0));
    cl::Owned<cl::TValue<cl::String>> keyword(
        thread->make_object_value<cl::String>(L"gc-external-keyword"));
    cl::Owned<cl::TValue<cl::Float>> weak_only(
        thread->make_object_value<cl::Float>(9.0));
    code.get()->constant_table.emplace_back(constant.raw_value());
    auto &caches = code.get()->inline_caches;
    caches.function_call_caches.emplace_back();
    caches.function_call_caches.back().guard_value = weak_only.raw_value();
    caches.expanded_call_caches.resize(1);
    auto &expanded = caches.expanded_call_caches.back();
    expanded.binding_plan = std::make_unique<cl::ExpandedCallCachedPlan>();
    expanded.binding_plan->keyword_names.emplace_back(keyword.value());
    cl::Value root = cl::Value::from_oop(code.get());
    int32_t code_refs = code.get()->refcount;
    int32_t constant_refs = constant.extract()->refcount;
    int32_t keyword_refs = keyword.extract()->refcount;
    int32_t weak_refs = weak_only.extract()->refcount;
    auto code_lifecycle = code.get()->lifecycle_state;
    size_t zct_size = thread->zero_count_table_size();

    cl::gc::CloverHeapAdapter adapter;
    cl::gc::StrongClosure<cl::gc::CloverHeapAdapter> mark(adapter);
    mark.add_root(cl::HeapReferenceSlot(&root));
    mark.drain();

    EXPECT_TRUE(adapter.is_marked(code.get()));
    EXPECT_TRUE(adapter.is_marked(constant.extract()));
    EXPECT_TRUE(adapter.is_marked(keyword.extract()));
    EXPECT_FALSE(adapter.is_marked(weak_only.extract()));
    EXPECT_EQ(cl::Value::from_oop(code.get()), root);
    EXPECT_EQ(constant.raw_value(), code.get()->constant_table.back().value());
    EXPECT_EQ(keyword.value(), expanded.binding_plan->keyword_names[0].value());
    EXPECT_EQ(weak_only.raw_value(),
              caches.function_call_caches.back().guard_value);
    EXPECT_EQ(code_refs, code.get()->refcount);
    EXPECT_EQ(constant_refs, constant.extract()->refcount);
    EXPECT_EQ(keyword_refs, keyword.extract()->refcount);
    EXPECT_EQ(weak_refs, weak_only.extract()->refcount);
    EXPECT_EQ(code_lifecycle, code.get()->lifecycle_state);
    EXPECT_EQ(zct_size, thread->zero_count_table_size());
}
