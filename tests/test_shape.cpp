#include "attr.h"
#include "class_object.h"
#include "instance.h"
#include "list.h"
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
    ASSERT_EQ(1u, root_shape->property_count());
    EXPECT_EQ(1u, root_shape->present_count());
    EXPECT_EQ(0u, root_shape->latent_count());
    EXPECT_FALSE(root_shape->has_flag(ShapeFlag::IsClassObject));
    EXPECT_STREQ(L"__class__",
                 root_shape->get_property_name(0).extract()->data);
    EXPECT_EQ(StorageKind::Inline,
              root_shape->get_property_storage_location(0).kind);
    EXPECT_EQ(0, root_shape->get_property_storage_location(0).physical_idx);
    EXPECT_TRUE(
        root_shape->get_descriptor_info(0).has_flag(DescriptorFlag::ReadOnly));
    EXPECT_TRUE(root_shape->get_descriptor_info(0).has_flag(
        DescriptorFlag::StableSlot));
    EXPECT_EQ(1, root_shape->get_next_slot_index());
    EXPECT_EQ(2u, root_shape->get_factory_default_inline_slot_count());
}

TEST(Shape, ShapeFlagsAreStoredOnShape)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(cls_name, 2);
    ShapeFlags flags = shape_flag(ShapeFlag::IsClassObject);
    flags |= shape_flag(ShapeFlag::IsImmutableType);

    Shape *shape = context.thread()->make_refcounted_raw<Shape>(
        Value::from_oop(cls), Value::None(), 0, 0, flags, 0);

    EXPECT_EQ(flags, shape->flags());
    EXPECT_TRUE(shape->has_flag(ShapeFlag::IsClassObject));
    EXPECT_TRUE(shape->has_flag(ShapeFlag::IsImmutableType));
    EXPECT_FALSE(shape->has_flag(ShapeFlag::HasCustomGetAttribute));
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

    ASSERT_EQ(2u, shape_with_a->property_count());
    EXPECT_EQ(2u, shape_with_a->present_count());
    EXPECT_EQ(0u, shape_with_a->latent_count());
    EXPECT_STREQ(L"__class__",
                 shape_with_a->get_property_name(0).extract()->data);
    EXPECT_STREQ(L"a", shape_with_a->get_property_name(1).extract()->data);
    EXPECT_EQ(StorageKind::Inline,
              shape_with_a->get_property_storage_location(1).kind);
    EXPECT_EQ(1, shape_with_a->get_property_storage_location(1).physical_idx);
    EXPECT_FALSE(shape_with_a->get_descriptor_info(1).has_flag(
        DescriptorFlag::ReadOnly));
    EXPECT_EQ(2, shape_with_a->get_next_slot_index());

    ASSERT_EQ(3u, shape_with_ab->property_count());
    EXPECT_EQ(3u, shape_with_ab->present_count());
    EXPECT_EQ(0u, shape_with_ab->latent_count());
    EXPECT_STREQ(L"__class__",
                 shape_with_ab->get_property_name(0).extract()->data);
    EXPECT_STREQ(L"a", shape_with_ab->get_property_name(1).extract()->data);
    EXPECT_STREQ(L"b", shape_with_ab->get_property_name(2).extract()->data);
    EXPECT_EQ(StorageKind::Overflow,
              shape_with_ab->get_property_storage_location(2).kind);
    EXPECT_EQ(0, shape_with_ab->get_property_storage_location(2).physical_idx);
    EXPECT_EQ(shape_with_a, shape_with_ab->get_previous_shape());
    EXPECT_EQ(3, shape_with_ab->get_next_slot_index());

    ASSERT_EQ(2u, shape_with_b->property_count());
    EXPECT_EQ(2u, shape_with_b->present_count());
    EXPECT_EQ(0u, shape_with_b->latent_count());
    EXPECT_STREQ(L"__class__",
                 shape_with_b->get_property_name(0).extract()->data);
    EXPECT_STREQ(L"b", shape_with_b->get_property_name(1).extract()->data);
    EXPECT_EQ(shape_with_ab, shape_with_b->get_previous_shape());
    EXPECT_EQ(3, shape_with_b->get_next_slot_index());
}

TEST(Shape, DescriptorLookupReportsPresentAndAbsentProperties)
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

    DescriptorLookup a_lookup =
        shape_with_a->lookup_descriptor_including_latent(a_name);
    EXPECT_EQ(DescriptorPresence::Present, a_lookup.presence);
    EXPECT_TRUE(a_lookup.is_present());
    EXPECT_FALSE(a_lookup.is_latent());
    EXPECT_EQ(1, a_lookup.descriptor_idx);
    EXPECT_TRUE(a_lookup.storage_location().is_found());

    DescriptorLookup b_lookup =
        shape_with_a->lookup_descriptor_including_latent(b_name);
    EXPECT_EQ(DescriptorPresence::Absent, b_lookup.presence);
    EXPECT_FALSE(b_lookup.is_present());
    EXPECT_FALSE(b_lookup.is_latent());
    EXPECT_EQ(-1, b_lookup.descriptor_idx);
    EXPECT_FALSE(b_lookup.storage_location().is_found());

    EXPECT_TRUE(shape_with_a->resolve_present_property(a_name).is_found());
    EXPECT_FALSE(shape_with_a->resolve_present_property(b_name).is_found());
}

TEST(Shape, DescriptorInfoPacksStorageAndFlags)
{
    StorageLocation location{17, StorageKind::Overflow};
    DescriptorFlags flags = descriptor_flag(DescriptorFlag::ReadOnly);
    flags |= descriptor_flag(DescriptorFlag::StableSlot);
    DescriptorInfo info = DescriptorInfo::make(location, flags);

    EXPECT_EQ(8u, sizeof(DescriptorInfo));
    EXPECT_EQ(17, info.physical_idx);
    EXPECT_EQ(StorageKind::Overflow, info.kind);
    EXPECT_EQ(0, info.reserved);
    EXPECT_TRUE(info.has_flag(DescriptorFlag::ReadOnly));
    EXPECT_TRUE(info.has_flag(DescriptorFlag::StableSlot));
    EXPECT_FALSE(info.has_flag(DescriptorFlag::None));
    EXPECT_EQ(17, info.storage_location().physical_idx);
    EXPECT_EQ(StorageKind::Overflow, info.storage_location().kind);
}

TEST(Shape, AddTransitionCanCarryDescriptorFlags)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> a_name(
        context.vm().get_or_create_interned_string_value(L"a"));
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(cls_name, 2);
    DescriptorFlags flags = descriptor_flag(DescriptorFlag::ReadOnly);
    flags |= descriptor_flag(DescriptorFlag::StableSlot);

    Shape *root_shape = cls->get_initial_shape();
    Shape *shape_with_a =
        root_shape->derive_transition(a_name, ShapeTransitionVerb::Add, flags);

    DescriptorLookup lookup =
        shape_with_a->lookup_descriptor_including_latent(a_name);
    ASSERT_TRUE(lookup.is_present());
    EXPECT_TRUE(lookup.info.has_flag(DescriptorFlag::ReadOnly));
    EXPECT_TRUE(lookup.info.has_flag(DescriptorFlag::StableSlot));
    EXPECT_EQ(shape_with_a, root_shape->lookup_transition(
                                a_name, ShapeTransitionVerb::Add, flags));
    EXPECT_EQ(nullptr,
              root_shape->lookup_transition(a_name, ShapeTransitionVerb::Add));
}

TEST(Shape, InstanceRejectsStoreToReadOnlyDescriptor)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> a_name(
        context.vm().get_or_create_interned_string_value(L"a"));
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(cls_name, 2);
    DescriptorFlags flags = descriptor_flag(DescriptorFlag::ReadOnly);

    Shape *shape_with_readonly = cls->get_initial_shape()->derive_transition(
        a_name, ShapeTransitionVerb::Add, flags);
    Instance *instance = context.thread()->make_refcounted_raw<Instance>(
        Value::from_oop(cls), Value::from_oop(shape_with_readonly));
    StorageLocation location =
        shape_with_readonly->resolve_present_property(a_name);
    ASSERT_TRUE(location.is_found());
    instance->write_storage_location(location, Value::from_smi(7));

    EXPECT_FALSE(instance->set_own_property(a_name, Value::from_smi(9)));
    EXPECT_EQ(Value::from_smi(7), instance->get_own_property(a_name));
    EXPECT_FALSE(
        store_attr(Value::from_oop(instance), a_name, Value::from_smi(9)));
    EXPECT_EQ(Value::from_smi(7), instance->get_own_property(a_name));

    EXPECT_FALSE(instance->delete_own_property(a_name));
    EXPECT_EQ(Value::from_smi(7), instance->get_own_property(a_name));
    EXPECT_EQ(shape_with_readonly, instance->get_shape());
}

TEST(Shape, StableSlotDeleteMovesDescriptorToLatentAndReAddReusesSlot)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> a_name(
        context.vm().get_or_create_interned_string_value(L"a"));
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(cls_name, 2);
    DescriptorFlags flags = descriptor_flag(DescriptorFlag::StableSlot);

    Shape *root_shape = cls->get_initial_shape();
    Shape *shape_with_a =
        root_shape->derive_transition(a_name, ShapeTransitionVerb::Add, flags);
    Shape *shape_without_a =
        shape_with_a->derive_transition(a_name, ShapeTransitionVerb::Delete);

    EXPECT_EQ(2u, shape_without_a->property_count());
    EXPECT_EQ(1u, shape_without_a->present_count());
    EXPECT_EQ(1u, shape_without_a->latent_count());
    DescriptorLookup latent_lookup =
        shape_without_a->lookup_descriptor_including_latent(a_name);
    EXPECT_TRUE(latent_lookup.is_latent());
    EXPECT_EQ(1, latent_lookup.info.physical_idx);
    EXPECT_EQ(2, shape_without_a->get_next_slot_index());
    EXPECT_FALSE(shape_without_a->resolve_present_property(a_name).is_found());

    Shape *shape_with_a_again =
        shape_without_a->derive_transition(a_name, ShapeTransitionVerb::Add);
    DescriptorLookup present_lookup =
        shape_with_a_again->lookup_descriptor_including_latent(a_name);
    EXPECT_TRUE(present_lookup.is_present());
    EXPECT_EQ(1, present_lookup.info.physical_idx);
    EXPECT_EQ(2u, shape_with_a_again->property_count());
    EXPECT_EQ(2u, shape_with_a_again->present_count());
    EXPECT_EQ(0u, shape_with_a_again->latent_count());
    EXPECT_EQ(2, shape_with_a_again->get_next_slot_index());
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

    ASSERT_EQ(3u, shape_with_ba->property_count());
    EXPECT_STREQ(L"__class__",
                 shape_with_ba->get_property_name(0).extract()->data);
    EXPECT_STREQ(L"b", shape_with_ba->get_property_name(1).extract()->data);
    EXPECT_STREQ(L"a", shape_with_ba->get_property_name(2).extract()->data);
    EXPECT_EQ(StorageKind::Overflow,
              shape_with_ba->get_property_storage_location(2).kind);
    EXPECT_EQ(2, shape_with_ba->get_property_storage_location(2).physical_idx);
    EXPECT_EQ(4, shape_with_ba->get_next_slot_index());
    EXPECT_EQ(1u, shape_with_ba->get_factory_default_inline_slot_count());
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

TEST(Shape, InstanceStoresDunderClassInPredefinedReadonlySlot)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> dunder_class_name(
        context.vm().get_or_create_interned_string_value(L"__class__"));
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(cls_name, 2);
    Instance *instance = context.thread()->make_refcounted_raw<Instance>(
        Value::from_oop(cls), Value::from_oop(cls->get_initial_shape()));

    Shape *shape = instance->get_shape();
    StorageLocation class_location =
        shape->resolve_present_property(dunder_class_name);
    ASSERT_TRUE(class_location.is_found());
    EXPECT_EQ(StorageKind::Inline, class_location.kind);
    EXPECT_EQ(0, class_location.physical_idx);
    EXPECT_TRUE(
        shape->get_descriptor_info(0).has_flag(DescriptorFlag::ReadOnly));
    EXPECT_TRUE(
        shape->get_descriptor_info(0).has_flag(DescriptorFlag::StableSlot));
    EXPECT_EQ(Value::from_oop(cls),
              instance->get_own_property(dunder_class_name));

    EXPECT_FALSE(
        instance->set_own_property(dunder_class_name, Value::from_oop(cls)));
    EXPECT_EQ(Value::from_oop(cls),
              instance->get_own_property(dunder_class_name));
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
    EXPECT_EQ(2u, instance->get_shape()->property_count());
    EXPECT_EQ(2, instance->get_shape()->get_next_slot_index());
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
    EXPECT_EQ(6u, overflow_slots->get_size());
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
    EXPECT_EQ(4, instance->get_shape()->get_next_slot_index());
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
    EXPECT_EQ(Value::from_smi(2), cls->get_member_value(0));
    EXPECT_EQ(Value::from_smi(3), cls->get_member_value(1));
}

TEST(ClassObject, MembersUseShapeBackedInlineAndOverflowStorage)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> names[] = {
        TValue<String>(context.vm().get_or_create_interned_string_value(L"a")),
        TValue<String>(context.vm().get_or_create_interned_string_value(L"b")),
        TValue<String>(context.vm().get_or_create_interned_string_value(L"c")),
        TValue<String>(context.vm().get_or_create_interned_string_value(L"d")),
        TValue<String>(context.vm().get_or_create_interned_string_value(L"e")),
    };
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(cls_name, 2);

    for(uint32_t idx = 0; idx < 5; ++idx)
    {
        cls->set_member(names[idx], Value::from_smi(idx + 1));
    }

    Shape *shape = cls->get_shape();
    ASSERT_EQ(9u, shape->property_count());
    EXPECT_EQ(9u, shape->present_count());
    EXPECT_EQ(9, shape->get_next_slot_index());
    EXPECT_EQ(5u, cls->member_count());
    EXPECT_EQ(Value::from_smi(1), cls->get_member_value(0));
    EXPECT_EQ(Value::from_smi(5), cls->get_member_value(4));

    StorageLocation first_location = shape->resolve_present_property(names[0]);
    ASSERT_TRUE(first_location.is_found());
    EXPECT_EQ(StorageKind::Inline, first_location.kind);
    EXPECT_EQ(4, first_location.physical_idx);
    EXPECT_EQ(Value::from_smi(1), cls->read_storage_location(first_location));

    StorageLocation last_location = shape->resolve_present_property(names[4]);
    ASSERT_TRUE(last_location.is_found());
    EXPECT_EQ(StorageKind::Overflow, last_location.kind);
    EXPECT_EQ(0, last_location.physical_idx);
    EXPECT_EQ(Value::from_smi(5), cls->read_storage_location(last_location));
}

TEST(ClassObject, PredefinedMetadataSlotsArePresentAndReadonly)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> other_name(
        context.vm().get_or_create_interned_string_value(L"Other"));
    TValue<String> dunder_class_name(
        context.vm().get_or_create_interned_string_value(L"__class__"));
    TValue<String> dunder_name_name(
        context.vm().get_or_create_interned_string_value(L"__name__"));
    TValue<String> dunder_bases_name(
        context.vm().get_or_create_interned_string_value(L"__bases__"));
    TValue<String> dunder_mro_name(
        context.vm().get_or_create_interned_string_value(L"__mro__"));
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(cls_name, 2);

    Shape *shape = cls->get_shape();
    ASSERT_NE(nullptr, shape);
    EXPECT_TRUE(shape->has_flag(ShapeFlag::IsClassObject));
    EXPECT_FALSE(shape->has_flag(ShapeFlag::IsImmutableType));
    ASSERT_EQ(4u, shape->property_count());
    EXPECT_EQ(4u, shape->present_count());
    EXPECT_EQ(0u, shape->latent_count());
    EXPECT_EQ(4, shape->get_next_slot_index());
    EXPECT_EQ(8u, shape->get_inline_slot_count());

    const cl_wchar *expected_names[] = {L"__class__", L"__name__", L"__bases__",
                                        L"__mro__"};
    for(uint32_t idx = 0; idx < shape->property_count(); ++idx)
    {
        EXPECT_STREQ(expected_names[idx],
                     shape->get_property_name(idx).extract()->data);
        EXPECT_EQ(StorageKind::Inline,
                  shape->get_property_storage_location(idx).kind);
        EXPECT_EQ(int32_t(idx),
                  shape->get_property_storage_location(idx).physical_idx);
        EXPECT_TRUE(
            shape->get_descriptor_info(idx).has_flag(DescriptorFlag::ReadOnly));
        EXPECT_TRUE(shape->get_descriptor_info(idx).has_flag(
            DescriptorFlag::StableSlot));
    }

    EXPECT_EQ(Value::None(), cls->get_own_property(dunder_class_name));
    EXPECT_EQ(cls_name.as_value(), cls->get_own_property(dunder_name_name));

    Value bases_value = cls->get_own_property(dunder_bases_name);
    ASSERT_TRUE(bases_value.is_ptr());
    ASSERT_EQ(&List::klass, bases_value.get_ptr<Object>()->klass);
    EXPECT_EQ(0u, bases_value.get_ptr<List>()->size());

    Value mro_value = cls->get_own_property(dunder_mro_name);
    ASSERT_TRUE(mro_value.is_ptr());
    ASSERT_EQ(&List::klass, mro_value.get_ptr<Object>()->klass);
    ASSERT_EQ(1u, mro_value.get_ptr<List>()->size());
    EXPECT_EQ(Value::from_oop(cls),
              mro_value.get_ptr<List>()->item_unchecked(0));

    EXPECT_FALSE(
        cls->set_own_property(dunder_name_name, other_name.as_value()));
    EXPECT_FALSE(cls->delete_own_property(dunder_name_name));
    EXPECT_EQ(cls_name.as_value(), cls->get_own_property(dunder_name_name));
}

TEST(ClassObject, PredefinedBasesAndMroReflectSingleBaseChain)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> base_name(
        context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> child_name(
        context.vm().get_or_create_interned_string_value(L"Child"));
    TValue<String> dunder_bases_name(
        context.vm().get_or_create_interned_string_value(L"__bases__"));
    TValue<String> dunder_mro_name(
        context.vm().get_or_create_interned_string_value(L"__mro__"));
    ClassObject *base =
        context.thread()->make_refcounted_raw<ClassObject>(base_name, 2);
    ClassObject *child = context.thread()->make_refcounted_raw<ClassObject>(
        child_name, 2, Value::from_oop(base));

    Value bases_value = child->get_own_property(dunder_bases_name);
    ASSERT_TRUE(bases_value.is_ptr());
    ASSERT_EQ(&List::klass, bases_value.get_ptr<Object>()->klass);
    ASSERT_EQ(1u, bases_value.get_ptr<List>()->size());
    EXPECT_EQ(Value::from_oop(base),
              bases_value.get_ptr<List>()->item_unchecked(0));

    Value mro_value = child->get_own_property(dunder_mro_name);
    ASSERT_TRUE(mro_value.is_ptr());
    ASSERT_EQ(&List::klass, mro_value.get_ptr<Object>()->klass);
    ASSERT_EQ(2u, mro_value.get_ptr<List>()->size());
    EXPECT_EQ(Value::from_oop(child),
              mro_value.get_ptr<List>()->item_unchecked(0));
    EXPECT_EQ(Value::from_oop(base),
              mro_value.get_ptr<List>()->item_unchecked(1));
}

TEST(ClassObject, OwnPropertyApiDoesNotFallBackToBaseChain)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> base_name(
        context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> child_name(
        context.vm().get_or_create_interned_string_value(L"Child"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"attr"));
    ClassObject *base =
        context.thread()->make_refcounted_raw<ClassObject>(base_name, 2);
    ClassObject *child = context.thread()->make_refcounted_raw<ClassObject>(
        child_name, 2, Value::from_oop(base));

    base->set_own_property(attr_name, Value::from_smi(7));

    EXPECT_EQ(Value::from_smi(7), base->get_own_property(attr_name));
    EXPECT_EQ(Value::not_present(), child->get_own_property(attr_name));
    EXPECT_EQ(Value::from_smi(7), child->get_member(attr_name));

    child->set_own_property(attr_name, Value::from_smi(8));
    EXPECT_EQ(Value::from_smi(8), child->get_own_property(attr_name));
    EXPECT_EQ(Value::from_smi(8), child->get_member(attr_name));

    EXPECT_TRUE(child->delete_own_property(attr_name));
    EXPECT_EQ(Value::not_present(), child->get_own_property(attr_name));
    EXPECT_EQ(Value::from_smi(7), child->get_member(attr_name));
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
