#include "attr.h"
#include "builtin_function.h"
#include "class_object.h"
#include "function.h"
#include "instance.h"
#include "shape.h"
#include "test_helpers.h"
#include "thread_state.h"
#include <gtest/gtest.h>

using namespace cl;

static Value builtin_identity(ThreadState *, const CallArguments &args)
{
    if(args.n_args != 1)
    {
        throw std::runtime_error("builtin_identity expected exactly one arg");
    }
    return args[0];
}

TEST(Attr, LoadAttrReturnsInstanceOwnPropertyBeforeClassMember)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"value"));
    ClassObject *cls =
        context.thread()->make_internal_raw<ClassObject>(cls_name, 2);
    cls->set_own_property(attr_name, Value::from_smi(1));

    Instance *instance = context.thread()->make_internal_raw<Instance>(
        TValue<ClassObject>::from_oop(cls));
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
        context.thread()->make_internal_raw<ClassObject>(base_name, 2);
    base->set_own_property(inherited_name, Value::from_smi(7));
    ClassObject *child = context.thread()->make_internal_raw<ClassObject>(
        child_name, 2, Value::from_oop(base));

    Instance *instance = context.thread()->make_internal_raw<Instance>(
        TValue<ClassObject>::from_oop(child));

    EXPECT_EQ(Value::from_smi(7),
              load_attr(Value::from_oop(instance), inherited_name));
    EXPECT_EQ(Value::from_smi(7),
              load_attr(Value::from_oop(child), inherited_name));
}

TEST(Attr, LoadAttrClassFallbackContinuesPastLatentDescriptor)
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
        context.thread()->make_internal_raw<ClassObject>(base_name, 2);
    ClassObject *child = context.thread()->make_internal_raw<ClassObject>(
        child_name, 2, Value::from_oop(base));
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

    Instance *instance = context.thread()->make_internal_raw<Instance>(
        TValue<ClassObject>::from_oop(child));

    EXPECT_EQ(Value::from_smi(7),
              load_attr(Value::from_oop(instance), attr_name));
    EXPECT_EQ(Value::from_smi(7), load_attr(Value::from_oop(child), attr_name));
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
        context.thread()->make_internal_raw<ClassObject>(cls_name, 2);
    Instance *instance = context.thread()->make_internal_raw<Instance>(
        TValue<ClassObject>::from_oop(cls));

    EXPECT_EQ(Value::from_oop(cls),
              instance->get_own_property(dunder_class_name));
    EXPECT_EQ(Value::from_oop(cls),
              load_attr(Value::from_oop(instance), dunder_class_name));
    EXPECT_EQ(Value::None(),
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
        context.thread()->make_internal_raw<ClassObject>(cls_name, 2);
    Instance *instance = context.thread()->make_internal_raw<Instance>(
        TValue<ClassObject>::from_oop(cls));

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
        context.thread()->make_internal_raw<ClassObject>(cls_name, 2);

    EXPECT_TRUE(
        store_attr(Value::from_oop(cls), attr_name, Value::from_smi(5)));
    EXPECT_EQ(Value::from_smi(5), load_attr(Value::from_oop(cls), attr_name));
}

TEST(Attr, ClassMetadataAttributesAreReadonly)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> replacement_name(
        context.vm().get_or_create_interned_string_value(L"Replacement"));
    TValue<String> dunder_name_name(
        context.vm().get_or_create_interned_string_value(L"__name__"));
    ClassObject *cls =
        context.thread()->make_internal_raw<ClassObject>(cls_name, 2);

    EXPECT_EQ(cls_name.as_value(),
              load_attr(Value::from_oop(cls), dunder_name_name));
    EXPECT_FALSE(store_attr(Value::from_oop(cls), dunder_name_name,
                            replacement_name.as_value()));
    EXPECT_EQ(cls_name.as_value(),
              load_attr(Value::from_oop(cls), dunder_name_name));
}

TEST(Attr, StoreAttrHandlesDunderClassAndUnsupportedInlineValues)
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
        context.thread()->make_internal_raw<ClassObject>(cls_name, 2);
    ClassObject *other_cls =
        context.thread()->make_internal_raw<ClassObject>(cls_name, 2);
    Instance *instance = context.thread()->make_internal_raw<Instance>(
        TValue<ClassObject>::from_oop(cls));

    EXPECT_TRUE(store_attr(Value::from_oop(instance), dunder_class_name,
                           Value::from_oop(cls)));
    EXPECT_FALSE(store_attr(Value::from_oop(instance), dunder_class_name,
                            Value::from_oop(other_cls)));
    EXPECT_EQ(Value::from_oop(cls),
              load_attr(Value::from_oop(instance), dunder_class_name));

    EXPECT_FALSE(store_attr(Value::from_smi(3), attr_name, Value::from_smi(7)));
}

TEST(Attr, LoadMethodBindsSelfOnlyForClassFunctions)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> method_name(
        context.vm().get_or_create_interned_string_value(L"method"));
    TValue<String> builtin_name(
        context.vm().get_or_create_interned_string_value(L"builtin"));
    TValue<String> own_name(
        context.vm().get_or_create_interned_string_value(L"own"));

    CodeObject *method_code = context.thread()->compile(L"def method(self):\n"
                                                        L"    return self\n",
                                                        StartRule::File);
    (void)context.thread()->run(method_code);
    Value method_value =
        method_code->module_scope.extract()->get_by_name(method_name);

    ClassObject *cls =
        context.thread()->make_internal_raw<ClassObject>(cls_name, 2);
    cls->set_own_property(method_name, method_value);
    TValue<BuiltinFunction> builtin =
        context.thread()->make_object_value<BuiltinFunction>(builtin_identity,
                                                             1, 1);
    cls->set_own_property(builtin_name, builtin);

    Instance *instance = context.thread()->make_internal_raw<Instance>(
        TValue<ClassObject>::from_oop(cls));
    instance->set_own_property(own_name, builtin);

    Value callable = Value::not_present();
    Value self = Value::not_present();

    ASSERT_TRUE(
        load_method(Value::from_oop(instance), method_name, callable, self));
    EXPECT_EQ(method_value, callable);
    EXPECT_EQ(Value::from_oop(instance), self);

    ASSERT_TRUE(
        load_method(Value::from_oop(instance), builtin_name, callable, self));
    EXPECT_EQ(Value(builtin), callable);
    EXPECT_TRUE(self.is_not_present());

    ASSERT_TRUE(
        load_method(Value::from_oop(instance), own_name, callable, self));
    EXPECT_EQ(Value(builtin), callable);
    EXPECT_TRUE(self.is_not_present());
}
