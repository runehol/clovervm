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
            TValue<ClassObject>::from_oop(base_exception), L"boom");

        EXPECT_TRUE(thread->set_pending_exception_object(exception)
                        .is_exception_marker());

        EXPECT_TRUE(thread->has_pending_exception());
        EXPECT_EQ(PendingExceptionKind::Object,
                  thread->pending_exception_kind());
        EXPECT_EQ(exception.as_value(), thread->pending_exception_object());
    }

    TEST(ThreadState, PendingStopIterationNoValueUsesNotPresent)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();

        EXPECT_TRUE(thread->set_pending_stop_iteration_no_value()
                        .is_exception_marker());

        EXPECT_TRUE(thread->has_pending_exception());
        EXPECT_EQ(PendingExceptionKind::StopIteration,
                  thread->pending_exception_kind());
        EXPECT_TRUE(thread->pending_stop_iteration_value().is_not_present());
    }

    TEST(ThreadState, PendingStopIterationValueDistinguishesNone)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();

        EXPECT_TRUE(thread->set_pending_stop_iteration_value(Value::None())
                        .is_exception_marker());

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

        EXPECT_TRUE(
            thread->set_pending_stop_iteration_value(Value::from_smi(42))
                .is_exception_marker());
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

        EXPECT_TRUE(
            thread
                ->set_pending_exception_string(
                    TValue<ClassObject>::from_oop(base_exception), L"boom")
                .is_exception_marker());

        EXPECT_TRUE(thread->has_pending_exception());
        EXPECT_EQ(PendingExceptionKind::Object,
                  thread->pending_exception_kind());
        TValue<ExceptionObject> exception =
            TValue<ExceptionObject>::from_value_checked(
                thread->pending_exception_object());
        EXPECT_EQ(base_exception,
                  exception.extract()->get_shape()->get_class());
        TValue<String> message =
            TValue<String>::from_value_checked(exception.extract()->message);
        EXPECT_STREQ(L"boom", message.extract()->data);
    }

    TEST(ThreadState, StopIterationObjectStoresValueSlot)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ClassObject *stop_iteration =
            context.vm().class_for_native_layout(NativeLayoutId::StopIteration);
        ThreadState::ActivationScope active_thread(thread);

        TValue<StopIterationObject> exception = make_stop_iteration_object(
            TValue<ClassObject>::from_oop(stop_iteration), Value::from_smi(42));

        EXPECT_EQ(Value::from_smi(42), exception.extract()->value.as_value());
        TValue<String> message =
            TValue<String>::from_value_checked(exception.extract()->message);
        EXPECT_STREQ(L"", message.extract()->data);
        TValue<ExceptionObject> base_exception =
            TValue<ExceptionObject>::from_value_checked(exception.as_value());
        EXPECT_EQ(exception.as_value(), base_exception.as_value());
    }

    TEST(ThreadState, StopIterationObjectDefaultsValueToNotPresent)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        ClassObject *stop_iteration =
            context.vm().class_for_native_layout(NativeLayoutId::StopIteration);
        ThreadState::ActivationScope active_thread(thread);

        TValue<StopIterationObject> exception = make_stop_iteration_object(
            TValue<ClassObject>::from_oop(stop_iteration));

        EXPECT_TRUE(exception.extract()->value.as_value().is_not_present());
    }

    TEST(ThreadState, ClassOfValueMapsPointerAndInlineValues)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        TValue<String> string =
            context.vm().get_or_create_interned_string_value(L"value");

        EXPECT_EQ(context.vm().str_class(),
                  thread->class_of_value(string.as_value()));
        EXPECT_EQ(context.vm().int_class(),
                  thread->class_of_value(Value::from_smi(42)));
        EXPECT_EQ(context.vm().bool_class(),
                  thread->class_of_value(Value::True()));
        EXPECT_EQ(context.vm().bool_class(),
                  thread->class_of_value(Value::False()));
        EXPECT_EQ(context.vm().none_type_class(),
                  thread->class_of_value(Value::None()));
    }

    TEST(ThreadState, ShapeOfValueMapsPointerAndInlineValues)
    {
        test::VmTestContext context;
        ThreadState *thread = context.thread();
        TValue<String> string =
            context.vm().get_or_create_interned_string_value(L"value");

        EXPECT_EQ(string.extract()->get_shape(),
                  thread->shape_of_value(string.as_value()));
        EXPECT_EQ(context.vm().smi_shape(),
                  thread->shape_of_value(Value::from_smi(42)));
        EXPECT_EQ(context.vm().int_class(),
                  thread->shape_of_value(Value::from_smi(42))->get_class());
        EXPECT_EQ(context.vm().bool_shape(),
                  thread->shape_of_value(Value::True()));
        EXPECT_EQ(context.vm().bool_shape(),
                  thread->shape_of_value(Value::False()));
        EXPECT_EQ(context.vm().bool_class(),
                  thread->shape_of_value(Value::True())->get_class());
        EXPECT_EQ(context.vm().none_shape(),
                  thread->shape_of_value(Value::None()));
        EXPECT_EQ(context.vm().none_type_class(),
                  thread->shape_of_value(Value::None())->get_class());

        TValue<String> dunder_class_name = context.vm().dunder_class_name();
        Shape *inline_shapes[] = {context.vm().smi_shape(),
                                  context.vm().bool_shape(),
                                  context.vm().none_shape()};
        for(Shape *shape: inline_shapes)
        {
            ASSERT_EQ(1u, shape->property_count());
            EXPECT_EQ(1u, shape->present_count());
            EXPECT_EQ(0, shape->get_next_slot_index());
            DescriptorLookup lookup =
                shape->lookup_descriptor_including_latent(dunder_class_name);
            ASSERT_TRUE(lookup.is_present());
            EXPECT_FALSE(lookup.storage_location().is_found());
            EXPECT_TRUE(lookup.info.has_flag(DescriptorFlag::ReadOnly));
            EXPECT_TRUE(lookup.info.has_flag(DescriptorFlag::StableSlot));
            EXPECT_TRUE(lookup.info.has_flag(DescriptorFlag::ShapeClassValue));
        }
    }

}  // namespace cl
