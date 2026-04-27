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
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());
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
        context.thread()->make_internal_raw<ClassObject>(
            descriptor_cls_name, 2, context.vm().object_class());
    descriptor_cls->set_own_property(get_name, Value::from_smi(1));
    descriptor_cls->set_own_property(set_name, Value::from_smi(2));

    Instance *descriptor =
        context.thread()->make_internal_raw<Instance>(descriptor_cls);
    ClassObject *owner_cls = context.thread()->make_internal_raw<ClassObject>(
        owner_cls_name, 2, context.vm().object_class());
    owner_cls->set_own_property(attr_name, Value::from_oop(descriptor));

    Instance *instance =
        context.thread()->make_internal_raw<Instance>(owner_cls);
    instance->set_own_property(attr_name, Value::from_smi(7));

    AttributeReadDescriptor read_descriptor =
        resolve_attr_read_descriptor(Value::from_oop(instance), attr_name);
    ASSERT_TRUE(read_descriptor.is_found());
    EXPECT_EQ(AttributeReadPlanKind::DataDescriptorGet,
              read_descriptor.plan.kind);
    EXPECT_EQ(Value::from_oop(descriptor), read_descriptor.plan.value);
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
        context.thread()->make_internal_raw<ClassObject>(
            descriptor_cls_name, 2, context.vm().object_class());
    descriptor_cls->set_own_property(get_name, Value::from_smi(1));

    Instance *descriptor =
        context.thread()->make_internal_raw<Instance>(descriptor_cls);
    ClassObject *owner_cls = context.thread()->make_internal_raw<ClassObject>(
        owner_cls_name, 2, context.vm().object_class());
    owner_cls->set_own_property(attr_name, Value::from_oop(descriptor));

    Instance *instance =
        context.thread()->make_internal_raw<Instance>(owner_cls);
    instance->set_own_property(attr_name, Value::from_smi(7));

    AttributeReadDescriptor read_descriptor =
        resolve_attr_read_descriptor(Value::from_oop(instance), attr_name);
    ASSERT_TRUE(read_descriptor.is_found());
    EXPECT_EQ(AttributeReadPlanKind::ReceiverSlot, read_descriptor.plan.kind);
    EXPECT_EQ(nullptr, read_descriptor.plan.storage_owner);
    EXPECT_EQ(Value::from_smi(7), load_attr_from_plan(Value::from_oop(instance),
                                                      read_descriptor.plan));

    EXPECT_TRUE(instance->delete_own_property(attr_name));
    read_descriptor =
        resolve_attr_read_descriptor(Value::from_oop(instance), attr_name);
    ASSERT_TRUE(read_descriptor.is_found());
    EXPECT_EQ(AttributeReadPlanKind::NonDataDescriptorGet,
              read_descriptor.plan.kind);
    EXPECT_EQ(Value::from_oop(descriptor), read_descriptor.plan.value);
}

TEST(Attr, InstanceOwnReadDescriptorKeepsClassCacheBlockers)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> descriptor_cls_name(
        context.vm().get_or_create_interned_string_value(L"Descriptor"));
    TValue<String> owner_cls_name(
        context.vm().get_or_create_interned_string_value(L"Owner"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"field"));

    ClassObject *descriptor_cls =
        context.thread()->make_internal_raw<ClassObject>(
            descriptor_cls_name, 2, context.vm().object_class());
    Instance *descriptor =
        context.thread()->make_internal_raw<Instance>(descriptor_cls);
    ClassObject *owner_cls = context.thread()->make_internal_raw<ClassObject>(
        owner_cls_name, 2, context.vm().object_class());
    owner_cls->set_own_property(attr_name, Value::from_oop(descriptor));

    Instance *instance =
        context.thread()->make_internal_raw<Instance>(owner_cls);
    instance->set_own_property(attr_name, Value::from_smi(7));

    AttributeReadDescriptor read_descriptor =
        resolve_attr_read_descriptor(Value::from_oop(instance), attr_name);

    ASSERT_TRUE(read_descriptor.is_found());
    EXPECT_EQ(AttributeReadPlanKind::ReceiverSlot, read_descriptor.plan.kind);
    EXPECT_EQ(
        attribute_cache_blocker(AttributeCacheBlocker::MutableDescriptorType),
        read_descriptor.cache_blockers);
    EXPECT_EQ(nullptr, read_descriptor.plan.lookup_validity_cell);
    EXPECT_FALSE(read_descriptor.is_cacheable());
    EXPECT_EQ(nullptr, owner_cls->current_mro_validity_cell());
}

TEST(Attr, ClassReadDescriptorDoesNotBlockOnWinningMutableValue)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> descriptor_cls_name(
        context.vm().get_or_create_interned_string_value(L"Descriptor"));
    TValue<String> owner_cls_name(
        context.vm().get_or_create_interned_string_value(L"Owner"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"field"));

    ClassObject *descriptor_cls =
        context.thread()->make_internal_raw<ClassObject>(
            descriptor_cls_name, 2, context.vm().object_class());
    Instance *descriptor =
        context.thread()->make_internal_raw<Instance>(descriptor_cls);
    ClassObject *owner_cls = context.thread()->make_internal_raw<ClassObject>(
        owner_cls_name, 2, context.vm().object_class());
    owner_cls->set_own_property(attr_name, Value::from_oop(descriptor));

    Instance *instance =
        context.thread()->make_internal_raw<Instance>(owner_cls);

    AttributeReadDescriptor read_descriptor =
        resolve_attr_read_descriptor(Value::from_oop(instance), attr_name);

    ASSERT_TRUE(read_descriptor.is_found());
    EXPECT_EQ(AttributeReadPlanKind::ResolvedValue, read_descriptor.plan.kind);
    EXPECT_EQ(attribute_cache_blocker(AttributeCacheBlocker::None),
              read_descriptor.cache_blockers);
    EXPECT_TRUE(read_descriptor.is_cacheable());
    EXPECT_EQ(owner_cls->current_mro_validity_cell(),
              read_descriptor.plan.lookup_validity_cell);
}

TEST(Attr, ClassReadDescriptorUsesMroAndMetaclassMroValidityCell)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> meta_name(
        context.vm().get_or_create_interned_string_value(L"Meta"));
    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"field"));
    ClassObject *meta = context.thread()->make_internal_raw<ClassObject>(
        context.vm().type_class(), meta_name, 2, context.vm().object_class());
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        meta, cls_name, 2, context.vm().object_class());
    EXPECT_TRUE(cls->set_own_property(attr_name, Value::from_smi(7)));

    AttributeReadDescriptor descriptor =
        resolve_attr_read_descriptor(Value::from_oop(cls), attr_name);

    ASSERT_TRUE(descriptor.is_found());
    EXPECT_TRUE(descriptor.is_cacheable());
    ValidityCell *cell = descriptor.plan.lookup_validity_cell;
    ASSERT_NE(nullptr, cell);
    EXPECT_EQ(cell, cls->current_mro_and_metaclass_mro_validity_cell());
    EXPECT_EQ(nullptr, cls->current_mro_validity_cell());
    EXPECT_EQ(1u, meta->attached_lookup_validity_cell_count());

    EXPECT_TRUE(meta->set_own_property(
        context.vm().get_or_create_interned_string_value(L"other"),
        Value::from_smi(8)));

    EXPECT_FALSE(cell->is_valid());
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
    ClassObject *base = context.thread()->make_internal_raw<ClassObject>(
        base_name, 2, context.vm().object_class());
    base->set_own_property(inherited_name, Value::from_smi(7));
    ClassObject *child =
        context.thread()->make_internal_raw<ClassObject>(child_name, 2, base);

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
    ClassObject *meta = context.thread()->make_internal_raw<ClassObject>(
        meta_name, 2, context.vm().object_class());
    meta->set_own_property(attr_name, Value::from_smi(7));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        meta, cls_name, 2, context.vm().object_class());

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
    ClassObject *meta = context.thread()->make_internal_raw<ClassObject>(
        meta_name, 2, context.vm().object_class());
    meta->set_own_property(attr_name, Value::from_smi(7));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        meta, cls_name, 2, context.vm().object_class());
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
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());
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
        Value::from_oop(context.vm().object_class()),
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
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

    EXPECT_TRUE(
        store_attr(Value::from_oop(instance), attr_name, Value::from_smi(9)));
    EXPECT_EQ(Value::from_smi(9),
              load_attr(Value::from_oop(instance), attr_name));
}

TEST(Attr, ReceiverSlotPlansExecuteAgainstCurrentReceiver)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"value"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());
    Instance *first = context.thread()->make_internal_raw<Instance>(cls);
    Instance *second = context.thread()->make_internal_raw<Instance>(cls);

    EXPECT_TRUE(first->set_own_property(attr_name, Value::from_smi(1)));
    EXPECT_TRUE(second->set_own_property(attr_name, Value::from_smi(2)));
    ASSERT_EQ(first->get_shape(), second->get_shape());

    AttributeReadDescriptor read_descriptor =
        resolve_attr_read_descriptor(Value::from_oop(first), attr_name);
    ASSERT_TRUE(read_descriptor.is_found());
    ASSERT_EQ(AttributeReadPlanKind::ReceiverSlot, read_descriptor.plan.kind);
    ASSERT_EQ(nullptr, read_descriptor.plan.storage_owner);
    EXPECT_EQ(Value::from_smi(2), load_attr_from_plan(Value::from_oop(second),
                                                      read_descriptor.plan));

    AttributeWriteDescriptor write_descriptor =
        resolve_attr_write_descriptor(Value::from_oop(first), attr_name);
    ASSERT_TRUE(write_descriptor.is_found());
    EXPECT_EQ(attribute_cache_blocker(AttributeCacheBlocker::None),
              write_descriptor.cache_blockers);
    ASSERT_EQ(nullptr, write_descriptor.plan.storage_owner);
    EXPECT_TRUE(store_attr_from_plan(
        Value::from_oop(second), write_descriptor.plan, Value::from_smi(3)));

    EXPECT_EQ(Value::from_smi(1), load_attr(Value::from_oop(first), attr_name));
    EXPECT_EQ(Value::from_smi(3),
              load_attr(Value::from_oop(second), attr_name));
}

TEST(Attr, StoreAttrWritesClassMember)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"value"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());

    EXPECT_TRUE(
        store_attr(Value::from_oop(cls), attr_name, Value::from_smi(5)));
    EXPECT_EQ(Value::from_smi(5), load_attr(Value::from_oop(cls), attr_name));
}

TEST(Attr, ClassWriteDescriptorUsesMetaclassMroValidityCell)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> meta_name(
        context.vm().get_or_create_interned_string_value(L"Meta"));
    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"value"));
    TValue<String> descriptor_name(
        context.vm().get_or_create_interned_string_value(L"descriptor"));
    ClassObject *meta = context.thread()->make_internal_raw<ClassObject>(
        context.vm().type_class(), meta_name, 2, context.vm().object_class());
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        meta, cls_name, 2, context.vm().object_class());

    EXPECT_TRUE(cls->set_own_property(attr_name, Value::from_smi(1)));

    AttributeWriteDescriptor descriptor =
        resolve_attr_write_descriptor(Value::from_oop(cls), attr_name);

    ASSERT_TRUE(descriptor.is_found());
    EXPECT_TRUE(descriptor.is_cacheable());
    ValidityCell *cell = descriptor.plan.lookup_validity_cell;
    ASSERT_NE(nullptr, cell);
    EXPECT_EQ(cell, meta->current_mro_validity_cell());
    EXPECT_EQ(nullptr, cls->current_mro_validity_cell());

    EXPECT_TRUE(meta->set_own_property(descriptor_name, Value::from_smi(1)));

    EXPECT_FALSE(cell->is_valid());
}

TEST(Attr, AttributeWriteDescriptorCarriesSupersededClassCacheBlockers)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> descriptor_cls_name(
        context.vm().get_or_create_interned_string_value(L"Descriptor"));
    TValue<String> owner_cls_name(
        context.vm().get_or_create_interned_string_value(L"Owner"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"field"));

    ClassObject *descriptor_cls =
        context.thread()->make_internal_raw<ClassObject>(
            descriptor_cls_name, 2, context.vm().object_class());
    Instance *descriptor =
        context.thread()->make_internal_raw<Instance>(descriptor_cls);
    ClassObject *owner_cls = context.thread()->make_internal_raw<ClassObject>(
        owner_cls_name, 2, context.vm().object_class());
    owner_cls->set_own_property(attr_name, Value::from_oop(descriptor));

    Instance *instance =
        context.thread()->make_internal_raw<Instance>(owner_cls);
    instance->set_own_property(attr_name, Value::from_smi(7));

    AttributeWriteDescriptor write_descriptor =
        resolve_attr_write_descriptor(Value::from_oop(instance), attr_name);

    ASSERT_TRUE(write_descriptor.is_found());
    EXPECT_EQ(
        attribute_cache_blocker(AttributeCacheBlocker::MutableDescriptorType),
        write_descriptor.cache_blockers);
    EXPECT_EQ(nullptr, write_descriptor.plan.lookup_validity_cell);
    EXPECT_FALSE(write_descriptor.is_cacheable());
    EXPECT_EQ(nullptr, owner_cls->current_mro_validity_cell());
}

TEST(Attr, AttributeWriteDescriptorRejectsClassDataDescriptor)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> descriptor_cls_name(
        context.vm().get_or_create_interned_string_value(L"Descriptor"));
    TValue<String> owner_cls_name(
        context.vm().get_or_create_interned_string_value(L"Owner"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"field"));
    TValue<String> set_name(
        context.vm().get_or_create_interned_string_value(L"__set__"));

    ClassObject *descriptor_cls =
        context.thread()->make_internal_raw<ClassObject>(
            descriptor_cls_name, 2, context.vm().object_class());
    descriptor_cls->set_own_property(set_name, Value::from_smi(1));
    Instance *descriptor =
        context.thread()->make_internal_raw<Instance>(descriptor_cls);
    ClassObject *owner_cls = context.thread()->make_internal_raw<ClassObject>(
        owner_cls_name, 2, context.vm().object_class());
    owner_cls->set_own_property(attr_name, Value::from_oop(descriptor));

    Instance *instance =
        context.thread()->make_internal_raw<Instance>(owner_cls);
    instance->set_own_property(attr_name, Value::from_smi(7));

    AttributeWriteDescriptor write_descriptor =
        resolve_attr_write_descriptor(Value::from_oop(instance), attr_name);

    EXPECT_FALSE(write_descriptor.is_found());
    EXPECT_EQ(AttributeWriteStatus::Disallowed, write_descriptor.status);
    EXPECT_FALSE(write_descriptor.is_cacheable());
    EXPECT_EQ(Value::from_smi(7),
              load_attr(Value::from_oop(instance), attr_name));
}

TEST(Attr, AttributeWritesInvalidateLookupCellsForClassTargets)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"value"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

    ValidityCell *add_cell = cls->get_or_create_mro_validity_cell();
    ASSERT_NE(nullptr, add_cell);
    EXPECT_TRUE(cls->set_own_property(attr_name, Value::from_smi(5)));
    EXPECT_FALSE(add_cell->is_valid());
    EXPECT_EQ(nullptr, cls->current_mro_validity_cell());

    ValidityCell *update_cell = cls->get_or_create_mro_validity_cell();
    ASSERT_NE(nullptr, update_cell);
    EXPECT_TRUE(cls->set_own_property(attr_name, Value::from_smi(6)));
    EXPECT_FALSE(update_cell->is_valid());
    EXPECT_EQ(nullptr, cls->current_mro_validity_cell());

    ValidityCell *delete_cell = cls->get_or_create_mro_validity_cell();
    ASSERT_NE(nullptr, delete_cell);
    EXPECT_TRUE(cls->delete_own_property(attr_name));
    EXPECT_FALSE(delete_cell->is_valid());
    EXPECT_EQ(nullptr, cls->current_mro_validity_cell());

    ValidityCell *instance_add_delete_cell =
        cls->get_or_create_mro_validity_cell();
    ASSERT_NE(nullptr, instance_add_delete_cell);
    EXPECT_TRUE(instance->set_own_property(attr_name, Value::from_smi(7)));
    EXPECT_TRUE(instance_add_delete_cell->is_valid());
    EXPECT_EQ(instance_add_delete_cell, cls->current_mro_validity_cell());

    EXPECT_TRUE(instance->delete_own_property(attr_name));
    EXPECT_TRUE(instance_add_delete_cell->is_valid());
    EXPECT_EQ(instance_add_delete_cell, cls->current_mro_validity_cell());
}

TEST(Attr, AttributeWriteDescriptorMissDoesNotCreateLookupValidityCell)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> base_name(
        context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> child_name(
        context.vm().get_or_create_interned_string_value(L"Child"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"value"));
    ClassObject *base = context.thread()->make_internal_raw<ClassObject>(
        base_name, 2, context.vm().object_class());
    ClassObject *child =
        context.thread()->make_internal_raw<ClassObject>(child_name, 2, base);
    Instance *instance = context.thread()->make_internal_raw<Instance>(child);

    AttributeWriteDescriptor descriptor =
        resolve_attr_write_descriptor(Value::from_oop(instance), attr_name);

    EXPECT_FALSE(descriptor.is_found());
    EXPECT_EQ(AttributeWriteStatus::NotFound, descriptor.status);
    EXPECT_EQ(attribute_cache_blocker(AttributeCacheBlocker::None),
              descriptor.cache_blockers);
    EXPECT_FALSE(descriptor.is_cacheable());
    EXPECT_EQ(nullptr, child->current_mro_validity_cell());
    EXPECT_EQ(0u, base->attached_lookup_validity_cell_count());
}

TEST(Attr, AttributeWriteDescriptorCarriesLookupValidityForDescriptorMiss)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> base_name(
        context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> child_name(
        context.vm().get_or_create_interned_string_value(L"Child"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"value"));
    TValue<String> descriptor_name(
        context.vm().get_or_create_interned_string_value(L"descriptor"));
    ClassObject *base = context.thread()->make_internal_raw<ClassObject>(
        base_name, 2, context.vm().object_class());
    ClassObject *child =
        context.thread()->make_internal_raw<ClassObject>(child_name, 2, base);
    Instance *instance = context.thread()->make_internal_raw<Instance>(child);

    EXPECT_TRUE(instance->set_own_property(attr_name, Value::from_smi(1)));
    EXPECT_EQ(nullptr, child->current_mro_validity_cell());
    EXPECT_EQ(0u, base->attached_lookup_validity_cell_count());

    AttributeWriteDescriptor descriptor =
        resolve_attr_write_descriptor(Value::from_oop(instance), attr_name);

    ASSERT_TRUE(descriptor.is_found());
    EXPECT_EQ(attribute_cache_blocker(AttributeCacheBlocker::None),
              descriptor.cache_blockers);
    EXPECT_EQ(nullptr, descriptor.plan.storage_owner);
    EXPECT_TRUE(descriptor.is_cacheable());
    ValidityCell *cell = descriptor.plan.lookup_validity_cell;
    ASSERT_NE(nullptr, cell);
    EXPECT_TRUE(cell->is_valid());
    EXPECT_EQ(cell, child->current_mro_validity_cell());
    EXPECT_EQ(1u, base->attached_lookup_validity_cell_count());

    EXPECT_TRUE(base->set_own_property(descriptor_name, Value::from_smi(1)));

    EXPECT_FALSE(cell->is_valid());
}

TEST(Attr, InstanceOwnReadDescriptorCarriesLookupValidityCell)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> base_name(
        context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> child_name(
        context.vm().get_or_create_interned_string_value(L"Child"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"value"));
    TValue<String> descriptor_name(
        context.vm().get_or_create_interned_string_value(L"descriptor"));
    ClassObject *base = context.thread()->make_internal_raw<ClassObject>(
        base_name, 2, context.vm().object_class());
    ClassObject *child =
        context.thread()->make_internal_raw<ClassObject>(child_name, 2, base);
    Instance *instance = context.thread()->make_internal_raw<Instance>(child);
    EXPECT_TRUE(instance->set_own_property(attr_name, Value::from_smi(1)));

    AttributeReadDescriptor descriptor =
        resolve_attr_read_descriptor(Value::from_oop(instance), attr_name);

    ASSERT_TRUE(descriptor.is_found());
    EXPECT_EQ(AttributeReadPlanKind::ReceiverSlot, descriptor.plan.kind);
    EXPECT_EQ(nullptr, descriptor.plan.storage_owner);
    EXPECT_TRUE(descriptor.is_cacheable());
    ValidityCell *cell = descriptor.plan.lookup_validity_cell;
    ASSERT_NE(nullptr, cell);
    EXPECT_TRUE(cell->is_valid());
    EXPECT_EQ(cell, child->current_mro_validity_cell());
    EXPECT_EQ(1u, base->attached_lookup_validity_cell_count());

    EXPECT_TRUE(base->set_own_property(descriptor_name, Value::from_smi(1)));

    EXPECT_FALSE(cell->is_valid());
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
        cls_name, 2, context.vm().object_class(),
        shape_flag(ShapeFlag::IsClassObject), instance_shape_flags);
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

    EXPECT_FALSE(instance->set_own_property(attr_name, Value::from_smi(7)));
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
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());

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
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());
    ClassObject *other_cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());
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

    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class());
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
