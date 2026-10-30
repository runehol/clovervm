#include "memory/heap_reclamation.h"
#include "memory/scoped_root.h"
#include "object_model/heap_ptr.h"
#include "object_model/shape.h"
#include "object_model/validity_cell.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace cl;

namespace
{
    constexpr size_t layout_count = static_cast<size_t>(NativeLayoutId::Count);

    std::vector<HeapObject *> persistent_roots(ThreadState &thread)
    {
        std::vector<HeapObject *> roots;
        thread.visit_persistent_root_slots([&](HeapReferenceSlot slot) {
            roots.push_back(slot.load_pointer());
        });
        return roots;
    }

    std::array<Shape *, 5> cached_shapes(ThreadState &thread)
    {
        return {thread.get_exact_string_shape(), thread.get_exact_tuple_shape(),
                thread.get_exact_list_shape(),
                thread.get_exact_dict_string_key_shape(),
                thread.get_exact_dict_general_shape()};
    }
}  // namespace

TEST(PersistentRoots, CoversClassArrayShapesAndInitializedExceptionCellsOnly)
{
    test::VmTestContext context;
    ThreadState &thread = *context.thread();
    ThreadState::ActivationScope activation(&thread);
    std::vector<HeapObject *> expected;
    for(size_t idx = 0; idx < layout_count; ++idx)
    {
        expected.push_back(context.vm().class_for_native_layout(
            static_cast<NativeLayoutId>(idx)));
    }
    for(Shape *shape: cached_shapes(thread))
    {
        expected.push_back(shape);
    }
    expected.insert(expected.end(), {nullptr, nullptr});
    EXPECT_EQ(layout_count + 7, expected.size());
    EXPECT_EQ(expected, persistent_roots(thread));
    EXPECT_EQ(nullptr, expected[static_cast<size_t>(NativeLayoutId::Invalid)]);
    EXPECT_EQ(nullptr, expected[static_cast<size_t>(NativeLayoutId::TestOnly)]);

    // None of these belong to the persistent-member visitor: the ZCT is not
    // a strong root, and scoped roots / interpreter publication are separate.
    ValidityCell *garbage = thread.make_internal_raw<ValidityCell>(
        ValidityCellDependencyMutability::Mutable);
    thread.add_to_zero_count_table_if_needed(garbage);
    Owned<TValue<String>> native(thread.make_object_value<String>(L"native"));
    ScopedRoot root(thread, HeapReferenceSlot(&native));
    thread.publish_safepoint_scan_record(thread.clover_frame_sentinel(),
                                         native.raw_value());
    EXPECT_EQ(expected, persistent_roots(thread));
    thread.publish_safepoint_scan_record(thread.clover_frame_sentinel(),
                                         Value::not_present());
}

TEST(PersistentRoots, RewritesEveryCachedCellWithoutChangingOwnership)
{
    test::VmTestContext context;
    ThreadState &thread = *context.thread();
    ThreadState::ActivationScope activation(&thread);
    std::vector<HeapObject *> originals = persistent_roots(thread);
    auto shapes = cached_shapes(thread);
    OwnedHeapPtr<Shape> replacement_shape(
        shapes.back()->clone_with_flags(shapes.back()->flags()));
    auto replacement_for = [&](HeapObject *original) -> HeapObject * {
        if(original->native_layout_id() == NativeLayoutId::ClassObject)
        {
            return original == context.vm().object_class()
                       ? context.vm().type_class()
                       : context.vm().object_class();
        }
        EXPECT_EQ(NativeLayoutId::Shape, original->native_layout_id());
        if(heap_ptr_is_refcounted(original))
        {
            return replacement_shape.get();
        }
        return original == shapes[0] ? shapes[1] : shapes[0];
    };
    std::vector<int32_t> refcounts;
    for(HeapObject *original: originals)
    {
        refcounts.push_back(original == nullptr ? 0 : original->refcount);
    }
    int32_t replacement_refs = replacement_shape.get()->refcount;
    size_t zct_size = thread.zero_count_table_size();
    thread.visit_persistent_root_slots([&](HeapReferenceSlot slot) {
        if(HeapObject *original = slot.load_pointer())
        {
            slot.rewrite_pointer(replacement_for(original));
        }
    });
    for(size_t idx = 0; idx < layout_count; ++idx)
    {
        if(originals[idx] != nullptr)
        {
            EXPECT_EQ(replacement_for(originals[idx]),
                      thread.class_for_native_layout(
                          static_cast<NativeLayoutId>(idx)));
        }
    }
    auto rewritten_shapes = cached_shapes(thread);
    for(size_t idx = 0; idx < shapes.size(); ++idx)
    {
        EXPECT_EQ(replacement_for(shapes[idx]), rewritten_shapes[idx]);
    }
    for(size_t idx = 0; idx < originals.size(); ++idx)
    {
        if(originals[idx] != nullptr)
        {
            EXPECT_EQ(refcounts[idx], originals[idx]->refcount);
        }
    }
    EXPECT_EQ(replacement_refs, replacement_shape.get()->refcount);
    EXPECT_EQ(zct_size, thread.zero_count_table_size());

    // No dispatch or collection while caches describe unrelated replacement
    // objects. Restore each physical cell, preserving its original aliasing.
    size_t index = 0;
    thread.visit_persistent_root_slots([&](HeapReferenceSlot slot) {
        HeapObject *original = originals[index++];
        if(original != nullptr)
        {
            slot.rewrite_pointer(original);
        }
    });
    EXPECT_EQ(originals, persistent_roots(thread));
}

TEST(PersistentRoots, PendingExceptionObjectRewriteIsGetterVisibleAndRCNeutral)
{
    test::VmTestContext context;
    ThreadState &thread = *context.thread();
    ThreadState::ActivationScope activation(&thread);
    ASSERT_TRUE(thread.set_pending_builtin_exception_none(L"TypeError")
                    .is_exception_marker());
    Owned<TValue<Exception>> original(thread.pending_exception_object());
    ASSERT_TRUE(thread.set_pending_builtin_exception_none(L"ValueError")
                    .is_exception_marker());
    Owned<TValue<Exception>> replacement(thread.pending_exception_object());
    ASSERT_TRUE(thread.set_pending_exception_object(original.value())
                    .is_exception_marker());
    int32_t original_refs = original.extract()->refcount;
    int32_t replacement_refs = replacement.extract()->refcount;
    size_t zct_size = thread.zero_count_table_size();
    size_t rewrites = 0;
    thread.visit_persistent_root_slots([&](HeapReferenceSlot slot) {
        if(slot.load_pointer() == original.extract())
        {
            slot.rewrite_pointer(replacement.extract());
            ++rewrites;
        }
    });
    EXPECT_EQ(1u, rewrites);
    EXPECT_EQ(PendingExceptionKind::Object, thread.pending_exception_kind());
    EXPECT_EQ(replacement.extract(),
              thread.pending_exception_object().extract());
    EXPECT_EQ(original_refs, original.extract()->refcount);
    EXPECT_EQ(replacement_refs, replacement.extract()->refcount);
    EXPECT_EQ(zct_size, thread.zero_count_table_size());
    ReclamationRootSet roots;
    collect_reclamation_roots_from_thread(roots, thread);
    EXPECT_TRUE(roots.contains(replacement.extract()));
    EXPECT_FALSE(roots.contains(original.extract()));

    // Restore the simulated relocation before releasing pending ownership.
    thread.visit_persistent_root_slots([&](HeapReferenceSlot slot) {
        if(slot.load_pointer() == replacement.extract())
        {
            slot.rewrite_pointer(original.extract());
        }
    });
    thread.clear_pending_exception();
    EXPECT_FALSE(thread.has_pending_exception());
    auto cleared = persistent_roots(thread);
    EXPECT_EQ(nullptr, cleared[cleared.size() - 2]);
    EXPECT_EQ(nullptr, cleared.back());
}

TEST(PersistentRoots, PendingStopIterationVisitsPointerAndInlinePayloads)
{
    test::VmTestContext context;
    ThreadState &thread = *context.thread();
    ThreadState::ActivationScope activation(&thread);
    Owned<TValue<String>> original(thread.make_object_value<String>(L"old"));
    Owned<TValue<String>> replacement(thread.make_object_value<String>(L"new"));
    ASSERT_TRUE(thread.set_pending_stop_iteration_value(original.raw_value())
                    .is_exception_marker());
    int32_t original_refs = original.extract()->refcount;
    int32_t replacement_refs = replacement.extract()->refcount;
    size_t zct_size = thread.zero_count_table_size();
    size_t rewrites = 0;
    thread.visit_persistent_root_slots([&](HeapReferenceSlot slot) {
        if(slot.load_pointer() == original.extract())
        {
            slot.rewrite_pointer(replacement.extract());
            ++rewrites;
        }
    });
    EXPECT_EQ(1u, rewrites);
    EXPECT_EQ(PendingExceptionKind::StopIteration,
              thread.pending_exception_kind());
    EXPECT_EQ(replacement.raw_value(), thread.pending_stop_iteration_value());
    EXPECT_EQ(original_refs, original.extract()->refcount);
    EXPECT_EQ(replacement_refs, replacement.extract()->refcount);
    EXPECT_EQ(zct_size, thread.zero_count_table_size());
    ReclamationRootSet roots;
    collect_reclamation_roots_from_thread(roots, thread);
    EXPECT_TRUE(roots.contains(replacement.extract()));
    EXPECT_FALSE(roots.contains(original.extract()));
    thread.visit_persistent_root_slots([&](HeapReferenceSlot slot) {
        if(slot.load_pointer() == replacement.extract())
        {
            slot.rewrite_pointer(original.extract());
        }
    });

    for(Value value: {Value::from_smi(7), Value::None(), Value::not_present()})
    {
        EXPECT_TRUE(thread.set_pending_stop_iteration_value(value)
                        .is_exception_marker());
        auto roots = persistent_roots(thread);
        EXPECT_EQ(nullptr, roots[roots.size() - 2]);
        EXPECT_EQ(nullptr, roots.back());
        EXPECT_EQ(value, thread.pending_stop_iteration_value());
    }
    EXPECT_TRUE(
        thread.set_pending_stop_iteration_no_value().is_exception_marker());
    EXPECT_EQ(Value::not_present(), thread.pending_stop_iteration_value());
    thread.clear_pending_exception();
}

TEST(PersistentRoots, CachedShapeKeepsZeroCountObjectAliveUntilCacheIsRestored)
{
    test::VmTestContext context;
    ThreadState &thread = *context.thread();
    ThreadState::ActivationScope activation(&thread);
    Shape *original = thread.get_exact_dict_general_shape();
    Shape *cached = original->clone_with_flags(original->flags());
    thread.add_to_zero_count_table_if_needed(cached);
    ASSERT_EQ(0, cached->refcount);
    thread.cache_exact_dict_shapes(thread.get_exact_dict_string_key_shape(),
                                   cached);
    context.vm().run_heap_reclamation();
    EXPECT_EQ(0, cached->refcount);
    EXPECT_TRUE(thread.zero_count_table_contains_for_testing(cached));
    EXPECT_EQ(cached, thread.get_exact_dict_general_shape());

    thread.cache_exact_dict_shapes(thread.get_exact_dict_string_key_shape(),
                                   original);
    context.vm().run_heap_reclamation();
    EXPECT_FALSE(thread.zero_count_table_contains_for_testing(cached));
}
