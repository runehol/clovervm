#include "builtin_types/str.h"
#include "compiler/scope.h"
#include "runtime/thread_state.h"
#include "test_helpers.h"
#include <gtest/gtest.h>

using namespace cl;

TEST(Scope, RegisterSlotIndexForWriteReusesNamedSlot)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Scope *scope = context.thread()->make_internal_raw<Scope>(nullptr);
    TValue<String> name(
        context.vm().get_or_create_interned_string_value(L"answer"));

    int32_t first_slot_idx = scope->register_slot_index_for_write(name);
    int32_t second_slot_idx = scope->register_slot_index_for_write(name);

    EXPECT_EQ(first_slot_idx, second_slot_idx);
    EXPECT_EQ(first_slot_idx, scope->lookup_slot_index_local(name));
    ASSERT_EQ(1u, scope->entry_count());
    EXPECT_EQ(first_slot_idx, scope->get_entry_slot_index(0));
    EXPECT_STREQ(L"answer", scope->get_entry_key(0).extract()->data);
}

TEST(Scope, RegisterSlotIndexForReadDoesNotTouchParentScope)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Scope *parent = context.thread()->make_internal_raw<Scope>(nullptr);
    Scope *child = context.thread()->make_internal_raw<Scope>(parent);
    TValue<String> name(
        context.vm().get_or_create_interned_string_value(L"tracked"));

    int32_t slot_idx = child->register_slot_index_for_read(name);

    EXPECT_EQ(0, slot_idx);
    EXPECT_EQ(-1, parent->lookup_slot_index_local(name));
    EXPECT_EQ(slot_idx, child->lookup_slot_index_local(name));
    ASSERT_EQ(1u, child->entry_count());
    EXPECT_EQ(slot_idx, child->get_entry_slot_index(0));
}

TEST(Scope, ReserveEmptySlotsDoNotCreateNamedEntries)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    Scope *scope = context.thread()->make_internal_raw<Scope>(nullptr);
    scope->reserve_empty_slots(3);

    EXPECT_EQ(3u, scope->size());
    EXPECT_TRUE(scope->slot_is_named(0) == false);
    EXPECT_TRUE(scope->slot_is_named(1) == false);
    EXPECT_TRUE(scope->slot_is_named(2) == false);
    EXPECT_EQ(0u, scope->entry_count());
}
