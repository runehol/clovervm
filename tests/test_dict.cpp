#include "dict.h"
#include "exception_object.h"
#include "owned.h"
#include "str.h"
#include "test_helpers.h"
#include "thread_state.h"
#include <gtest/gtest.h>

using namespace cl;

namespace
{
    static Value make_string(test::VmTestContext &context, const wchar_t *text)
    {
        return context.thread()->make_internal_value<String>(text);
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
                                     ->get_shape()
                                     ->get_class()
                                     ->get_name()
                                     .extract()
                                     ->data);
        EXPECT_STREQ(message, TValue<String>::from_value_checked(
                                  exception.extract()->message)
                                  .extract()
                                  ->data);
    }
}  // namespace

TEST(Dict, SetGetAndContainsWorkForStringKeys)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Dict *dict = context.thread()->make_object_raw<Dict>();

    Value alpha = make_string(context, L"alpha");
    Value beta = make_string(context, L"beta");

    dict->set_item(alpha, Value::from_smi(11));
    dict->set_item(beta, Value::from_smi(22));

    EXPECT_EQ(2u, dict->size());
    EXPECT_FALSE(dict->empty());
    EXPECT_TRUE(dict->contains(alpha));
    EXPECT_TRUE(dict->contains(beta));
    EXPECT_EQ(Value::from_smi(11), dict->get_item(alpha));
    EXPECT_EQ(Value::from_smi(22), dict->get_item(beta));
}

TEST(Dict, SetItemOverwritesExistingValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Dict *dict = context.thread()->make_object_raw<Dict>();

    Value key = make_string(context, L"shared");

    dict->set_item(key, Value::from_smi(1));
    dict->set_item(key, Value::from_smi(99));

    EXPECT_EQ(1u, dict->size());
    EXPECT_EQ(Value::from_smi(99), dict->get_item(key));
}

TEST(Dict, SetItemOverwriteEnqueuesOverwrittenObject)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Dict *dict = thread->make_object_raw<Dict>();
    Value key = make_string(context, L"shared");
    Value old_value = make_string(context, L"old-dict");
    Value new_value = make_string(context, L"new-dict");
    OwnedValue keep_dict(Value::from_oop(dict));
    OwnedValue keep_new(new_value);
    HeapObject *old_object = old_value.as.ptr;
    HeapObject *new_object = new_value.as.ptr;
    dict->set_item(key, old_value);
    thread->drain_zero_count_table_for_testing();
    ASSERT_FALSE(thread->zero_count_table_contains_for_testing(old_object));
    ASSERT_EQ(HeapLifecycleState::Normal, old_object->lifecycle_state);

    dict->set_item(key, new_value);

    EXPECT_EQ(new_value, dict->get_item(key));
    EXPECT_EQ(0, old_object->refcount);
    EXPECT_EQ(HeapLifecycleState::InZct, old_object->lifecycle_state);
    EXPECT_TRUE(thread->zero_count_table_contains_for_testing(old_object));
    EXPECT_FALSE(thread->zero_count_table_contains_for_testing(new_object));
}

TEST(Dict, DelItemRemovesKeyFromLookupAndLogicalSize)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Dict *dict = context.thread()->make_object_raw<Dict>();

    Value keep = make_string(context, L"keep");
    Value erase = make_string(context, L"erase");

    dict->set_item(keep, Value::from_smi(1));
    dict->set_item(erase, Value::from_smi(2));
    EXPECT_EQ(Value::None(), dict->del_item(erase));

    EXPECT_EQ(1u, dict->size());
    EXPECT_TRUE(dict->contains(keep));
    EXPECT_FALSE(dict->contains(erase));
    EXPECT_EQ(Value::from_smi(1), dict->get_item(keep));
    EXPECT_TRUE(dict->get_item(erase).is_exception_marker());
    expect_pending_exception(context.thread(), L"KeyError", L"");
}

TEST(Dict, CopyConstructorPreservesLiveEntriesOnly)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Dict *dict = context.thread()->make_object_raw<Dict>();

    Value first = make_string(context, L"first");
    Value second = make_string(context, L"second");

    dict->set_item(first, Value::from_smi(10));
    dict->set_item(second, Value::from_smi(20));
    EXPECT_EQ(Value::None(), dict->del_item(first));

    Dict copy(context.thread()->class_for_native_layout(Dict::native_layout_id),
              *dict);

    EXPECT_EQ(1u, copy.size());
    EXPECT_FALSE(copy.contains(first));
    EXPECT_TRUE(copy.contains(second));
    EXPECT_EQ(Value::from_smi(20), copy.get_item(second));
    EXPECT_TRUE(copy.get_item(first).is_exception_marker());
    expect_pending_exception(context.thread(), L"KeyError", L"");
}

TEST(Dict, IteratorVisitsLiveEntriesInInsertionOrder)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Dict *dict = context.thread()->make_object_raw<Dict>();

    Value first = make_string(context, L"first");
    Value second = make_string(context, L"second");
    Value third = make_string(context, L"third");
    Value fourth = make_string(context, L"fourth");

    dict->set_item(first, Value::from_smi(10));
    dict->set_item(second, Value::from_smi(20));
    dict->set_item(third, Value::from_smi(30));
    EXPECT_EQ(Value::None(), dict->del_item(first));
    EXPECT_EQ(Value::None(), dict->del_item(third));
    dict->set_item(fourth, Value::from_smi(40));

    Value expected_keys[] = {second, fourth};
    Value expected_values[] = {Value::from_smi(20), Value::from_smi(40)};
    size_t idx = 0;
    for(Dict::EntryView entry: *dict)
    {
        ASSERT_LT(idx, 2u);
        EXPECT_EQ(expected_keys[idx], entry.key);
        EXPECT_EQ(expected_values[idx], entry.value);
        ++idx;
    }
    EXPECT_EQ(2u, idx);
}

TEST(Dict, EmptyIteratorEqualsEnd)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Dict *dict = context.thread()->make_object_raw<Dict>();

    EXPECT_EQ(dict->begin(), dict->end());

    Value key = make_string(context, L"key");
    dict->set_item(key, Value::from_smi(1));
    EXPECT_EQ(Value::None(), dict->del_item(key));

    EXPECT_EQ(dict->begin(), dict->end());
}

TEST(Dict, ClearRemovesAllEntriesAndAllowsReuse)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Dict *dict = context.thread()->make_object_raw<Dict>();

    Value alpha = make_string(context, L"alpha");
    Value beta = make_string(context, L"beta");

    dict->set_item(alpha, Value::from_smi(1));
    dict->set_item(beta, Value::from_smi(2));
    dict->clear();

    EXPECT_EQ(0u, dict->size());
    EXPECT_TRUE(dict->empty());
    EXPECT_FALSE(dict->contains(alpha));
    EXPECT_TRUE(dict->get_item(alpha).is_exception_marker());
    expect_pending_exception(context.thread(), L"KeyError", L"");
    context.thread()->clear_pending_exception();

    dict->set_item(alpha, Value::from_smi(7));
    EXPECT_EQ(1u, dict->size());
    EXPECT_TRUE(dict->contains(alpha));
    EXPECT_EQ(Value::from_smi(7), dict->get_item(alpha));
}
