#include "builtin_types/float.h"
#include "memory/global_heap.h"
#include "memory/heap_reclamation.h"
#include "memory/scoped_root.h"
#include "object_model/owned.h"
#include "object_model/validity_cell.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

using namespace cl;

static_assert(!std::is_copy_constructible_v<ScopedRoot>);
static_assert(!std::is_copy_assignable_v<ScopedRoot>);
static_assert(!std::is_move_constructible_v<ScopedRoot>);
static_assert(!std::is_move_assignable_v<ScopedRoot>);

namespace
{
    std::vector<HeapObject *> native_roots(const ThreadState &thread)
    {
        std::vector<HeapObject *> roots;
        thread.visit_native_root_slots([&](HeapReferenceSlot slot) {
            roots.push_back(slot.load_pointer());
        });
        return roots;
    }
}  // namespace

TEST(ScopedRoot, NestedScopesRestorePreviousChain)
{
    test::VmTestContext context;
    ThreadState &thread = *context.thread();
    auto first = context.vm().get_or_create_interned_string_value(L"first");
    auto second = context.vm().get_or_create_interned_string_value(L"second");
    Value outer = first.raw_value();
    Value inner = second.raw_value();
    EXPECT_TRUE(native_roots(thread).empty());
    {
        ScopedRoot root(thread, HeapReferenceSlot(&outer));
        EXPECT_EQ((std::vector<HeapObject *>{first.extract()}),
                  native_roots(thread));
        {
            ScopedRoot nested(thread, HeapReferenceSlot(&inner));
            EXPECT_EQ(
                (std::vector<HeapObject *>{second.extract(), first.extract()}),
                native_roots(thread));
        }
        EXPECT_EQ((std::vector<HeapObject *>{first.extract()}),
                  native_roots(thread));
    }
    EXPECT_TRUE(native_roots(thread).empty());
}

TEST(ScopedRoot, EarlyReturnRestoresChainIndependentlyOfActiveThread)
{
    test::VmTestContext context;
    ThreadState &thread = *context.thread();
    Value cell = Value::None();
    ScopedRoot outer(thread, HeapReferenceSlot(&cell));
    auto enter = [&] {
        ThreadState::NoActiveThreadScope no_active_thread;
        Value temporary = Value::from_smi(1);
        ScopedRoot inner(thread, HeapReferenceSlot(&temporary));
        EXPECT_EQ(2u, native_roots(thread).size());
        return;
    };
    enter();
    EXPECT_EQ(1u, native_roots(thread).size());
}

TEST(ScopedRoot, ThreadChainsAreIndependent)
{
    test::VmTestContext first;
    test::VmTestContext second;
    Value cell = Value::None();
    {
        ScopedRoot first_root(*first.thread(), HeapReferenceSlot(&cell));
        EXPECT_EQ(1u, native_roots(*first.thread()).size());
        EXPECT_TRUE(native_roots(*second.thread()).empty());
        {
            ScopedRoot second_root(*second.thread(), HeapReferenceSlot(&cell));
            EXPECT_EQ(1u, native_roots(*first.thread()).size());
            EXPECT_EQ(1u, native_roots(*second.thread()).size());
        }
        EXPECT_EQ(1u, native_roots(*first.thread()).size());
        EXPECT_TRUE(native_roots(*second.thread()).empty());
    }
    EXPECT_TRUE(native_roots(*first.thread()).empty());
}

TEST(ScopedRoot, ReadsCurrentCellAndAcceptsInlineAndNullValues)
{
    test::VmTestContext context;
    Value value = Value::from_smi(42);
    HeapObject *pointer = nullptr;
    ScopedRoot value_root(*context.thread(), HeapReferenceSlot(&value));
    ScopedRoot pointer_root(*context.thread(), HeapReferenceSlot(&pointer));
    EXPECT_EQ((std::vector<HeapObject *>{nullptr, nullptr}),
              native_roots(*context.thread()));
    EXPECT_EQ(Value::from_smi(42), value);
    EXPECT_EQ(nullptr, pointer);

    auto string = context.vm().get_or_create_interned_string_value(L"now-live");
    value = string.raw_value();
    EXPECT_EQ((std::vector<HeapObject *>{nullptr, string.extract()}),
              native_roots(*context.thread()));
}

TEST(ScopedRoot, RewritesEveryAliasedCellWithoutOwnershipChanges)
{
    test::VmTestContext context;
    ThreadState &thread = *context.thread();
    ThreadState::ActivationScope activation(&thread);
    Owned<TValue<Float>> original(thread.make_object_value<Float>(1));
    Owned<TValue<Float>> replacement(thread.make_object_value<Float>(2));
    Value value = original.raw_value();
    Float *pointer = original.extract();
    Owned<TValue<Float>> owner(original.value());
    ScopedRoot value_root(thread, HeapReferenceSlot(&value));
    ScopedRoot pointer_root(thread, HeapReferenceSlot(&pointer));
    ScopedRoot owner_root(thread, HeapReferenceSlot(&owner));
    int32_t original_refcount = original.extract()->refcount;
    int32_t replacement_refcount = replacement.extract()->refcount;
    size_t zct_size = thread.zero_count_table_size();

    size_t visits = 0;
    thread.visit_native_root_slots([&](HeapReferenceSlot slot) {
        EXPECT_EQ(original.extract(), slot.load_pointer());
        slot.rewrite_pointer(replacement.extract());
        ++visits;
    });
    EXPECT_EQ(3u, visits);
    EXPECT_EQ(replacement.raw_value(), value);
    EXPECT_EQ(replacement.extract(), pointer);
    EXPECT_EQ(replacement.extract(), owner.extract());
    EXPECT_EQ(original_refcount, original.extract()->refcount);
    EXPECT_EQ(replacement_refcount, replacement.extract()->refcount);
    EXPECT_EQ(zct_size, thread.zero_count_table_size());

    ReclamationRootSet roots;
    collect_reclamation_roots_from_thread(roots, thread);
    EXPECT_TRUE(roots.contains(replacement.extract()));
    EXPECT_FALSE(roots.contains(original.extract()));

    // These are unrelated objects, not a real relocation. Restore ownership
    // before the Owned cell's destructor performs ordinary RC cleanup.
    thread.visit_native_root_slots([&](HeapReferenceSlot slot) {
        slot.rewrite_pointer(original.extract());
    });
    EXPECT_EQ(original.raw_value(), value);
    EXPECT_EQ(original.extract(), pointer);
    EXPECT_EQ(original.extract(), owner.extract());
}

TEST(ScopedRoot, ReclamationProtectsOnlyCurrentlyRegisteredReferents)
{
    test::VmTestContext context;
    ThreadState &thread = *context.thread();
    ThreadState::ActivationScope activation(&thread);
    context.vm().run_heap_reclamation();
    GlobalHeap &heap = context.vm().get_refcounted_global_heap();
    uint64_t baseline = heap.count_valid_objects_slow();
    ValidityCell *object = thread.make_internal_raw<ValidityCell>(
        ValidityCellDependencyMutability::Mutable);
    Value cell;
    cell.as.ptr = object;
    thread.add_to_zero_count_table_if_needed(object);
    ASSERT_EQ(0, object->refcount);
    ASSERT_EQ(baseline + 1, heap.count_valid_objects_slow());
    {
        ScopedRoot root(thread, HeapReferenceSlot(&cell));
        ReclamationRootSet roots;
        collect_reclamation_roots_from_thread(roots, thread);
        EXPECT_TRUE(roots.contains(object));
        context.vm().run_heap_reclamation();
        EXPECT_EQ(baseline + 1, heap.count_valid_objects_slow());
        EXPECT_EQ(0, object->refcount);
        EXPECT_TRUE(thread.zero_count_table_contains_for_testing(object));
    }

    ReclamationRootSet roots;
    collect_reclamation_roots_from_thread(roots, thread);
    EXPECT_FALSE(roots.contains(object));
    context.vm().run_heap_reclamation();
    EXPECT_EQ(baseline, heap.count_valid_objects_slow());
    EXPECT_FALSE(thread.zero_count_table_contains_for_testing(object));
}
