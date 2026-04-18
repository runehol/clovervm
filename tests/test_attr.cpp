#include "attr.h"
#include "class_object.h"
#include "instance.h"
#include "klass.h"
#include "test_helpers.h"
#include "thread_state.h"
#include <gtest/gtest.h>

using namespace cl;

TEST(Attr, LoadAttrReturnsInstanceOwnPropertyBeforeClassMember)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"value"));
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(cls_name, 2);
    cls->set_member(attr_name, Value::from_smi(1));

    Instance *instance = context.thread()->make_refcounted_raw<Instance>(
        Value::from_oop(cls), Value::from_oop(cls->get_initial_shape()));
    instance->set_own_property(attr_name, Value::from_smi(2));

    EXPECT_EQ(Value::from_smi(2),
              load_attr(Value::from_oop(instance), attr_name));
}

TEST(Attr, LoadAttrFallsBackToClassAndBaseMembers)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> base_name(
        context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> child_name(
        context.vm().get_or_create_interned_string_value(L"Child"));
    TValue<String> inherited_name(
        context.vm().get_or_create_interned_string_value(L"inherited"));
    ClassObject *base =
        context.thread()->make_refcounted_raw<ClassObject>(base_name, 2);
    base->set_member(inherited_name, Value::from_smi(7));
    ClassObject *child = context.thread()->make_refcounted_raw<ClassObject>(
        child_name, 2, Value::from_oop(base));

    Instance *instance = context.thread()->make_refcounted_raw<Instance>(
        Value::from_oop(child), Value::from_oop(child->get_initial_shape()));

    EXPECT_EQ(Value::from_smi(7),
              load_attr(Value::from_oop(instance), inherited_name));
    EXPECT_EQ(Value::from_smi(7),
              load_attr(Value::from_oop(child), inherited_name));
}

TEST(Attr, LoadAttrReturnsDunderClassForObjectBackedValues)
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

    EXPECT_EQ(Value::from_oop(cls),
              load_attr(Value::from_oop(instance), dunder_class_name));
    EXPECT_EQ(Value::from_oop(const_cast<Klass *>(&ClassObject::klass)),
              load_attr(Value::from_oop(cls), dunder_class_name));
}

TEST(Attr, LoadAttrMissesOnUnsupportedInlineValues)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> dunder_class_name(
        context.vm().get_or_create_interned_string_value(L"__class__"));
    TValue<String> missing_name(
        context.vm().get_or_create_interned_string_value(L"missing"));

    EXPECT_EQ(Value::not_present(),
              load_attr(Value::from_smi(3), dunder_class_name));
    EXPECT_EQ(Value::not_present(), load_attr(Value::None(), missing_name));
}

TEST(Attr, StoreAttrWritesInstanceOwnProperty)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"value"));
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(cls_name, 2);
    Instance *instance = context.thread()->make_refcounted_raw<Instance>(
        Value::from_oop(cls), Value::from_oop(cls->get_initial_shape()));

    EXPECT_TRUE(
        store_attr(Value::from_oop(instance), attr_name, Value::from_smi(9)));
    EXPECT_EQ(Value::from_smi(9),
              load_attr(Value::from_oop(instance), attr_name));
}

TEST(Attr, StoreAttrWritesClassMember)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"value"));
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(cls_name, 2);

    EXPECT_TRUE(
        store_attr(Value::from_oop(cls), attr_name, Value::from_smi(5)));
    EXPECT_EQ(Value::from_smi(5), load_attr(Value::from_oop(cls), attr_name));
}

TEST(Attr, StoreAttrRejectsDunderClassAndUnsupportedInlineValues)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"value"));
    TValue<String> dunder_class_name(
        context.vm().get_or_create_interned_string_value(L"__class__"));
    ClassObject *cls =
        context.thread()->make_refcounted_raw<ClassObject>(cls_name, 2);
    Instance *instance = context.thread()->make_refcounted_raw<Instance>(
        Value::from_oop(cls), Value::from_oop(cls->get_initial_shape()));

    EXPECT_FALSE(store_attr(Value::from_oop(instance), dunder_class_name,
                            Value::from_oop(cls)));
    EXPECT_EQ(Value::from_oop(cls),
              load_attr(Value::from_oop(instance), dunder_class_name));

    EXPECT_FALSE(store_attr(Value::from_smi(3), attr_name, Value::from_smi(7)));
}
