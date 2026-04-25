#include "scope.h"
#include "test_helpers.h"
#include "thread_state.h"
#include <gtest/gtest.h>

using namespace cl;

TEST(Scope, ReinsertByNameReusesSlotAndAppendsEntry)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Scope *scope = context.thread()->make_refcounted_raw<Scope>(nullptr);
    TValue<String> name(
        context.vm().get_or_create_interned_string_value(L"answer"));

    scope->set_by_name(name, Value::from_smi(1));

    ASSERT_EQ(1u, scope->entry_count());
    int32_t slot_idx = scope->lookup_slot_index_local(name);
    ASSERT_EQ(0, slot_idx);
    EXPECT_EQ(0, scope->get_entry_slot_index(0));

    scope->set_by_slot_index(slot_idx, Value::not_present());
    scope->set_by_name(name, Value::from_smi(2));

    EXPECT_EQ(slot_idx, scope->lookup_slot_index_local(name));
    EXPECT_EQ(Value::from_smi(2), scope->get_by_name(name));
    ASSERT_EQ(2u, scope->entry_count());
    EXPECT_EQ(-1, scope->get_entry_slot_index(0));
    EXPECT_EQ(slot_idx, scope->get_entry_slot_index(1));
    EXPECT_STREQ(L"answer", scope->get_entry_key(1).extract()->data);
}

TEST(Scope, ReadTrackingSlotStartsWithoutEntryUntilBound)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Scope *parent = context.thread()->make_refcounted_raw<Scope>(nullptr);
    Scope *child = context.thread()->make_refcounted_raw<Scope>(parent);
    TValue<String> name(
        context.vm().get_or_create_interned_string_value(L"tracked"));

    parent->set_by_name(name, Value::from_smi(7));

    int32_t slot_idx = child->register_slot_index_for_read(name);
    EXPECT_EQ(0, slot_idx);
    EXPECT_EQ(0u, child->entry_count());
    EXPECT_EQ(Value::from_smi(7), child->get_by_name(name));

    child->set_by_name(name, Value::from_smi(11));

    ASSERT_EQ(1u, child->entry_count());
    EXPECT_EQ(slot_idx, child->get_entry_slot_index(0));
    EXPECT_EQ(Value::from_smi(11), child->get_by_name(name));
}

TEST(Scope, DeletedChildSlotFallsBackToParentByCurrentEntryName)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Scope *parent = context.thread()->make_refcounted_raw<Scope>(nullptr);
    Scope *child = context.thread()->make_refcounted_raw<Scope>(parent);
    TValue<String> name(
        context.vm().get_or_create_interned_string_value(L"shadowed"));

    parent->set_by_name(name, Value::from_smi(7));

    int32_t slot_idx = child->register_slot_index_for_read(name);
    EXPECT_EQ(Value::from_smi(7), child->get_by_slot_index(slot_idx));

    child->set_by_name(name, Value::from_smi(11));
    ASSERT_EQ(1u, child->entry_count());
    EXPECT_EQ(slot_idx, child->get_entry_slot_index(0));

    child->set_by_slot_index(slot_idx, Value::not_present());

    EXPECT_EQ(Value::from_smi(7), child->get_by_name(name));
}
