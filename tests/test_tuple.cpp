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
    ThreadState::ActivationScope activation_scope(context.thread());
    Tuple *tuple =
        context.thread()->make_internal_raw<Tuple>(BootstrapObjectTag{}, 1);

    tuple->initialize_item_unchecked(0, Value::from_smi(1));

    try
    {
        (void)tuple->get_item(1);
        FAIL() << "Expected std::runtime_error";
    }
    catch(const std::runtime_error &err)
    {
        EXPECT_STREQ("IndexError: tuple index out of range", err.what());
    }

    try
    {
        (void)tuple->get_item(-2);
        FAIL() << "Expected std::runtime_error";
    }
    catch(const std::runtime_error &err)
    {
        EXPECT_STREQ("IndexError: tuple index out of range", err.what());
    }
}
