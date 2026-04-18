#include "shape.h"
#include "test_helpers.h"
#include "thread_state.h"
#include <gtest/gtest.h>

using namespace cl;

TEST(Shape, ClassOwnsRootShape)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(name, 2);

    Shape *root_shape = cls->get_initial_shape();
    ASSERT_NE(nullptr, root_shape);
    EXPECT_EQ(cls, root_shape->get_owner_class());
    EXPECT_EQ(nullptr, root_shape->get_previous_shape());
    EXPECT_EQ(0u, root_shape->property_count());
    EXPECT_EQ(0u, root_shape->get_next_physical_slot());
    EXPECT_EQ(2u, root_shape->get_inline_slot_capacity());
}

TEST(Shape, AddAndDeleteTransitionsAreCached)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> a_name(
        context.vm().get_or_create_interned_string_value(L"a"));
    TValue<String> b_name(
        context.vm().get_or_create_interned_string_value(L"b"));
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(cls_name, 2);

    Shape *root_shape = cls->get_initial_shape();
    Shape *shape_with_a =
        root_shape->derive_transition(a_name, ShapeTransitionVerb::Add);
    Shape *shape_with_a_again =
        root_shape->derive_transition(a_name, ShapeTransitionVerb::Add);
    Shape *shape_with_ab =
        shape_with_a->derive_transition(b_name, ShapeTransitionVerb::Add);
    Shape *shape_with_b =
        shape_with_ab->derive_transition(a_name, ShapeTransitionVerb::Delete);
    Shape *shape_with_b_again =
        shape_with_ab->derive_transition(a_name, ShapeTransitionVerb::Delete);

    EXPECT_EQ(shape_with_a, shape_with_a_again);
    EXPECT_EQ(shape_with_b, shape_with_b_again);

    ASSERT_EQ(1u, shape_with_a->property_count());
    EXPECT_STREQ(L"a", shape_with_a->get_property_name(0).extract()->data);
    EXPECT_EQ(0u, shape_with_a->get_property_physical_slot_index(0));
    EXPECT_EQ(1u, shape_with_a->get_next_physical_slot());

    ASSERT_EQ(2u, shape_with_ab->property_count());
    EXPECT_STREQ(L"a", shape_with_ab->get_property_name(0).extract()->data);
    EXPECT_STREQ(L"b", shape_with_ab->get_property_name(1).extract()->data);
    EXPECT_EQ(0u, shape_with_ab->get_property_physical_slot_index(0));
    EXPECT_EQ(1u, shape_with_ab->get_property_physical_slot_index(1));
    EXPECT_EQ(shape_with_a, shape_with_ab->get_previous_shape());
    EXPECT_EQ(2u, shape_with_ab->get_next_physical_slot());

    ASSERT_EQ(1u, shape_with_b->property_count());
    EXPECT_STREQ(L"b", shape_with_b->get_property_name(0).extract()->data);
    EXPECT_EQ(1u, shape_with_b->get_property_physical_slot_index(0));
    EXPECT_EQ(shape_with_ab, shape_with_b->get_previous_shape());
    EXPECT_EQ(2u, shape_with_b->get_next_physical_slot());
}

TEST(Shape, ReAddAfterDeleteAppendsAndAllocatesFreshPhysicalSlot)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> a_name(
        context.vm().get_or_create_interned_string_value(L"a"));
    TValue<String> b_name(
        context.vm().get_or_create_interned_string_value(L"b"));
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(cls_name, 1);

    Shape *root_shape = cls->get_initial_shape();
    Shape *shape_with_a =
        root_shape->derive_transition(a_name, ShapeTransitionVerb::Add);
    Shape *shape_with_ab =
        shape_with_a->derive_transition(b_name, ShapeTransitionVerb::Add);
    Shape *shape_with_b =
        shape_with_ab->derive_transition(a_name, ShapeTransitionVerb::Delete);
    Shape *shape_with_ba =
        shape_with_b->derive_transition(a_name, ShapeTransitionVerb::Add);

    ASSERT_EQ(2u, shape_with_ba->property_count());
    EXPECT_STREQ(L"b", shape_with_ba->get_property_name(0).extract()->data);
    EXPECT_STREQ(L"a", shape_with_ba->get_property_name(1).extract()->data);
    EXPECT_EQ(1u, shape_with_ba->get_property_physical_slot_index(0));
    EXPECT_EQ(2u, shape_with_ba->get_property_physical_slot_index(1));
    EXPECT_EQ(3u, shape_with_ba->get_next_physical_slot());
    EXPECT_EQ(1u, shape_with_ba->get_inline_slot_capacity());
}

TEST(Shape, InstanceStoresClassAndShapeSeparately)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(cls_name, 2);
    Instance *instance = context.thread()->make_refcounted_raw<Instance>(
        Value::from_oop(cls), Value::from_oop(cls->get_initial_shape()));

    EXPECT_EQ(Value::from_oop(cls), instance->get_class());
    EXPECT_EQ(cls->get_initial_shape(), instance->get_shape());
}

TEST(Shape, InstanceStoresAndLoadsInlineOwnProperty)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> a_name(
        context.vm().get_or_create_interned_string_value(L"a"));
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(cls_name, 2);
    Instance *instance = context.thread()->make_refcounted_raw<Instance>(
        Value::from_oop(cls), Value::from_oop(cls->get_initial_shape()));

    instance->set_own_property(a_name, Value::from_smi(7));

    EXPECT_EQ(Value::from_smi(7), instance->get_own_property(a_name));
    EXPECT_EQ(1u, instance->get_shape()->property_count());
    EXPECT_EQ(1u, instance->get_shape()->get_next_physical_slot());
    EXPECT_EQ(nullptr, instance->get_overflow_slots());
}

TEST(Shape, InstanceSpillsIntoGeometricallyGrowingOverflowStorage)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> a_name(
        context.vm().get_or_create_interned_string_value(L"a"));
    TValue<String> b_name(
        context.vm().get_or_create_interned_string_value(L"b"));
    TValue<String> c_name(
        context.vm().get_or_create_interned_string_value(L"c"));
    TValue<String> d_name(
        context.vm().get_or_create_interned_string_value(L"d"));
    TValue<String> e_name(
        context.vm().get_or_create_interned_string_value(L"e"));
    TValue<String> f_name(
        context.vm().get_or_create_interned_string_value(L"f"));
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(cls_name, 1);
    Instance *instance = context.thread()->make_refcounted_raw<Instance>(
        Value::from_oop(cls), Value::from_oop(cls->get_initial_shape()));

    instance->set_own_property(a_name, Value::from_smi(1));
    instance->set_own_property(b_name, Value::from_smi(2));
    instance->set_own_property(c_name, Value::from_smi(3));
    instance->set_own_property(d_name, Value::from_smi(4));
    instance->set_own_property(e_name, Value::from_smi(5));
    instance->set_own_property(f_name, Value::from_smi(6));

    OverflowSlots *overflow_slots = instance->get_overflow_slots();
    ASSERT_NE(nullptr, overflow_slots);
    EXPECT_EQ(5u, overflow_slots->get_size());
    EXPECT_EQ(8u, overflow_slots->get_capacity());
    EXPECT_EQ(Value::from_smi(1), instance->get_own_property(a_name));
    EXPECT_EQ(Value::from_smi(2), instance->get_own_property(b_name));
    EXPECT_EQ(Value::from_smi(6), instance->get_own_property(f_name));
}

TEST(Shape, DeleteClearsSlotAndAllowsFreshReAdd)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> a_name(
        context.vm().get_or_create_interned_string_value(L"a"));
    TValue<String> b_name(
        context.vm().get_or_create_interned_string_value(L"b"));
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(cls_name, 1);
    Instance *instance = context.thread()->make_refcounted_raw<Instance>(
        Value::from_oop(cls), Value::from_oop(cls->get_initial_shape()));

    instance->set_own_property(a_name, Value::from_smi(10));
    instance->set_own_property(b_name, Value::from_smi(11));

    EXPECT_TRUE(instance->delete_own_property(a_name));
    EXPECT_EQ(Value::not_present(), instance->get_own_property(a_name));
    EXPECT_EQ(Value::from_smi(11), instance->get_own_property(b_name));

    instance->set_own_property(a_name, Value::from_smi(12));
    EXPECT_EQ(Value::from_smi(12), instance->get_own_property(a_name));
    EXPECT_EQ(3u, instance->get_shape()->get_next_physical_slot());
}

TEST(Shape, TwoInstancesShareShapeTransitionsButHoldDistinctValues)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> a_name(
        context.vm().get_or_create_interned_string_value(L"a"));
    TValue<String> b_name(
        context.vm().get_or_create_interned_string_value(L"b"));
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(cls_name, 1);
    Instance *first = context.thread()->make_refcounted_raw<Instance>(
        Value::from_oop(cls), Value::from_oop(cls->get_initial_shape()));
    Instance *second = context.thread()->make_refcounted_raw<Instance>(
        Value::from_oop(cls), Value::from_oop(cls->get_initial_shape()));

    first->set_own_property(a_name, Value::from_smi(1));
    first->set_own_property(b_name, Value::from_smi(2));
    second->set_own_property(a_name, Value::from_smi(10));
    second->set_own_property(b_name, Value::from_smi(20));

    EXPECT_EQ(first->get_shape(), second->get_shape());
    EXPECT_EQ(Value::from_smi(1), first->get_own_property(a_name));
    EXPECT_EQ(Value::from_smi(2), first->get_own_property(b_name));
    EXPECT_EQ(Value::from_smi(10), second->get_own_property(a_name));
    EXPECT_EQ(Value::from_smi(20), second->get_own_property(b_name));
}
