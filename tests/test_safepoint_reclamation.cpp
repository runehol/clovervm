#include "test_helpers.h"

#include "global_heap.h"
#include "refcount.h"
#include "safepoint_reclamation.h"

#include <gtest/gtest.h>

namespace cl
{
    namespace
    {
        class ReclamationTestObject : public HeapObject
        {
        public:
            CL_DECLARE_STATIC_LAYOUT_WITH_VALUES(ReclamationTestObject, values,
                                                 2);

            ReclamationTestObject() : HeapObject(compact_layout())
            {
                values[0] = Value::not_present();
                values[1] = Value::not_present();
            }

            Value values[2];
        };

        void drain_supported_zct_entries(ThreadState *thread)
        {
            SafepointRootSet roots;
            process_zero_count_table_for_safepoint(*thread, roots);
        }
    }  // namespace

    TEST(SafepointReclamation, RootCollectionIncludesFrameSlotRoot)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        String *string = thread->make_object_raw<String>(L"root");
        Value *slot = thread->clover_frame_sentinel() - 1;
        *slot = Value::from_oop(string);
        thread->publish_safepoint_scan_record(slot, Value::not_present());

        SafepointRootSet roots;
        collect_safepoint_roots_from_thread(roots, *thread);

        EXPECT_TRUE(roots.contains(string));
    }

    TEST(SafepointReclamation, RootCollectionIncludesAccumulatorRoot)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        String *string = thread->make_object_raw<String>(L"accumulator");
        thread->publish_safepoint_scan_record(thread->clover_frame_sentinel(),
                                              Value::from_oop(string));

        SafepointRootSet roots;
        collect_safepoint_roots_from_thread(roots, *thread);

        EXPECT_TRUE(roots.contains(string));
    }

    TEST(SafepointReclamation, RootCollectionDoesNotDereferenceJunk)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        Value junk;
        junk.as.integer = 0x1010;
        Value *slot = thread->clover_frame_sentinel() - 1;
        *slot = junk;
        thread->publish_safepoint_scan_record(slot, Value::not_present());

        SafepointRootSet roots;
        collect_safepoint_roots_from_thread(roots, *thread);

        EXPECT_TRUE(roots.contains(reinterpret_cast<HeapObject *>(0x1010)));
    }

    TEST(SafepointReclamation, ZctProcessingRestoresPositiveRefcountEntry)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        size_t zct_size_before = thread->zero_count_table_size();
        String *string = thread->make_object_raw<String>(L"retained");
        ASSERT_EQ(zct_size_before + 1, thread->zero_count_table_size());
        ASSERT_EQ(HeapLifecycleState::InZct, string->lifecycle_state);

        incref_heap_ptr(string);
        SafepointRootSet roots;
        process_zero_count_table_for_safepoint(*thread, roots);

        EXPECT_EQ(1, string->refcount);
        EXPECT_EQ(HeapLifecycleState::Normal, string->lifecycle_state);
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(string));
        decref_heap_ptr(string);
    }

    TEST(SafepointReclamation, ZctProcessingKeepsStackRootedZeroEntry)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        size_t zct_size_before = thread->zero_count_table_size();
        String *string = thread->make_object_raw<String>(L"stack-rooted");
        ASSERT_EQ(zct_size_before + 1, thread->zero_count_table_size());
        Value *slot = thread->clover_frame_sentinel() - 1;
        *slot = Value::from_oop(string);
        thread->publish_safepoint_scan_record(slot, Value::not_present());
        SafepointRootSet roots;
        collect_safepoint_roots_from_thread(roots, *thread);

        process_zero_count_table_for_safepoint(*thread, roots);

        EXPECT_EQ(0, string->refcount);
        EXPECT_EQ(HeapLifecycleState::InZct, string->lifecycle_state);
        EXPECT_TRUE(thread->zero_count_table_contains_for_testing(string));
        *slot = Value::not_present();
    }

    TEST(SafepointReclamation, ZctProcessingReclaimsUnrootedZeroEntry)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        drain_supported_zct_entries(thread);
        GlobalHeap &heap = context.vm().get_refcounted_global_heap();
        uint64_t blockers_before_alloc =
            heap.total_reclaim_blockers_for_testing();
        ReclamationTestObject *object =
            thread->make_internal_raw<ReclamationTestObject>();
        ASSERT_TRUE(thread->zero_count_table_contains_for_testing(object));
        uint64_t blockers_after_alloc =
            heap.total_reclaim_blockers_for_testing();
        ASSERT_EQ(blockers_before_alloc + 1, blockers_after_alloc);

        SafepointRootSet roots;
        process_zero_count_table_for_safepoint(*thread, roots);

        EXPECT_EQ(blockers_after_alloc - 1,
                  heap.total_reclaim_blockers_for_testing());
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(object));
    }

    TEST(SafepointReclamation, ZctProcessingReclaimsCascadedChild)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ThreadState::ActivationScope active_thread(thread);
        drain_supported_zct_entries(thread);
        GlobalHeap &heap = context.vm().get_refcounted_global_heap();
        uint64_t blockers_before_alloc =
            heap.total_reclaim_blockers_for_testing();
        ReclamationTestObject *child =
            thread->make_internal_raw<ReclamationTestObject>();
        ReclamationTestObject *owner =
            thread->make_internal_raw<ReclamationTestObject>();
        incref_heap_ptr(child);
        owner->values[0].as.ptr = child;
        ASSERT_EQ(1, child->refcount);
        ASSERT_TRUE(thread->zero_count_table_contains_for_testing(owner));
        ASSERT_TRUE(thread->zero_count_table_contains_for_testing(child));
        uint64_t blockers_after_alloc =
            heap.total_reclaim_blockers_for_testing();
        ASSERT_EQ(blockers_before_alloc + 2, blockers_after_alloc);

        SafepointRootSet roots;
        process_zero_count_table_for_safepoint(*thread, roots);

        EXPECT_EQ(blockers_after_alloc - 2,
                  heap.total_reclaim_blockers_for_testing());
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(owner));
        EXPECT_FALSE(thread->zero_count_table_contains_for_testing(child));
    }
}  // namespace cl
