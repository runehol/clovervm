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
