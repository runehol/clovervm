#include "builtin_function.h"
#include "class_object.h"
#include "function.h"
#include "instance.h"
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
    EXPECT_EQ(0, root_shape->get_next_slot_index());
    EXPECT_EQ(2u, root_shape->get_instance_inline_slot_count());
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
    EXPECT_EQ(StorageKind::Inline,
              shape_with_a->get_property_storage_location(0).kind);
    EXPECT_EQ(0, shape_with_a->get_property_storage_location(0).physical_idx);
    EXPECT_EQ(1, shape_with_a->get_next_slot_index());

    ASSERT_EQ(2u, shape_with_ab->property_count());
    EXPECT_STREQ(L"a", shape_with_ab->get_property_name(0).extract()->data);
    EXPECT_STREQ(L"b", shape_with_ab->get_property_name(1).extract()->data);
    EXPECT_EQ(StorageKind::Inline,
              shape_with_ab->get_property_storage_location(1).kind);
    EXPECT_EQ(1, shape_with_ab->get_property_storage_location(1).physical_idx);
    EXPECT_EQ(shape_with_a, shape_with_ab->get_previous_shape());
    EXPECT_EQ(2, shape_with_ab->get_next_slot_index());

    ASSERT_EQ(1u, shape_with_b->property_count());
    EXPECT_STREQ(L"b", shape_with_b->get_property_name(0).extract()->data);
    EXPECT_EQ(shape_with_ab, shape_with_b->get_previous_shape());
    EXPECT_EQ(2, shape_with_b->get_next_slot_index());
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
    EXPECT_EQ(StorageKind::Overflow,
              shape_with_ba->get_property_storage_location(1).kind);
    EXPECT_EQ(1, shape_with_ba->get_property_storage_location(1).physical_idx);
    EXPECT_EQ(3, shape_with_ba->get_next_slot_index());
    EXPECT_EQ(1u, shape_with_ba->get_instance_inline_slot_count());
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
    EXPECT_EQ(1, instance->get_shape()->get_next_slot_index());
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

    Instance::OverflowSlots *overflow_slots = instance->get_overflow_slots();
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
    EXPECT_EQ(3, instance->get_shape()->get_next_slot_index());
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

TEST(ClassObject, MembersPreserveInsertionOrderAndCompactOnDelete)
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

    cls->set_member(a_name, Value::from_smi(1));
    cls->set_member(b_name, Value::from_smi(2));

    ASSERT_EQ(2u, cls->member_count());
    EXPECT_STREQ(L"a", cls->get_member_name(0).extract()->data);
    EXPECT_STREQ(L"b", cls->get_member_name(1).extract()->data);

    EXPECT_TRUE(cls->delete_member(a_name));

    ASSERT_EQ(1u, cls->member_count());
    EXPECT_STREQ(L"b", cls->get_member_name(0).extract()->data);

    cls->set_member(a_name, Value::from_smi(3));

    ASSERT_EQ(2u, cls->member_count());
    EXPECT_STREQ(L"b", cls->get_member_name(0).extract()->data);
    EXPECT_STREQ(L"a", cls->get_member_name(1).extract()->data);
}

TEST(ClassObject, MemberLookupFallsBackToBaseChain)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> base_name(
        context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> child_name(
        context.vm().get_or_create_interned_string_value(L"Child"));
    TValue<String> method_name(
        context.vm().get_or_create_interned_string_value(L"m"));
    ClassObject *base =
        context.thread()->make_refcounted_raw<ClassObject>(base_name, 2);
    ClassObject *child = context.thread()->make_refcounted_raw<ClassObject>(
        child_name, 2, Value::from_oop(base));

    base->set_member(method_name, Value::from_smi(7));

    EXPECT_EQ(Value::from_smi(7), child->get_member(method_name));
}

TEST(ClassObject, MethodVersionTracksOnlyMethodShapeChanges)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> data_name(
        context.vm().get_or_create_interned_string_value(L"data"));
    TValue<String> method_name(
        context.vm().get_or_create_interned_string_value(L"method"));
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(cls_name, 2);
    CodeObject *code_obj = context.compile_file(L"def f():\n    return 1\n");
    Function *fun1 = context.thread()->make_refcounted_raw<Function>(
        TValue<CodeObject>::from_oop(code_obj));
    Function *fun2 = context.thread()->make_refcounted_raw<Function>(
        TValue<CodeObject>::from_oop(code_obj));

    EXPECT_EQ(0u, cls->get_method_version());

    cls->set_member(data_name, Value::from_smi(1));
    EXPECT_EQ(0u, cls->get_method_version());

    cls->set_member(data_name, Value::from_smi(2));
    EXPECT_EQ(0u, cls->get_method_version());

    cls->set_member(method_name, Value::from_oop(fun1));
    EXPECT_EQ(1u, cls->get_method_version());

    cls->set_member(method_name, Value::from_oop(fun2));
    EXPECT_EQ(2u, cls->get_method_version());

    cls->set_member(method_name, Value::from_oop(fun2));
    EXPECT_EQ(2u, cls->get_method_version());

    cls->set_member(method_name, Value::from_smi(3));
    EXPECT_EQ(3u, cls->get_method_version());

    cls->delete_member(data_name);
    EXPECT_EQ(3u, cls->get_method_version());

    cls->set_member(method_name, Value::from_oop(fun1));
    EXPECT_EQ(4u, cls->get_method_version());

    cls->delete_member(method_name);
    EXPECT_EQ(5u, cls->get_method_version());
}
