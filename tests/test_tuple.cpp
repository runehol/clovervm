#include "exception_object.h"
#include "str.h"
#include "test_helpers.h"
#include "thread_state.h"
#include "tuple.h"
#include <gtest/gtest.h>
#include <stdexcept>

using namespace cl;

namespace
{
    static String *make_string(test::VmTestContext &context,
                               const wchar_t *text)
    {
        return context.thread()->make_internal_raw<String>(text);
    }

    static void expect_pending_exception(ThreadState *thread,
                                         const wchar_t *class_name,
                                         const wchar_t *message)
    {
        ASSERT_EQ(PendingExceptionKind::Object,
                  thread->pending_exception_kind());
        TValue<ExceptionObject> exception =
            TValue<ExceptionObject>::from_value_checked(
                thread->pending_exception_object());
        EXPECT_STREQ(class_name, exception.extract()
                                     ->get_class()
                                     .extract()
                                     ->get_name()
                                     .extract()
                                     ->data);
        EXPECT_STREQ(message, TValue<String>::from_value_checked(
                                  exception.extract()->message)
                                  .extract()
                                  ->data);
    }
}  // namespace

TEST(Tuple, SizedConstructorInitializesEntriesToNotPresent)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Tuple *tuple =
        context.thread()->make_internal_raw<Tuple>(BootstrapObjectTag{}, 3);

    ASSERT_EQ(3u, tuple->size());
    EXPECT_TRUE(tuple->item_unchecked(0).is_not_present());
    EXPECT_TRUE(tuple->item_unchecked(1).is_not_present());
    EXPECT_TRUE(tuple->item_unchecked(2).is_not_present());
}

TEST(Tuple, ObjectAllocationUsesTupleClass)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Tuple *tuple = context.thread()->make_object_raw<Tuple>(2);

    ASSERT_NE(nullptr, context.vm().tuple_class());
    EXPECT_EQ(context.vm().tuple_class(), tuple->Object::get_class().extract());
    EXPECT_EQ(2u, tuple->size());
}

TEST(Tuple, EmptyTupleHasNoItems)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Tuple *tuple =
        context.thread()->make_internal_raw<Tuple>(BootstrapObjectTag{}, 0);

    EXPECT_TRUE(tuple->empty());
    EXPECT_EQ(0u, tuple->size());
}

TEST(Tuple, InitializeItemUncheckedStoresValues)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Tuple *tuple =
        context.thread()->make_internal_raw<Tuple>(BootstrapObjectTag{}, 2);

    String *first = make_string(context, L"first");
    String *second = make_string(context, L"second");

    tuple->initialize_item_unchecked(0, Value::from_oop(first));
    tuple->initialize_item_unchecked(1, Value::from_oop(second));

    EXPECT_EQ(Value::from_oop(first), tuple->item_unchecked(0));
    EXPECT_EQ(Value::from_oop(second), tuple->item_unchecked(1));
}

TEST(Tuple, CheckedIndexingSupportsNegativeIndices)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Tuple *tuple =
        context.thread()->make_internal_raw<Tuple>(BootstrapObjectTag{}, 3);

    tuple->initialize_item_unchecked(0, Value::from_smi(10));
    tuple->initialize_item_unchecked(1, Value::from_smi(20));
    tuple->initialize_item_unchecked(2, Value::from_smi(30));

    EXPECT_EQ(Value::from_smi(30), tuple->get_item(-1));
    EXPECT_EQ(Value::from_smi(20), tuple->get_item(-2));
}

TEST(Tuple, CheckedIndexingRaisesIndexError)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Tuple *tuple = thread->make_internal_raw<Tuple>(BootstrapObjectTag{}, 1);

    tuple->initialize_item_unchecked(0, Value::from_smi(1));

    EXPECT_TRUE(tuple->get_item(1).is_exception_marker());
    expect_pending_exception(thread, L"IndexError",
                             L"tuple index out of range");
    thread->clear_pending_exception();

    EXPECT_TRUE(tuple->get_item(-2).is_exception_marker());
    expect_pending_exception(thread, L"IndexError",
                             L"tuple index out of range");
}
