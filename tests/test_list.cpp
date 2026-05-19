#include "exception_object.h"
#include "list.h"
#include "owned.h"
#include "str.h"
#include "test_helpers.h"
#include "thread_state.h"
#include "value_state.h"
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
        TValue2<Exception> exception = thread->pending_exception_object();
        EXPECT_STREQ(class_name, exception.extract()
                                     ->get_shape()
                                     ->get_class()
                                     ->get_name()
                                     .extract()
                                     ->data);
        EXPECT_STREQ(message, exception.extract()->message.extract()->data);
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

TEST(List, ReserveTransfersBackingElementOwnership)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    List *list = context.thread()->make_object_raw<List>();
    String *item = make_string(context, L"kept");

    list->append(Value::from_oop(item));
    list->reserve(8);

    EXPECT_EQ(Value::from_oop(item), list->item_unchecked(0));
    EXPECT_EQ(1, item->refcount);
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

TEST(List, SetItemUncheckedEnqueuesOverwrittenObject)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    List *list = thread->make_object_raw<List>();
    String *old_string = make_string(context, L"old-list");
    String *new_string = make_string(context, L"new-list");
    OwnedValue keep_list(Value::from_oop(list));
    OwnedValue keep_new(Value::from_oop(new_string));
    list->append(Value::from_oop(old_string));
    ASSERT_FALSE(thread->zero_count_table_contains_for_testing(old_string));
    ASSERT_EQ(HeapLifecycleState::Normal, old_string->lifecycle_state);

    list->set_item_unchecked(0, Value::from_oop(new_string));

    EXPECT_EQ(Value::from_oop(new_string), list->item_unchecked(0));
    EXPECT_EQ(0, old_string->refcount);
    EXPECT_EQ(HeapLifecycleState::InZct, old_string->lifecycle_state);
    EXPECT_TRUE(thread->zero_count_table_contains_for_testing(old_string));
    EXPECT_FALSE(thread->zero_count_table_contains_for_testing(new_string));
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
