#include "attr.h"
#include "class_object.h"
#include "instance.h"
#include "list.h"
#include "shape.h"
#include "test_helpers.h"
#include "thread_state.h"
#include "tuple.h"
#include <cassert>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace cl;

static uint32_t class_property_count(ClassObject *cls)
{
    Shape *shape = cls->get_shape();
    assert(shape->present_count() >= ClassObject::kClassMetadataSlotCount);
    return shape->present_count() - ClassObject::kClassMetadataSlotCount;
}

static TValue<String> class_property_name(ClassObject *cls, uint32_t idx)
{
    return cls->get_shape()->get_property_name(
        ClassObject::kClassMetadataSlotCount + idx);
}

static Value class_property_value(ClassObject *cls, uint32_t idx)
{
    DescriptorInfo info = cls->get_shape()->get_descriptor_info(
        ClassObject::kClassMetadataSlotCount + idx);
    return cls->read_storage_location(info.storage_location());
}

TEST(Shape, ClassOwnsRootShape)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        name, 2, context.vm().object_class());

    Shape *root_shape = cls->get_instance_root_shape();
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
    EXPECT_EQ(2u, root_shape->get_instance_default_inline_slot_count());
}

TEST(Shape, ShapeFlagsAreStoredOnShape)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());
    ShapeFlags flags = shape_flag(ShapeFlag::IsClassObject);
    flags |= shape_flag(ShapeFlag::IsImmutableType);

    Shape *shape = context.thread()->make_internal_raw<Shape>(
        Value::from_oop(cls), nullptr, 0, 0, flags, 0);

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
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());

    Shape *root_shape = cls->get_instance_root_shape();
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
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());

    Shape *root_shape = cls->get_instance_root_shape();
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
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());
    DescriptorFlags flags = descriptor_flag(DescriptorFlag::ReadOnly);
    flags |= descriptor_flag(DescriptorFlag::StableSlot);

    Shape *root_shape = cls->get_instance_root_shape();
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
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());
    DescriptorFlags flags = descriptor_flag(DescriptorFlag::ReadOnly);

    Shape *shape_with_readonly =
        cls->get_instance_root_shape()->derive_transition(
            a_name, ShapeTransitionVerb::Add, flags);
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);
    instance->set_shape(shape_with_readonly);
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
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());
    DescriptorFlags flags = descriptor_flag(DescriptorFlag::StableSlot);

    Shape *root_shape = cls->get_instance_root_shape();
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
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 1, context.vm().object_class());

    Shape *root_shape = cls->get_instance_root_shape();
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
    EXPECT_EQ(1u, shape_with_ba->get_instance_default_inline_slot_count());
}

TEST(Shape, InstanceStoresClassAndShapeSeparately)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

    EXPECT_EQ(cls, instance->get_class().extract());
    EXPECT_EQ(cls->get_instance_root_shape(), instance->get_shape());
}

TEST(Shape, InstanceStoresDunderClassInPredefinedReadonlySlot)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> dunder_class_name(
        context.vm().get_or_create_interned_string_value(L"__class__"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

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
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

    instance->set_own_property(a_name, Value::from_smi(7));

    EXPECT_EQ(Value::from_smi(7), instance->get_own_property(a_name));
    EXPECT_EQ(2u, instance->get_shape()->property_count());
    EXPECT_EQ(2, instance->get_shape()->get_next_slot_index());
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
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 1, context.vm().object_class());
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

    instance->set_own_property(a_name, Value::from_smi(1));
    instance->set_own_property(b_name, Value::from_smi(2));
    instance->set_own_property(c_name, Value::from_smi(3));
    instance->set_own_property(d_name, Value::from_smi(4));
    instance->set_own_property(e_name, Value::from_smi(5));
    instance->set_own_property(f_name, Value::from_smi(6));

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
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 1, context.vm().object_class());
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

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
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 1, context.vm().object_class());
    Instance *first = context.thread()->make_internal_raw<Instance>(cls);
    Instance *second = context.thread()->make_internal_raw<Instance>(cls);

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

TEST(ClassObject, ClassPropertiesPreserveInsertionOrderAndCompactOnDelete)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> a_name(
        context.vm().get_or_create_interned_string_value(L"a"));
    TValue<String> b_name(
        context.vm().get_or_create_interned_string_value(L"b"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());

    cls->set_own_property(a_name, Value::from_smi(1));
    cls->set_own_property(b_name, Value::from_smi(2));

    ASSERT_EQ(2u, class_property_count(cls));
    EXPECT_STREQ(L"a", class_property_name(cls, 0).extract()->data);
    EXPECT_STREQ(L"b", class_property_name(cls, 1).extract()->data);

    EXPECT_TRUE(cls->delete_own_property(a_name));

    ASSERT_EQ(1u, class_property_count(cls));
    EXPECT_STREQ(L"b", class_property_name(cls, 0).extract()->data);

    cls->set_own_property(a_name, Value::from_smi(3));

    ASSERT_EQ(2u, class_property_count(cls));
    EXPECT_STREQ(L"b", class_property_name(cls, 0).extract()->data);
    EXPECT_STREQ(L"a", class_property_name(cls, 1).extract()->data);
    EXPECT_EQ(Value::from_smi(2), class_property_value(cls, 0));
    EXPECT_EQ(Value::from_smi(3), class_property_value(cls, 1));
}

TEST(ClassObject, ClassPropertiesUseShapeBackedInlineAndOverflowStorage)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    std::vector<TValue<String>> names;
    constexpr uint32_t property_count =
        ClassObject::kClassInlineStorageSlotCount -
        ClassObject::kClassPredefinedSlotCount + 1;
    for(uint32_t idx = 0; idx < property_count; ++idx)
    {
        std::wstring name = L"p" + std::to_wstring(idx);
        names.push_back(context.vm().get_or_create_interned_string_value(name));
    }
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());

    for(uint32_t idx = 0; idx < property_count; ++idx)
    {
        cls->set_own_property(names[idx], Value::from_smi(idx + 1));
    }

    Shape *shape = cls->get_shape();
    ASSERT_EQ(ClassObject::kClassInlineStorageSlotCount + 1,
              shape->property_count());
    EXPECT_EQ(ClassObject::kClassMetadataSlotCount + property_count,
              shape->present_count());
    EXPECT_EQ(ClassObject::kClassPredefinedSlotCount -
                  ClassObject::kClassMetadataSlotCount,
              shape->latent_count());
    EXPECT_EQ(int32_t(ClassObject::kClassInlineStorageSlotCount + 1),
              shape->get_next_slot_index());
    EXPECT_EQ(property_count, class_property_count(cls));
    EXPECT_EQ(Value::from_smi(1), class_property_value(cls, 0));
    EXPECT_EQ(Value::from_smi(property_count),
              class_property_value(cls, property_count - 1));

    StorageLocation first_location = shape->resolve_present_property(names[0]);
    ASSERT_TRUE(first_location.is_found());
    EXPECT_EQ(StorageKind::Inline, first_location.kind);
    EXPECT_EQ(int32_t(ClassObject::kClassPredefinedSlotCount),
              first_location.physical_idx);
    EXPECT_EQ(Value::from_smi(1), cls->read_storage_location(first_location));

    StorageLocation last_location =
        shape->resolve_present_property(names[property_count - 1]);
    ASSERT_TRUE(last_location.is_found());
    EXPECT_EQ(StorageKind::Overflow, last_location.kind);
    EXPECT_EQ(0, last_location.physical_idx);
    EXPECT_EQ(Value::from_smi(property_count),
              cls->read_storage_location(last_location));
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
    TValue<String> dunder_new_name(
        context.vm().get_or_create_interned_string_value(L"__new__"));
    TValue<String> dunder_init_name(
        context.vm().get_or_create_interned_string_value(L"__init__"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());

    Shape *shape = cls->get_shape();
    ASSERT_NE(nullptr, shape);
    EXPECT_TRUE(shape->has_flag(ShapeFlag::IsClassObject));
    EXPECT_FALSE(shape->has_flag(ShapeFlag::IsImmutableType));
    ASSERT_EQ(6u, shape->property_count());
    EXPECT_EQ(4u, shape->present_count());
    EXPECT_EQ(2u, shape->latent_count());
    EXPECT_EQ(6, shape->get_next_slot_index());
    EXPECT_EQ(ClassObject::kClassInlineStorageSlotCount,
              shape->get_inline_slot_count());

    const cl_wchar *expected_names[] = {L"__class__", L"__name__", L"__bases__",
                                        L"__mro__"};
    const uint32_t expected_slots[] = {ClassObject::kClassMetadataSlotClass,
                                       ClassObject::kClassMetadataSlotName,
                                       ClassObject::kClassMetadataSlotBases,
                                       ClassObject::kClassMetadataSlotMro};
    for(uint32_t idx = 0; idx < ClassObject::kClassMetadataSlotCount; ++idx)
    {
        EXPECT_STREQ(expected_names[idx],
                     shape->get_property_name(idx).extract()->data);
        EXPECT_EQ(StorageKind::Inline,
                  shape->get_property_storage_location(idx).kind);
        EXPECT_EQ(int32_t(expected_slots[idx]),
                  shape->get_property_storage_location(idx).physical_idx);
        EXPECT_TRUE(
            shape->get_descriptor_info(idx).has_flag(DescriptorFlag::ReadOnly));
        EXPECT_TRUE(shape->get_descriptor_info(idx).has_flag(
            DescriptorFlag::StableSlot));
    }

    DescriptorLookup new_lookup =
        shape->lookup_descriptor_including_latent(dunder_new_name);
    ASSERT_TRUE(new_lookup.is_latent());
    EXPECT_EQ(int32_t(ClassObject::kClassPredefinedSlotNew),
              new_lookup.info.physical_idx);
    EXPECT_TRUE(new_lookup.info.has_flag(DescriptorFlag::StableSlot));
    EXPECT_FALSE(new_lookup.info.has_flag(DescriptorFlag::ReadOnly));
    EXPECT_EQ(Value::not_present(), cls->get_own_property(dunder_new_name));
    EXPECT_EQ(Value::not_present(),
              cls->read_storage_location(new_lookup.info.storage_location()));

    DescriptorLookup init_lookup =
        shape->lookup_descriptor_including_latent(dunder_init_name);
    ASSERT_TRUE(init_lookup.is_latent());
    EXPECT_EQ(int32_t(ClassObject::kClassPredefinedSlotInit),
              init_lookup.info.physical_idx);
    EXPECT_TRUE(init_lookup.info.has_flag(DescriptorFlag::StableSlot));
    EXPECT_FALSE(init_lookup.info.has_flag(DescriptorFlag::ReadOnly));
    EXPECT_EQ(Value::not_present(), cls->get_own_property(dunder_init_name));
    EXPECT_EQ(Value::not_present(),
              cls->read_storage_location(init_lookup.info.storage_location()));

    EXPECT_EQ(Value::from_oop(context.vm().type_class()),
              cls->get_own_property(dunder_class_name));
    EXPECT_EQ(cls_name.as_value(), cls->get_own_property(dunder_name_name));

    Value bases_value = cls->get_own_property(dunder_bases_name);
    ASSERT_TRUE(bases_value.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              bases_value.get_ptr<Object>()->native_layout_id());
    ASSERT_EQ(1u, bases_value.get_ptr<Tuple>()->size());
    EXPECT_EQ(Value::from_oop(context.vm().object_class()),
              bases_value.get_ptr<Tuple>()->item_unchecked(0));

    Value mro_value = cls->get_own_property(dunder_mro_name);
    ASSERT_TRUE(mro_value.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              mro_value.get_ptr<Object>()->native_layout_id());
    ASSERT_EQ(2u, mro_value.get_ptr<Tuple>()->size());
    EXPECT_EQ(Value::from_oop(cls),
              mro_value.get_ptr<Tuple>()->item_unchecked(0));
    EXPECT_EQ(Value::from_oop(context.vm().object_class()),
              mro_value.get_ptr<Tuple>()->item_unchecked(1));

    TValue<String> readonly_names[] = {dunder_class_name, dunder_name_name,
                                       dunder_bases_name, dunder_mro_name};
    Value readonly_values[] = {Value::from_oop(context.vm().type_class()),
                               cls_name.as_value(), bases_value, mro_value};
    for(uint32_t idx = 0; idx < ClassObject::kClassMetadataSlotCount; ++idx)
    {
        Shape *before_shape = cls->get_shape();
        EXPECT_FALSE(
            cls->set_own_property(readonly_names[idx], other_name.as_value()));
        EXPECT_FALSE(cls->delete_own_property(readonly_names[idx]));
        EXPECT_EQ(before_shape, cls->get_shape());
        EXPECT_EQ(readonly_values[idx],
                  cls->get_own_property(readonly_names[idx]));
    }
    EXPECT_EQ(cls_name.as_value(), cls->get_own_property(dunder_name_name));
}

TEST(ClassObject, PredefinedConstructorSlotsAreReadWriteStableSlots)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> dunder_init_name(
        context.vm().get_or_create_interned_string_value(L"__init__"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());

    DescriptorLookup latent_lookup =
        cls->get_shape()->lookup_descriptor_including_latent(dunder_init_name);
    ASSERT_TRUE(latent_lookup.is_latent());
    EXPECT_EQ(int32_t(ClassObject::kClassPredefinedSlotInit),
              latent_lookup.info.physical_idx);
    EXPECT_EQ(Value::not_present(), cls->get_own_property(dunder_init_name));

    ASSERT_TRUE(cls->set_own_property(dunder_init_name, Value::from_smi(42)));
    DescriptorLookup present_lookup =
        cls->get_shape()->lookup_descriptor_including_latent(dunder_init_name);
    ASSERT_TRUE(present_lookup.is_present());
    EXPECT_EQ(int32_t(ClassObject::kClassPredefinedSlotInit),
              present_lookup.info.physical_idx);
    EXPECT_EQ(Value::from_smi(42), cls->get_own_property(dunder_init_name));
    EXPECT_EQ(Value::from_smi(42), cls->read_storage_location(
                                       present_lookup.info.storage_location()));

    ASSERT_TRUE(cls->delete_own_property(dunder_init_name));
    DescriptorLookup deleted_lookup =
        cls->get_shape()->lookup_descriptor_including_latent(dunder_init_name);
    ASSERT_TRUE(deleted_lookup.is_latent());
    EXPECT_EQ(int32_t(ClassObject::kClassPredefinedSlotInit),
              deleted_lookup.info.physical_idx);
    EXPECT_EQ(Value::not_present(), cls->get_own_property(dunder_init_name));
    EXPECT_EQ(
        Value::not_present(),
        cls->read_storage_location(deleted_lookup.info.storage_location()));
}

TEST(ClassObject, BuiltinClassRegistersReadonlyFixedMethods)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"str"));
    TValue<String> method_name(
        context.vm().get_or_create_interned_string_value(L"upper"));
    TValue<String> other_method_name(
        context.vm().get_or_create_interned_string_value(L"lower"));
    BuiltinClassMethod methods[] = {
        BuiltinClassMethod{method_name, Value::from_smi(11)},
        BuiltinClassMethod{other_method_name, Value::from_smi(23)},
    };

    ClassObject *cls = ClassObject::make_builtin_class(
        cls_name, 2, methods, 2, context.vm().object_class());

    Shape *shape = cls->get_shape();
    ASSERT_NE(nullptr, shape);
    EXPECT_TRUE(shape->has_flag(ShapeFlag::IsClassObject));
    EXPECT_TRUE(shape->has_flag(ShapeFlag::IsImmutableType));
    ASSERT_EQ(2u, class_property_count(cls));
    EXPECT_STREQ(L"upper", class_property_name(cls, 0).extract()->data);
    EXPECT_STREQ(L"lower", class_property_name(cls, 1).extract()->data);
    EXPECT_EQ(Value::from_smi(11), class_property_value(cls, 0));
    EXPECT_EQ(Value::from_smi(23), class_property_value(cls, 1));

    for(uint32_t idx = ClassObject::kClassMetadataSlotCount;
        idx < shape->present_count(); ++idx)
    {
        DescriptorInfo info = shape->get_descriptor_info(idx);
        EXPECT_TRUE(info.has_flag(DescriptorFlag::ReadOnly));
        EXPECT_TRUE(info.has_flag(DescriptorFlag::StableSlot));
    }

    Shape *before_shape = cls->get_shape();
    EXPECT_FALSE(cls->set_own_property(method_name, Value::from_smi(99)));
    EXPECT_FALSE(cls->delete_own_property(other_method_name));
    EXPECT_EQ(before_shape, cls->get_shape());
    EXPECT_EQ(Value::from_smi(11), cls->get_own_property(method_name));
    EXPECT_EQ(Value::from_smi(23), cls->get_own_property(other_method_name));
}

TEST(ClassObject, DefineAndSetExistingOwnPropertyHaveSeparateSemantics)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"attr"));
    TValue<String> missing_name(
        context.vm().get_or_create_interned_string_value(L"missing"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());

    EXPECT_FALSE(cls->set_existing_own_property(attr_name, Value::from_smi(1)));
    EXPECT_TRUE(
        cls->define_own_property(attr_name, Value::from_smi(1),
                                 descriptor_flag(DescriptorFlag::StableSlot)));
    EXPECT_EQ(Value::from_smi(1), cls->get_own_property(attr_name));
    EXPECT_FALSE(
        cls->define_own_property(attr_name, Value::from_smi(2),
                                 descriptor_flag(DescriptorFlag::StableSlot)));
    EXPECT_TRUE(cls->set_existing_own_property(attr_name, Value::from_smi(3)));
    EXPECT_EQ(Value::from_smi(3), cls->get_own_property(attr_name));
    EXPECT_FALSE(
        cls->set_existing_own_property(missing_name, Value::from_smi(4)));
    EXPECT_EQ(Value::not_present(), cls->get_own_property(missing_name));
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
    ClassObject *base = context.thread()->make_internal_raw<ClassObject>(
        base_name, 2, context.vm().object_class());
    ClassObject *child =
        context.thread()->make_internal_raw<ClassObject>(child_name, 2, base);

    Value bases_value = child->get_own_property(dunder_bases_name);
    ASSERT_TRUE(bases_value.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              bases_value.get_ptr<Object>()->native_layout_id());
    ASSERT_EQ(1u, bases_value.get_ptr<Tuple>()->size());
    EXPECT_EQ(Value::from_oop(base),
              bases_value.get_ptr<Tuple>()->item_unchecked(0));

    Value mro_value = child->get_own_property(dunder_mro_name);
    ASSERT_TRUE(mro_value.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              mro_value.get_ptr<Object>()->native_layout_id());
    ASSERT_EQ(3u, mro_value.get_ptr<Tuple>()->size());
    EXPECT_EQ(Value::from_oop(child),
              mro_value.get_ptr<Tuple>()->item_unchecked(0));
    EXPECT_EQ(Value::from_oop(base),
              mro_value.get_ptr<Tuple>()->item_unchecked(1));
    EXPECT_EQ(Value::from_oop(context.vm().object_class()),
              mro_value.get_ptr<Tuple>()->item_unchecked(2));
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
    ClassObject *base = context.thread()->make_internal_raw<ClassObject>(
        base_name, 2, context.vm().object_class());
    ClassObject *child =
        context.thread()->make_internal_raw<ClassObject>(child_name, 2, base);

    base->set_own_property(attr_name, Value::from_smi(7));

    EXPECT_EQ(Value::from_smi(7), base->get_own_property(attr_name));
    EXPECT_EQ(Value::not_present(), child->get_own_property(attr_name));
    EXPECT_EQ(Value::from_smi(7), load_attr(Value::from_oop(child), attr_name));

    child->set_own_property(attr_name, Value::from_smi(8));
    EXPECT_EQ(Value::from_smi(8), child->get_own_property(attr_name));
    EXPECT_EQ(Value::from_smi(8), load_attr(Value::from_oop(child), attr_name));

    EXPECT_TRUE(child->delete_own_property(attr_name));
    EXPECT_EQ(Value::not_present(), child->get_own_property(attr_name));
    EXPECT_EQ(Value::from_smi(7), load_attr(Value::from_oop(child), attr_name));
}

TEST(ClassObject, MutationDistinguishesSlotUpdateAddAndDelete)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"attr"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());

    Shape *root_shape = cls->get_shape();
    EXPECT_TRUE(root_shape->has_flag(ShapeFlag::IsClassObject));
    EXPECT_TRUE(cls->set_own_property(attr_name, Value::from_smi(1)));
    Shape *shape_with_attr = cls->get_shape();
    EXPECT_NE(root_shape, shape_with_attr);
    EXPECT_TRUE(shape_with_attr->has_flag(ShapeFlag::IsClassObject));
    EXPECT_EQ(Value::from_smi(1), cls->get_own_property(attr_name));

    StorageLocation location =
        shape_with_attr->resolve_present_property(attr_name);
    ASSERT_TRUE(location.is_found());
    EXPECT_TRUE(cls->set_own_property(attr_name, Value::from_smi(2)));
    EXPECT_EQ(shape_with_attr, cls->get_shape());
    EXPECT_EQ(Value::from_smi(2), cls->read_storage_location(location));

    EXPECT_TRUE(cls->delete_own_property(attr_name));
    Shape *shape_without_attr = cls->get_shape();
    EXPECT_NE(shape_with_attr, shape_without_attr);
    EXPECT_TRUE(shape_without_attr->has_flag(ShapeFlag::IsClassObject));
    EXPECT_EQ(Value::not_present(), cls->read_storage_location(location));
    EXPECT_EQ(Value::not_present(), cls->get_own_property(attr_name));
}

TEST(ClassObject, MroShapeAndContentsValidityCellStartsNullAndIsCreatedLazily)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());

    EXPECT_EQ(nullptr, cls->current_mro_shape_and_contents_validity_cell());

    ValidityCell *cell =
        cls->get_or_create_mro_shape_and_contents_validity_cell();

    ASSERT_NE(nullptr, cell);
    EXPECT_TRUE(cell->is_valid());
    EXPECT_EQ(cell, cls->current_mro_shape_and_contents_validity_cell());
}

TEST(ClassObject,
     BaseContentsMutationInvalidatesChildMroShapeAndContentsValidityCell)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> base_name(
        context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> child_name(
        context.vm().get_or_create_interned_string_value(L"Child"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"attr"));
    ClassObject *base = context.thread()->make_internal_raw<ClassObject>(
        base_name, 2, context.vm().object_class());
    ClassObject *child =
        context.thread()->make_internal_raw<ClassObject>(child_name, 2, base);

    ValidityCell *child_cell =
        child->get_or_create_mro_shape_and_contents_validity_cell();
    ASSERT_NE(nullptr, child_cell);
    ASSERT_TRUE(child_cell->is_valid());
    EXPECT_EQ(1u, base->attached_mro_shape_and_contents_validity_cell_count());

    EXPECT_TRUE(base->set_own_property(attr_name, Value::from_smi(1)));

    EXPECT_FALSE(child_cell->is_valid());
    EXPECT_EQ(0u, base->attached_mro_shape_and_contents_validity_cell_count());

    ValidityCell *delete_cell =
        child->get_or_create_mro_shape_and_contents_validity_cell();
    ASSERT_NE(nullptr, delete_cell);
    ASSERT_TRUE(delete_cell->is_valid());
    EXPECT_EQ(1u, base->attached_mro_shape_and_contents_validity_cell_count());

    EXPECT_TRUE(base->delete_own_property(attr_name));

    EXPECT_FALSE(delete_cell->is_valid());
    EXPECT_EQ(0u, base->attached_mro_shape_and_contents_validity_cell_count());
}

TEST(ClassObject,
     ReceiverContentsMutationInvalidatesOwnMroShapeAndContentsValidityCell)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"attr"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());

    ValidityCell *cell =
        cls->get_or_create_mro_shape_and_contents_validity_cell();
    ASSERT_NE(nullptr, cell);
    ASSERT_TRUE(cell->is_valid());

    EXPECT_TRUE(cls->set_own_property(attr_name, Value::from_smi(1)));

    EXPECT_FALSE(cell->is_valid());
    EXPECT_EQ(nullptr, cls->current_mro_shape_and_contents_validity_cell());

    ValidityCell *updated_cell =
        cls->get_or_create_mro_shape_and_contents_validity_cell();
    ASSERT_NE(nullptr, updated_cell);
    ASSERT_TRUE(updated_cell->is_valid());

    EXPECT_TRUE(cls->set_own_property(attr_name, Value::from_smi(2)));

    EXPECT_FALSE(updated_cell->is_valid());
    EXPECT_EQ(nullptr, cls->current_mro_shape_and_contents_validity_cell());
}

TEST(ClassObject,
     NewMroShapeAndContentsValidityCellReusesNearFrontBaseContentsAttachment)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> base_name(
        context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> child_name(
        context.vm().get_or_create_interned_string_value(L"Child"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"attr"));
    ClassObject *base = context.thread()->make_internal_raw<ClassObject>(
        base_name, 2, context.vm().object_class());
    ClassObject *child =
        context.thread()->make_internal_raw<ClassObject>(child_name, 2, base);

    ValidityCell *first_cell =
        child->get_or_create_mro_shape_and_contents_validity_cell();
    ASSERT_NE(nullptr, first_cell);
    ASSERT_TRUE(first_cell->is_valid());
    EXPECT_EQ(1u, base->attached_mro_shape_and_contents_validity_cell_count());

    EXPECT_TRUE(child->set_own_property(attr_name, Value::from_smi(1)));
    EXPECT_FALSE(first_cell->is_valid());
    EXPECT_EQ(1u, base->attached_mro_shape_and_contents_validity_cell_count());

    ValidityCell *second_cell =
        child->get_or_create_mro_shape_and_contents_validity_cell();
    ASSERT_NE(nullptr, second_cell);
    EXPECT_NE(first_cell, second_cell);
    EXPECT_TRUE(second_cell->is_valid());
    EXPECT_EQ(1u, base->attached_mro_shape_and_contents_validity_cell_count());

    EXPECT_TRUE(base->set_own_property(attr_name, Value::from_smi(2)));
    EXPECT_FALSE(second_cell->is_valid());
    EXPECT_EQ(0u, base->attached_mro_shape_and_contents_validity_cell_count());
}

TEST(ClassObject,
     MroShapeAndMetaclassMroShapeAndContentsValidityCellAttachesToMetaclassMro)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> meta_name(
        context.vm().get_or_create_interned_string_value(L"Meta"));
    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"attr"));
    ClassObject *meta = context.thread()->make_internal_raw<ClassObject>(
        context.vm().type_class(), meta_name, 2, context.vm().object_class());
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        meta, cls_name, 2, context.vm().object_class());

    EXPECT_EQ(
        nullptr,
        cls->current_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell());
    EXPECT_EQ(0u, meta->attached_mro_shape_and_contents_validity_cell_count());

    ValidityCell *cell =
        cls->get_or_create_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell();

    ASSERT_NE(nullptr, cell);
    EXPECT_TRUE(cell->is_valid());
    EXPECT_EQ(
        cell,
        cls->current_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell());
    EXPECT_EQ(1u, meta->attached_mro_shape_and_contents_validity_cell_count());

    EXPECT_TRUE(meta->set_own_property(attr_name, Value::from_smi(1)));

    EXPECT_FALSE(cell->is_valid());
    EXPECT_EQ(0u, meta->attached_mro_shape_and_contents_validity_cell_count());
}

TEST(ClassObject, TypeCombinedValidityCellSkipsMetaclassSelfLoop)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    ClassObject *type = context.vm().type_class();

    EXPECT_EQ(
        nullptr,
        type->current_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell());
    EXPECT_EQ(0u, type->attached_mro_shape_and_contents_validity_cell_count());

    ValidityCell *cell =
        type->get_or_create_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell();

    ASSERT_NE(nullptr, cell);
    EXPECT_TRUE(cell->is_valid());
    EXPECT_EQ(
        cell,
        type->current_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell());
    EXPECT_EQ(0u, type->attached_mro_shape_and_contents_validity_cell_count());
}

TEST(ClassObject, ClassLookupWalksMaterializedMro)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> base_name(
        context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> child_name(
        context.vm().get_or_create_interned_string_value(L"Child"));
    TValue<String> method_name(
        context.vm().get_or_create_interned_string_value(L"m"));
    ClassObject *base = context.thread()->make_internal_raw<ClassObject>(
        base_name, 2, context.vm().object_class());
    ClassObject *child =
        context.thread()->make_internal_raw<ClassObject>(child_name, 2, base);

    base->set_own_property(method_name, Value::from_smi(7));

    EXPECT_EQ(Value::from_smi(7),
              load_attr(Value::from_oop(child), method_name));
}

TEST(ClassObject, ClassLookupContinuesPastLatentDescriptor)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> base_name(
        context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> child_name(
        context.vm().get_or_create_interned_string_value(L"Child"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"attr"));
    ClassObject *base = context.thread()->make_internal_raw<ClassObject>(
        base_name, 2, context.vm().object_class());
    ClassObject *child =
        context.thread()->make_internal_raw<ClassObject>(child_name, 2, base);
    DescriptorFlags flags = descriptor_flag(DescriptorFlag::StableSlot);

    base->set_own_property(attr_name, Value::from_smi(7));
    Shape *shape_with_attr = child->get_shape()->derive_transition(
        attr_name, ShapeTransitionVerb::Add, flags);
    child->set_shape(shape_with_attr);
    StorageLocation location =
        shape_with_attr->resolve_present_property(attr_name);
    ASSERT_TRUE(location.is_found());
    child->write_storage_location(location, Value::from_smi(8));
    EXPECT_TRUE(child->delete_own_property(attr_name));

    DescriptorLookup lookup =
        child->get_shape()->lookup_descriptor_including_latent(attr_name);
    ASSERT_TRUE(lookup.is_latent());
    EXPECT_EQ(Value::not_present(), child->get_own_property(attr_name));
    EXPECT_EQ(Value::from_smi(7), load_attr(Value::from_oop(child), attr_name));
}
