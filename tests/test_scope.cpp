#include "owned.h"
#include "refcount.h"
#include "scope.h"
#include "str.h"
#include "test_helpers.h"
#include "thread_state.h"
#include <gtest/gtest.h>

using namespace cl;

TEST(Scope, ReinsertByNameReusesSlotAndAppendsEntry)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Scope *scope = context.thread()->make_internal_raw<Scope>(nullptr);
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

    Scope *parent = context.thread()->make_internal_raw<Scope>(nullptr);
    Scope *child = context.thread()->make_internal_raw<Scope>(parent);
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

    Scope *parent = context.thread()->make_internal_raw<Scope>(nullptr);
    Scope *child = context.thread()->make_internal_raw<Scope>(parent);
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

TEST(Scope, SetBySlotIndexEnqueuesOverwrittenObject)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    Scope *scope = thread->make_internal_raw<Scope>(nullptr);
    incref_heap_ptr(scope);
    TValue<String> name(
        context.vm().get_or_create_interned_string_value(L"slot"));
    String *old_string = thread->make_object_raw<String>(L"old-scope");
    String *new_string = thread->make_object_raw<String>(L"new-scope");
    OwnedValue keep_new(Value::from_oop(new_string));
    int32_t slot_idx = scope->register_slot_index_for_write(name);
    scope->set_by_slot_index(slot_idx, Value::from_oop(old_string));
    thread->drain_zero_count_table_for_testing();
    ASSERT_FALSE(thread->zero_count_table_contains_for_testing(old_string));
    ASSERT_EQ(HeapLifecycleState::Normal, old_string->lifecycle_state);

    scope->set_by_slot_index(slot_idx, Value::from_oop(new_string));

    EXPECT_EQ(Value::from_oop(new_string), scope->get_by_slot_index(slot_idx));
    EXPECT_EQ(0, old_string->refcount);
    EXPECT_EQ(HeapLifecycleState::InZct, old_string->lifecycle_state);
    EXPECT_TRUE(thread->zero_count_table_contains_for_testing(old_string));
    EXPECT_FALSE(thread->zero_count_table_contains_for_testing(new_string));
    decref_heap_ptr(scope);
}
