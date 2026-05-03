#include "dict.h"
#include "exception_object.h"
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
