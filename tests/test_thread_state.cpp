#include "test_helpers.h"

#include "exception_object.h"

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

    TEST(ThreadState, PendingExceptionObjectStoresTypedObject)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ClassObject *base_exception =
            context.vm().class_for_native_layout(NativeLayoutId::Exception);
        ThreadState::ActivationScope active_thread(thread);
        TValue<ExceptionObject> exception = make_exception_object(
            TValue<ClassObject>::from_oop(base_exception), "boom");

        thread->set_pending_exception_object(exception);

        EXPECT_TRUE(thread->has_pending_exception());
        EXPECT_EQ(PendingExceptionKind::Object,
                  thread->pending_exception_kind());
        EXPECT_EQ(exception.as_value(), thread->pending_exception_object());
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

    TEST(ThreadState, PendingExceptionStringCreatesExceptionObject)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ClassObject *base_exception =
            context.vm().class_for_native_layout(NativeLayoutId::Exception);
        ThreadState::ActivationScope active_thread(thread);

        thread->set_pending_exception_string(
            TValue<ClassObject>::from_oop(base_exception), "boom");

        EXPECT_TRUE(thread->has_pending_exception());
        EXPECT_EQ(PendingExceptionKind::Object,
                  thread->pending_exception_kind());
        TValue<ExceptionObject> exception =
            TValue<ExceptionObject>::from_value_checked(
                thread->pending_exception_object());
        EXPECT_EQ(base_exception, exception.extract()->get_class().extract());
        TValue<String> message =
            TValue<String>::from_value_checked(exception.extract()->message);
        EXPECT_STREQ(L"boom", message.extract()->data);
    }

}  // namespace cl
