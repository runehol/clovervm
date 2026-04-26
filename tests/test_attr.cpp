#include "attr.h"
#include "builtin_function.h"
#include "class_object.h"
#include "dict.h"
#include "function.h"
#include "instance.h"
#include "list.h"
#include "range_iterator.h"
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

    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);
    instance->set_own_property(attr_name, Value::from_smi(2));

    EXPECT_EQ(Value::from_smi(2),
              load_attr(Value::from_oop(instance), attr_name));
}

TEST(Attr, DataDescriptorReadDescriptorTakesPrecedenceOverInstanceOwnProperty)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> descriptor_cls_name(
        context.vm().get_or_create_interned_string_value(L"Descriptor"));
    TValue<String> owner_cls_name(
        context.vm().get_or_create_interned_string_value(L"Owner"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"field"));
    TValue<String> get_name(
        context.vm().get_or_create_interned_string_value(L"__get__"));
    TValue<String> set_name(
        context.vm().get_or_create_interned_string_value(L"__set__"));

    ClassObject *descriptor_cls =
        context.thread()->make_internal_raw<ClassObject>(descriptor_cls_name,
                                                         2);
    descriptor_cls->set_own_property(get_name, Value::from_smi(1));
    descriptor_cls->set_own_property(set_name, Value::from_smi(2));

    Instance *descriptor =
        context.thread()->make_internal_raw<Instance>(descriptor_cls);
    ClassObject *owner_cls =
        context.thread()->make_internal_raw<ClassObject>(owner_cls_name, 2);
    owner_cls->set_own_property(attr_name, Value::from_oop(descriptor));

    Instance *instance =
        context.thread()->make_internal_raw<Instance>(owner_cls);
    instance->set_own_property(attr_name, Value::from_smi(7));

    AttributeReadDescriptor read_descriptor =
        resolve_attr_read_descriptor(Value::from_oop(instance), attr_name);
    ASSERT_TRUE(read_descriptor.is_found());
    EXPECT_EQ(AttributeReadAccessKind::DataDescriptorGet,
              read_descriptor.access.kind);
    EXPECT_EQ(Value::from_oop(descriptor), read_descriptor.access.value);
}

TEST(Attr, NonDataDescriptorReadDescriptorRunsAfterInstanceOwnProperty)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> descriptor_cls_name(
        context.vm().get_or_create_interned_string_value(L"Descriptor"));
    TValue<String> owner_cls_name(
        context.vm().get_or_create_interned_string_value(L"Owner"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"field"));
    TValue<String> get_name(
        context.vm().get_or_create_interned_string_value(L"__get__"));

    ClassObject *descriptor_cls =
        context.thread()->make_internal_raw<ClassObject>(descriptor_cls_name,
                                                         2);
    descriptor_cls->set_own_property(get_name, Value::from_smi(1));

    Instance *descriptor =
        context.thread()->make_internal_raw<Instance>(descriptor_cls);
    ClassObject *owner_cls =
        context.thread()->make_internal_raw<ClassObject>(owner_cls_name, 2);
    owner_cls->set_own_property(attr_name, Value::from_oop(descriptor));

    Instance *instance =
        context.thread()->make_internal_raw<Instance>(owner_cls);
    instance->set_own_property(attr_name, Value::from_smi(7));

    AttributeReadDescriptor read_descriptor =
        resolve_attr_read_descriptor(Value::from_oop(instance), attr_name);
    ASSERT_TRUE(read_descriptor.is_found());
    EXPECT_EQ(AttributeReadAccessKind::ReceiverSlot,
              read_descriptor.access.kind);
    EXPECT_EQ(Value::from_smi(7), load_attr_from_descriptor(read_descriptor));

    EXPECT_TRUE(instance->delete_own_property(attr_name));
    read_descriptor =
        resolve_attr_read_descriptor(Value::from_oop(instance), attr_name);
    ASSERT_TRUE(read_descriptor.is_found());
    EXPECT_EQ(AttributeReadAccessKind::NonDataDescriptorGet,
              read_descriptor.access.kind);
    EXPECT_EQ(Value::from_oop(descriptor), read_descriptor.access.value);
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

    Instance *instance = context.thread()->make_internal_raw<Instance>(child);

    EXPECT_EQ(Value::from_smi(7),
              load_attr(Value::from_oop(instance), inherited_name));
    EXPECT_EQ(Value::from_smi(7),
              load_attr(Value::from_oop(child), inherited_name));
}

TEST(Attr, LoadAttrOnClassFallsBackToMetaclass)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> meta_name(
        context.vm().get_or_create_interned_string_value(L"Meta"));
    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"meta_attr"));
    ClassObject *meta =
        context.thread()->make_internal_raw<ClassObject>(meta_name, 2);
    meta->set_own_property(attr_name, Value::from_smi(7));
    ClassObject *cls =
        context.thread()->make_internal_raw<ClassObject>(meta, cls_name, 2);

    EXPECT_EQ(Value::from_smi(7), load_attr(Value::from_oop(cls), attr_name));
}

TEST(Attr, LoadAttrOnClassPrefersClassPathOverMetaclass)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> meta_name(
        context.vm().get_or_create_interned_string_value(L"Meta"));
    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"attr"));
    ClassObject *meta =
        context.thread()->make_internal_raw<ClassObject>(meta_name, 2);
    meta->set_own_property(attr_name, Value::from_smi(7));
    ClassObject *cls =
        context.thread()->make_internal_raw<ClassObject>(meta, cls_name, 2);
    cls->set_own_property(attr_name, Value::from_smi(11));

    EXPECT_EQ(Value::from_smi(11), load_attr(Value::from_oop(cls), attr_name));
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

    Instance *instance = context.thread()->make_internal_raw<Instance>(child);

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
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

    EXPECT_EQ(Value::from_oop(cls),
              instance->get_own_property(dunder_class_name));
    EXPECT_EQ(Value::from_oop(cls),
              load_attr(Value::from_oop(instance), dunder_class_name));
    EXPECT_EQ(Value::from_oop(context.vm().type_class()),
              load_attr(Value::from_oop(cls), dunder_class_name));

    TValue<String> string_value(
        context.vm().get_or_create_interned_string_value(L"hello"));
    EXPECT_EQ(Value::from_oop(context.vm().str_class()),
              load_attr(string_value.as_value(), dunder_class_name));
}

TEST(Attr, BuiltinInstancesExposeDunderClassThroughAttributeLookup)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> dunder_class_name(
        context.vm().get_or_create_interned_string_value(L"__class__"));

    TValue<String> string_value(
        context.vm().get_or_create_interned_string_value(L"hello"));
    List *list = context.thread()->make_object_raw<List>();
    Dict *dict = context.thread()->make_object_raw<Dict>();
    TValue<BuiltinFunction> builtin_function =
        context.thread()->make_object_value<BuiltinFunction>(builtin_identity,
                                                             1, 1);
    CodeObject *code = context.thread()->compile(L"def f():\n"
                                                 L"    return 1\n",
                                                 StartRule::File);
    Function *function = context.thread()->make_object_raw<Function>(
        TValue<CodeObject>(Value::from_oop(code)));
    RangeIterator *range_iterator =
        context.thread()->make_object_raw<RangeIterator>(
            TValue<CLInt>(Value::from_smi(0)),
            TValue<CLInt>(Value::from_smi(3)),
            TValue<CLInt>(Value::from_smi(1)));

    struct BuiltinInstance
    {
        Value value;
        ClassObject *expected_class;
    };

    BuiltinInstance instances[] = {
        {string_value.as_value(), context.vm().str_class()},
        {Value::from_oop(list), context.vm().list_class()},
        {Value::from_oop(dict), context.vm().dict_class()},
        {Value(builtin_function), context.vm().builtin_function_class()},
        {Value::from_oop(code), context.vm().code_class()},
        {Value::from_oop(function), context.vm().function_class()},
        {Value::from_oop(range_iterator), context.vm().range_iterator_class()},
    };

    for(const BuiltinInstance &instance: instances)
    {
        Object *object = instance.value.get_ptr<Object>();
        EXPECT_EQ(Value::from_oop(instance.expected_class),
                  object->get_own_property(dunder_class_name));
        EXPECT_EQ(Value::from_oop(instance.expected_class),
                  load_attr(instance.value, dunder_class_name));
    }
}

TEST(Attr, BuiltinInstancesRejectUnsupportedAttributeWrites)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"custom"));
    TValue<String> dunder_class_name(
        context.vm().get_or_create_interned_string_value(L"__class__"));

    TValue<String> string_value(
        context.vm().get_or_create_interned_string_value(L"hello"));
    List *list = context.thread()->make_object_raw<List>();
    Dict *dict = context.thread()->make_object_raw<Dict>();
    TValue<BuiltinFunction> builtin_function =
        context.thread()->make_object_value<BuiltinFunction>(builtin_identity,
                                                             1, 1);
    CodeObject *code = context.thread()->compile(L"def f():\n"
                                                 L"    return 1\n",
                                                 StartRule::File);
    Function *function = context.thread()->make_object_raw<Function>(
        TValue<CodeObject>(Value::from_oop(code)));
    RangeIterator *range_iterator =
        context.thread()->make_object_raw<RangeIterator>(
            TValue<CLInt>(Value::from_smi(0)),
            TValue<CLInt>(Value::from_smi(3)),
            TValue<CLInt>(Value::from_smi(1)));

    Value instances[] = {
        string_value.as_value(),         Value::from_oop(list),
        Value::from_oop(dict),           Value(builtin_function),
        Value::from_oop(code),           Value::from_oop(function),
        Value::from_oop(range_iterator),
    };

    for(Value instance: instances)
    {
        Value original_class = load_attr(instance, dunder_class_name);
        ASSERT_FALSE(original_class.is_not_present());

        EXPECT_FALSE(store_attr(instance, attr_name, Value::from_smi(99)));
        EXPECT_EQ(Value::not_present(), load_attr(instance, attr_name));

        EXPECT_FALSE(store_attr(instance, dunder_class_name, Value::None()));
        EXPECT_EQ(original_class, load_attr(instance, dunder_class_name));
    }
}

TEST(Attr, BuiltinTypeObjectsRejectUnsupportedAttributeWrites)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"custom"));
    Value builtin_types[] = {
        Value::from_oop(context.vm().type_class()),
        Value::from_oop(context.vm().instance_class()),
        Value::from_oop(context.vm().str_class()),
        Value::from_oop(context.vm().list_class()),
        Value::from_oop(context.vm().dict_class()),
        Value::from_oop(context.vm().function_class()),
        Value::from_oop(context.vm().builtin_function_class()),
        Value::from_oop(context.vm().code_class()),
        Value::from_oop(context.vm().range_iterator_class()),
    };

    for(Value type: builtin_types)
    {
        EXPECT_FALSE(store_attr(type, attr_name, Value::from_smi(99)));
        EXPECT_EQ(Value::not_present(), load_attr(type, attr_name));
    }
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
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

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

TEST(Attr, AttributeWritesExposeLookupInvalidationEffectForClassTargets)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"value"));
    ClassObject *cls =
        context.thread()->make_internal_raw<ClassObject>(cls_name, 2);
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

    AttributeWriteResult class_result =
        cls->set_own_property_with_result(attr_name, Value::from_smi(5));
    EXPECT_EQ(AttributeMutationKind::Added, class_result.kind);
    EXPECT_TRUE(has_attribute_write_effect(
        class_result.effects,
        AttributeWriteEffect::InvalidateLookupCellsOnTarget));

    AttributeWriteResult class_update_result =
        cls->set_own_property_with_result(attr_name, Value::from_smi(6));
    EXPECT_EQ(AttributeMutationKind::UpdatedExisting, class_update_result.kind);
    EXPECT_TRUE(has_attribute_write_effect(
        class_update_result.effects,
        AttributeWriteEffect::InvalidateLookupCellsOnTarget));

    AttributeWriteResult class_delete_result =
        cls->delete_own_property_with_result(attr_name);
    EXPECT_EQ(AttributeMutationKind::Deleted, class_delete_result.kind);
    EXPECT_TRUE(has_attribute_write_effect(
        class_delete_result.effects,
        AttributeWriteEffect::InvalidateLookupCellsOnTarget));

    AttributeWriteResult instance_result =
        instance->set_own_property_with_result(attr_name, Value::from_smi(7));
    EXPECT_EQ(AttributeMutationKind::Added, instance_result.kind);
    EXPECT_FALSE(has_attribute_write_effect(
        instance_result.effects,
        AttributeWriteEffect::InvalidateLookupCellsOnTarget));
}

TEST(Attr, ShapePolicyCanDisallowInstanceAttributeAddDelete)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"value"));
    ShapeFlags instance_shape_flags =
        shape_flag(ShapeFlag::DisallowAttributeAddDelete);
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, Value::None(), shape_flag(ShapeFlag::IsClassObject),
        instance_shape_flags);
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

    AttributeWriteResult result =
        instance->set_own_property_with_result(attr_name, Value::from_smi(7));
    EXPECT_FALSE(result.is_stored());
    EXPECT_EQ(AttributeMutationKind::NotStored, result.kind);
    EXPECT_EQ(attribute_write_effect(AttributeWriteEffect::None),
              result.effects);
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
        context.thread()->make_internal_raw<ClassObject>(cls_name, 2);
    ClassObject *other_cls =
        context.thread()->make_internal_raw<ClassObject>(cls_name, 2);
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

    EXPECT_FALSE(store_attr(Value::from_oop(instance), dunder_class_name,
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

    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);
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
