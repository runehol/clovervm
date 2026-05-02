#include "test_helpers.h"

#include <gtest/gtest.h>

namespace cl
{

    TEST(ThreadState, PendingExceptionStartsClear)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();

        EXPECT_FALSE(thread->has_pending_exception());
        EXPECT_EQ(PendingExceptionKind::None, thread->pending_exception_kind());
    }

    TEST(ThreadState, PendingExceptionObjectStoresValue)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();

        thread->set_pending_exception_object(Value::True());

        EXPECT_TRUE(thread->has_pending_exception());
        EXPECT_EQ(PendingExceptionKind::Object,
                  thread->pending_exception_kind());
        EXPECT_EQ(Value::True(), thread->pending_exception_object());
    }

    TEST(ThreadState, PendingStopIterationNoValueUsesNotPresent)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();

        thread->set_pending_stop_iteration_no_value();

        EXPECT_TRUE(thread->has_pending_exception());
        EXPECT_EQ(PendingExceptionKind::StopIteration,
                  thread->pending_exception_kind());
        EXPECT_TRUE(thread->pending_stop_iteration_value().is_not_present());
    }

    TEST(ThreadState, PendingStopIterationValueDistinguishesNone)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();

        thread->set_pending_stop_iteration_value(Value::None());

        EXPECT_TRUE(thread->has_pending_exception());
        EXPECT_EQ(PendingExceptionKind::StopIteration,
                  thread->pending_exception_kind());
        EXPECT_EQ(Value::None(), thread->pending_stop_iteration_value());
        EXPECT_FALSE(thread->pending_stop_iteration_value().is_not_present());
    }

    TEST(ThreadState, ClearPendingExceptionResetsState)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();

        thread->set_pending_stop_iteration_value(Value::from_smi(42));
        thread->clear_pending_exception();

        EXPECT_FALSE(thread->has_pending_exception());
        EXPECT_EQ(PendingExceptionKind::None, thread->pending_exception_kind());
    }

}  // namespace cl
