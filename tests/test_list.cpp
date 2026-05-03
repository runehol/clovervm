#include "exception_object.h"
#include "list.h"
#include "str.h"
#include "test_helpers.h"
#include "thread_state.h"
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

TEST(List, UncheckedOperationsSupportAppendSetInsertAndErase)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    List *list = context.thread()->make_object_raw<List>();

    String *first = make_string(context, L"first");
    String *second = make_string(context, L"second");
    String *middle = make_string(context, L"middle");

    list->append(Value::from_oop(first));
    list->append(Value::from_oop(second));
    list->insert_item_unchecked(1, Value::from_oop(middle));

    ASSERT_EQ(3u, list->size());
    EXPECT_EQ(Value::from_oop(first), list->item_unchecked(0));
    EXPECT_EQ(Value::from_oop(middle), list->item_unchecked(1));
    EXPECT_EQ(Value::from_oop(second), list->item_unchecked(2));

    list->set_item_unchecked(2, Value::from_smi(42));
    EXPECT_EQ(Value::from_smi(42), list->item_unchecked(2));

    Value removed = list->pop_item_unchecked(1);
    EXPECT_EQ(Value::from_oop(middle), removed);
    ASSERT_EQ(2u, list->size());
    EXPECT_EQ(Value::from_oop(first), list->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(42), list->item_unchecked(1));
}

TEST(List, SizedConstructorInitializesEntriesToNotPresent)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    List *list = context.thread()->make_object_raw<List>(3);

    ASSERT_EQ(3u, list->size());
    EXPECT_TRUE(list->item_unchecked(0).is_not_present());
    EXPECT_TRUE(list->item_unchecked(1).is_not_present());
    EXPECT_TRUE(list->item_unchecked(2).is_not_present());
}

TEST(List, CheckedIndexingSupportsNegativeIndices)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    List *list = context.thread()->make_object_raw<List>();

    list->append(Value::from_smi(10));
    list->append(Value::from_smi(20));
    list->append(Value::from_smi(30));

    EXPECT_EQ(Value::from_smi(30), list->get_item(-1));
    EXPECT_EQ(Value::from_smi(20), list->get_item(-2));

    EXPECT_EQ(Value::None(), list->set_item(-1, Value::from_smi(99)));
    EXPECT_EQ(Value::from_smi(99), list->get_item(2));
}

TEST(List, CheckedInsertClampsToPythonListSemantics)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    List *list = context.thread()->make_object_raw<List>();

    list->append(Value::from_smi(2));
    list->append(Value::from_smi(3));

    list->insert_item(-10, Value::from_smi(1));
    list->insert_item(100, Value::from_smi(4));

    ASSERT_EQ(4u, list->size());
    EXPECT_EQ(Value::from_smi(1), list->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(2), list->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(3), list->item_unchecked(2));
    EXPECT_EQ(Value::from_smi(4), list->item_unchecked(3));
}

TEST(List, PopItemSupportsDefaultAndNegativeIndices)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    List *list = context.thread()->make_object_raw<List>();

    String *first = make_string(context, L"first");
    String *second = make_string(context, L"second");
    String *third = make_string(context, L"third");
    list->append(Value::from_oop(first));
    list->append(Value::from_oop(second));
    list->append(Value::from_oop(third));

    Value last = list->pop_item();
    EXPECT_EQ(Value::from_oop(third), last);

    Value first_removed = list->pop_item(-2);
    EXPECT_EQ(Value::from_oop(first), first_removed);

    ASSERT_EQ(1u, list->size());
    EXPECT_EQ(Value::from_oop(second), list->item_unchecked(0));
}

TEST(List, CheckedIndexingRaisesIndexError)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    List *list = thread->make_object_raw<List>();

    list->append(Value::from_smi(1));

    EXPECT_TRUE(list->get_item(1).is_exception_marker());
    expect_pending_exception(thread, L"IndexError", L"list index out of range");
    thread->clear_pending_exception();

    EXPECT_TRUE(list->set_item(-2, Value::from_smi(0)).is_exception_marker());
    expect_pending_exception(thread, L"IndexError", L"list index out of range");
    thread->clear_pending_exception();

    EXPECT_TRUE(list->pop_item(3).is_exception_marker());
    expect_pending_exception(thread, L"IndexError", L"list index out of range");
}
