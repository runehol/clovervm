#include "builtin_types/bigint.h"
#include "builtin_types/dict.h"
#include "builtin_types/list.h"
#include "builtin_types/list_iterator.h"
#include "builtin_types/module_object.h"
#include "builtin_types/range_iterator.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "builtin_types/tuple_iterator.h"
#include "compiler/scope.h"
#include "native/native_layout_descriptor.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "object_model/instance.h"
#include "object_model/object.h"
#include "object_model/overflow_slots.h"
#include "object_model/shape.h"
#include "object_model/slot_dict.h"
#include "object_model/validity_cell.h"
#include "object_model/vm_array_backing.h"
#include "runtime/exception_object.h"
#include "runtime/thread_state.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <limits>

using namespace cl;

namespace
{
    class CustomDeallocTestObject : public HeapObject
    {
    public:
        static void dealloc(HeapObject *) {}

        CL_DECLARE_CUSTOM_DEALLOC(CustomDeallocTestObject, dealloc);
    };

    template <typename T> void expect_static_native_layout_descriptor()
    {
        const ReleaseDescriptor &release =
            release_descriptor_for(T::native_layout);

        EXPECT_EQ(ReleaseKind::StaticSpan, release.kind);
        EXPECT_EQ(T::native_value_offset_in_words(),
                  release.value_offset_words);
        EXPECT_EQ(T::native_static_release_count(),
                  release.static_release_count);

        const ObjectSizeDescriptor &object_size =
            object_size_descriptor_for(T::native_layout);

        EXPECT_EQ(ObjectSizeKind::StaticSize, object_size.kind);
        EXPECT_EQ(sizeof(T), object_size.static_size_in_bytes);
    }
}  // namespace

TEST(NativeLayoutDescriptor, ListUsesNativeStaticReleaseDescriptor)
{
    expect_static_native_layout_descriptor<List>();
}

TEST(NativeLayoutDescriptor, CustomDeallocDeclarationBuildsDescriptor)
{
    const ReleaseDescriptor release =
        native_layout_descriptor_detail::NativeLayoutReleaseDescriptorBuilder<
            CustomDeallocTestObject>::build();

    EXPECT_EQ(ReleaseKind::CustomDealloc, release.kind);
    EXPECT_EQ(CustomDeallocTestObject::dealloc, release.custom_dealloc);
}

TEST(NativeLayoutDescriptor, ListNativeReleaseCountIncludesInheritedObjectCells)
{
    EXPECT_EQ(Object::native_static_release_count() +
                  ValueArray<Value>::embedded_value_count,
              List::native_static_release_count());
}

TEST(NativeLayoutDescriptor, SlotObjectCarriesAttributeStorageCells)
{
    EXPECT_TRUE(native_layout_has_slots(NativeLayoutId::Instance));
    EXPECT_TRUE(native_layout_has_slots(NativeLayoutId::ClassObject));
    EXPECT_TRUE(native_layout_has_slots(NativeLayoutId::Function));
    EXPECT_TRUE(native_layout_has_slots(NativeLayoutId::Exception));
    EXPECT_TRUE(native_layout_has_slots(NativeLayoutId::StopIteration));
    EXPECT_FALSE(native_layout_has_slots(NativeLayoutId::List));
    EXPECT_EQ(Object::native_static_release_count() + 1,
              SlotObject::native_static_release_count());
}

TEST(NativeLayoutDescriptor, ModuleObjectReleaseCountCoversManagedFields)
{
    EXPECT_EQ(SlotObject::native_static_release_count() +
                  ModuleObject::module_inline_storage_slot_count + 2 +
                  HeapPtrArray<ValidityCell>::embedded_value_count,
              ModuleObject::native_static_release_count());
    expect_static_native_layout_descriptor<ModuleObject>();
}

TEST(NativeLayout, ObjectAndValueConversionHelpersUseExactLayout)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    String *string = context.thread()->make_internal_raw<String>(L"layout");
    Value string_value = Value::from_oop(string);
    Object *object = string_value.get_ptr<Object>();

    EXPECT_EQ(NativeLayoutId::String, object->native_layout_id());
    EXPECT_TRUE(can_convert_to<String>(object));
    EXPECT_FALSE(can_convert_to<Dict>(object));
    EXPECT_EQ(string, try_convert_to<String>(object));
    EXPECT_EQ(nullptr, try_convert_to<Dict>(object));
    EXPECT_EQ(string, assume_convert_to<String>(object));
    EXPECT_TRUE(can_convert_to<String>(string_value));
    EXPECT_EQ(string, try_convert_to<String>(string_value));
    EXPECT_EQ(string, assume_convert_to<String>(string_value));
    EXPECT_EQ(nullptr, try_convert_to<String>(Value::None()));
}

TEST(NativeLayoutDescriptor, ListNativeReleaseSpanStartsAtInheritedObjectCells)
{
    const ReleaseDescriptor &release =
        release_descriptor_for(List::native_layout);

    EXPECT_EQ(Object::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(List::native_static_release_count(),
              release.static_release_count);
}

TEST(NativeLayoutDescriptor, FixedObjectSubclassesUseNativeStaticDescriptors)
{
    expect_static_native_layout_descriptor<RangeIterator>();
    expect_static_native_layout_descriptor<TupleIterator>();
    expect_static_native_layout_descriptor<ListIterator>();
    expect_static_native_layout_descriptor<ExceptionObject>();
    expect_static_native_layout_descriptor<StopIterationObject>();
    expect_static_native_layout_descriptor<Function>();
    expect_static_native_layout_descriptor<Dict>();
    expect_static_native_layout_descriptor<SlotDict>();
    expect_static_native_layout_descriptor<ClassObject>();
}

TEST(NativeLayoutDescriptor, BigIntUsesInheritedObjectReleaseDescriptor)
{
    const ReleaseDescriptor &release =
        release_descriptor_for(BigInt::native_layout);

    EXPECT_EQ(ReleaseKind::StaticSpan, release.kind);
    EXPECT_EQ(Object::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(Object::native_static_release_count(),
              release.static_release_count);

    const ObjectSizeDescriptor &object_size =
        object_size_descriptor_for(BigInt::native_layout);

    EXPECT_EQ(ObjectSizeKind::Custom, object_size.kind);
    ASSERT_NE(nullptr, object_size.custom_size_in_bytes);
}

TEST(NativeLayoutDescriptor, BigIntExactSizedAllocationMapsToIntClass)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    BigInt *bigint =
        make_uninitialized_bigint_for_digits(context.thread(), 3, -1);

    EXPECT_EQ(NativeLayoutId::BigInt, bigint->native_layout_id());
    EXPECT_EQ(context.vm().int_class(), bigint->get_shape()->get_class());
    EXPECT_EQ(3u, bigint->n_digits());
    EXPECT_EQ(-1, bigint->signum());
    EXPECT_EQ(3u, bigint->view().n_digits);
    EXPECT_EQ(-1, bigint->view().signum);
    EXPECT_EQ(3u, bigint->mutable_view_for_initialization().capacity);
    EXPECT_EQ(BigInt::size_for(3), object_size_in_bytes(bigint));
}

TEST(NativeLayoutDescriptor, BigIntZeroAllocationStillHasOneDigitSlot)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    BigInt *bigint =
        make_uninitialized_bigint_for_digits(context.thread(), 0, 0);

    EXPECT_EQ(0u, bigint->n_digits());
    EXPECT_EQ(0, bigint->signum());
    EXPECT_EQ(0u, bigint->view().n_digits);
    EXPECT_EQ(0, bigint->view().signum);
    EXPECT_EQ(1u, bigint->mutable_view_for_initialization().capacity);
    EXPECT_EQ(sizeof(BigInt), BigInt::size_for(0));
    EXPECT_EQ(BigInt::size_for(0), object_size_in_bytes(bigint));
}

TEST(NativeLayoutDescriptor, ValidityCellUsesEmptyStaticDescriptor)
{
    expect_static_native_layout_descriptor<ValidityCell>();
    EXPECT_EQ(0u, ValidityCell::native_static_release_count());
}

TEST(NativeLayoutDescriptor, ScopeUsesNativeStaticReleaseDescriptor)
{
    expect_static_native_layout_descriptor<Scope>();
}

TEST(NativeLayoutDescriptor, ScopeNativeReleaseSpanStartsAtParentScope)
{
    const ReleaseDescriptor &release =
        release_descriptor_for(Scope::native_layout);

    EXPECT_EQ(Scope::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(Scope::native_static_release_count(),
              release.static_release_count);
}

TEST(NativeLayoutDescriptor, StringUsesStaticReleaseAndCustomObjectSize)
{
    const ReleaseDescriptor &release =
        release_descriptor_for(String::native_layout);

    EXPECT_EQ(ReleaseKind::StaticSpan, release.kind);
    EXPECT_EQ(String::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(String::native_static_release_count(),
              release.static_release_count);

    const ObjectSizeDescriptor &object_size =
        object_size_descriptor_for(String::native_layout);

    EXPECT_EQ(ObjectSizeKind::Custom, object_size.kind);
    ASSERT_NE(nullptr, object_size.custom_size_in_bytes);
}

TEST(NativeLayoutDescriptor, StaticObjectSizeQueryUsesDescriptorConstant)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    List *list = context.thread()->make_object_raw<List>();

    EXPECT_EQ(sizeof(List), object_size_in_bytes(list));
}

TEST(NativeLayoutDescriptor, NewObjectSizeReportsCurrentLayoutExtent)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    size_t expected_list_size = sizeof(List);
    List *list = context.thread()->make_object_raw<List>();
    EXPECT_EQ(expected_list_size, object_size_in_bytes(list));

    size_t expected_range_iterator_size = sizeof(RangeIterator);
    RangeIterator *range_iterator =
        context.thread()->make_object_raw<RangeIterator>(
            TValue<SMI>::from_smi(0), TValue<SMI>::from_smi(3),
            TValue<SMI>::from_smi(1));
    EXPECT_EQ(expected_range_iterator_size,
              object_size_in_bytes(range_iterator));

    size_t expected_string_size = String::size_for(5);
    String *str = context.thread()->make_object_raw<String>(L"hello");
    EXPECT_EQ(expected_string_size, object_size_in_bytes(str));

    size_t expected_tuple_size = Tuple::size_for(5);
    Tuple *tuple = context.thread()->make_object_raw<Tuple>(5);
    EXPECT_EQ(expected_tuple_size, object_size_in_bytes(tuple));

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"SizedInstance"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 4, context.vm().object_class(), NativeLayoutId::Instance);
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);
    EXPECT_EQ(Instance::size_for(uint32_t{0}), object_size_in_bytes(instance));
}

TEST(NativeLayoutDescriptor, StringCustomObjectSizeUsesStoredCount)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    String *str = context.thread()->make_object_raw<String>(L"hello");

    const ObjectSizeDescriptor &object_size =
        object_size_descriptor_for(String::native_layout);

    ASSERT_EQ(ObjectSizeKind::Custom, object_size.kind);
    ASSERT_NE(nullptr, object_size.custom_size_in_bytes);
    EXPECT_EQ(String::size_for(5), object_size.custom_size_in_bytes(str));
    EXPECT_EQ(String::size_for(5), object_size_in_bytes(str));
}

TEST(NativeLayoutDescriptor,
     StringNativeReleaseSpanStartsAtInheritedObjectCells)
{
    const ReleaseDescriptor &release =
        release_descriptor_for(String::native_layout);

    EXPECT_EQ(Object::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(String::native_static_release_count(),
              release.static_release_count);
}

TEST(NativeLayoutDescriptor, TupleUsesDynamicSmiReleaseAndCustomObjectSize)
{
    const ReleaseDescriptor &release =
        release_descriptor_for(Tuple::native_layout);

    EXPECT_EQ(ReleaseKind::DynamicSmiSpan, release.kind);
    EXPECT_EQ(Tuple::native_value_count_offset_in_words(),
              release.count_offset_words);
    EXPECT_EQ(Tuple::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(Tuple::native_additional_release_count(),
              release.additional_release_count);
    EXPECT_EQ(Object::native_static_release_count() + 1,
              release.additional_release_count);

    const ObjectSizeDescriptor &object_size =
        object_size_descriptor_for(Tuple::native_layout);

    EXPECT_EQ(ObjectSizeKind::Custom, object_size.kind);
    ASSERT_NE(nullptr, object_size.custom_size_in_bytes);
}

TEST(NativeLayoutDescriptor, CompactTupleDynamicSmiReleaseSpanUsesStoredCount)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Tuple *tuple = context.thread()->make_object_raw<Tuple>(3);
    const ReleaseDescriptor &release =
        release_descriptor_for(Tuple::native_layout);

    Value count_value =
        *(reinterpret_cast<Value *>(tuple) + release.count_offset_words);
    ASSERT_TRUE(count_value.is_smi());
    EXPECT_EQ(3, count_value.get_smi());
    EXPECT_EQ(Object::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(Tuple::native_additional_release_count(),
              release.additional_release_count);
}

TEST(NativeLayoutDescriptor, TupleCustomObjectSizeUsesStoredCount)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Tuple *tuple = context.thread()->make_object_raw<Tuple>(5);

    const ObjectSizeDescriptor &object_size =
        object_size_descriptor_for(Tuple::native_layout);

    ASSERT_EQ(ObjectSizeKind::Custom, object_size.kind);
    ASSERT_NE(nullptr, object_size.custom_size_in_bytes);
    EXPECT_EQ(Tuple::size_for(5), object_size.custom_size_in_bytes(tuple));
    EXPECT_EQ(Tuple::size_for(5), object_size_in_bytes(tuple));
}

TEST(NativeLayoutDescriptor, InstanceUsesDynamicAuxReleaseAndCustomObjectSize)
{
    const ReleaseDescriptor &release =
        release_descriptor_for(Instance::native_layout);

    EXPECT_EQ(ReleaseKind::DynamicAuxSpan, release.kind);
    EXPECT_EQ(Instance::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(Instance::native_additional_release_count(),
              release.additional_release_count);
    EXPECT_EQ(SlotObject::native_static_release_count(),
              release.additional_release_count);

    const ObjectSizeDescriptor &object_size =
        object_size_descriptor_for(Instance::native_layout);

    EXPECT_EQ(ObjectSizeKind::Custom, object_size.kind);
    ASSERT_NE(nullptr, object_size.custom_size_in_bytes);
}

TEST(NativeLayoutDescriptor, InstanceAuxCountTracksInitializedInlineSlots)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TValue<String> small_name(
        context.vm().get_or_create_interned_string_value(L"Small"));
    TValue<String> large_name(
        context.vm().get_or_create_interned_string_value(L"Large"));
    ClassObject *small_cls = context.thread()->make_internal_raw<ClassObject>(
        small_name, 1, context.vm().object_class(), NativeLayoutId::Instance);
    ClassObject *large_cls = context.thread()->make_internal_raw<ClassObject>(
        large_name, 7, context.vm().object_class(), NativeLayoutId::Instance);

    Instance *small = context.thread()->make_internal_raw<Instance>(small_cls);
    Instance *large = context.thread()->make_internal_raw<Instance>(large_cls);

    EXPECT_EQ(0u, small->native_layout_aux_count_value());
    EXPECT_EQ(0u, large->native_layout_aux_count_value());

    const ReleaseDescriptor &release =
        release_descriptor_for(Instance::native_layout);

    EXPECT_EQ(Object::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(Instance::native_additional_release_count(),
              release.additional_release_count);
}

TEST(NativeLayoutDescriptor, InstanceCustomObjectSizeUsesInitializedInlineSlots)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"SizedInstance"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 4, context.vm().object_class(), NativeLayoutId::Instance);
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

    const ObjectSizeDescriptor &object_size =
        object_size_descriptor_for(Instance::native_layout);

    ASSERT_EQ(ObjectSizeKind::Custom, object_size.kind);
    ASSERT_NE(nullptr, object_size.custom_size_in_bytes);
    EXPECT_EQ(0u, instance->native_layout_aux_count_value());
    EXPECT_EQ(Instance::size_for(uint32_t{0}),
              object_size.custom_size_in_bytes(instance));
    EXPECT_EQ(Instance::size_for(uint32_t{0}), object_size_in_bytes(instance));
}

TEST(NativeLayoutDescriptor, InstanceShapeTransitionsDoNotChangeAuxCount)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Transitioned"));
    TValue<String> a_name(
        context.vm().get_or_create_interned_string_value(L"a"));
    TValue<String> b_name(
        context.vm().get_or_create_interned_string_value(L"b"));
    TValue<String> c_name(
        context.vm().get_or_create_interned_string_value(L"c"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 1, context.vm().object_class(), NativeLayoutId::Instance);
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

    ASSERT_EQ(0u, instance->native_layout_aux_count_value());
    ASSERT_TRUE(instance->set_own_property(a_name, Value::from_smi(1)));
    EXPECT_EQ(1u, instance->native_layout_aux_count_value());
    ASSERT_TRUE(instance->set_own_property(b_name, Value::from_smi(2)));
    ASSERT_TRUE(instance->set_own_property(c_name, Value::from_smi(3)));

    EXPECT_EQ(Value::from_smi(1), instance->get_own_property(a_name));
    EXPECT_EQ(Value::from_smi(2), instance->get_own_property(b_name));
    EXPECT_EQ(Value::from_smi(3), instance->get_own_property(c_name));
    EXPECT_EQ(1u, instance->native_layout_aux_count_value());
}

TEST(NativeLayoutDescriptor,
     InstanceOverflowPropertiesDoNotInitializeInlineSlots)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"OverflowOnly"));
    TValue<String> attr_name(
        context.vm().get_or_create_interned_string_value(L"attr"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 0, context.vm().object_class(), NativeLayoutId::Instance);
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

    ASSERT_TRUE(instance->set_own_property(attr_name, Value::from_smi(1)));

    EXPECT_EQ(Value::from_smi(1), instance->get_own_property(attr_name));
    EXPECT_EQ(0u, instance->native_layout_aux_count_value());
}

TEST(NativeLayoutDescriptor, CodeObjectUsesCustomDeallocDescriptor)
{
    const ReleaseDescriptor &release =
        release_descriptor_for(CodeObject::native_layout);

    EXPECT_EQ(ReleaseKind::CustomDealloc, release.kind);
    EXPECT_EQ(CodeObject::dealloc, release.custom_dealloc);

    const ObjectSizeDescriptor &object_size =
        object_size_descriptor_for(CodeObject::native_layout);

    EXPECT_EQ(ObjectSizeKind::StaticSize, object_size.kind);
    EXPECT_EQ(sizeof(CodeObject), object_size.static_size_in_bytes);
}

TEST(NativeLayoutDescriptor, ShapeUsesCustomDeallocAndCustomObjectSize)
{
    const ReleaseDescriptor &release =
        release_descriptor_for(Shape::native_layout);

    EXPECT_EQ(ReleaseKind::CustomDealloc, release.kind);
    EXPECT_EQ(Shape::dealloc, release.custom_dealloc);

    const ObjectSizeDescriptor &object_size =
        object_size_descriptor_for(Shape::native_layout);

    EXPECT_EQ(ObjectSizeKind::Custom, object_size.kind);
    ASSERT_NE(nullptr, object_size.custom_size_in_bytes);
}

TEST(NativeLayoutDescriptor, ShapeNativeObjectSizeUsesPropertyCount)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    ClassObject *cls = context.vm().object_class();
    Shape *shape = context.thread()->make_internal_raw<Shape>(
        TValue<ClassObject>::from_oop(cls), nullptr, 0, 3, 0,
        shape_flag(ShapeFlag::None), 0);

    EXPECT_EQ(Shape::size_for(3), object_size_in_bytes(shape));
}

TEST(NativeLayoutDescriptor, OverflowSlotsUsesDynamicAuxReleaseDescriptor)
{
    const ReleaseDescriptor &release =
        release_descriptor_for(OverflowSlots::native_layout);

    EXPECT_EQ(ReleaseKind::DynamicAuxSpan, release.kind);
    EXPECT_EQ(OverflowSlots::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(0u, release.additional_release_count);

    const ObjectSizeDescriptor &object_size =
        object_size_descriptor_for(OverflowSlots::native_layout);

    EXPECT_EQ(ObjectSizeKind::Custom, object_size.kind);
    ASSERT_NE(nullptr, object_size.custom_size_in_bytes);
}

TEST(NativeLayoutDescriptor, OverflowSlotsNativeReleaseSpanUsesCapacity)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    OverflowSlots *overflow_slots =
        context.thread()->make_internal_raw<OverflowSlots>(2, 5);
    const ReleaseDescriptor &release =
        release_descriptor_for(OverflowSlots::native_layout);

    EXPECT_EQ(OverflowSlots::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(0u, release.additional_release_count);
    EXPECT_EQ(5u, overflow_slots->native_layout_aux_count_value());
}

TEST(NativeLayoutDescriptor, OverflowSlotsNativeObjectSizeUsesCapacity)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    OverflowSlots *overflow_slots =
        context.thread()->make_internal_raw<OverflowSlots>(2, 5);

    EXPECT_EQ(OverflowSlots::size_for(5), object_size_in_bytes(overflow_slots));
}

TEST(NativeLayoutDescriptor, RawArrayBackingUsesEmptyReleaseDescriptor)
{
    const ReleaseDescriptor &release =
        release_descriptor_for(RawArrayBacking::native_layout);

    EXPECT_EQ(ReleaseKind::StaticSpan, release.kind);
    EXPECT_EQ(0u, release.static_release_count);

    const ObjectSizeDescriptor &object_size =
        object_size_descriptor_for(RawArrayBacking::native_layout);

    EXPECT_EQ(ObjectSizeKind::Custom, object_size.kind);
    ASSERT_NE(nullptr, object_size.custom_size_in_bytes);
}

TEST(NativeLayoutDescriptor, RawArrayBackingNativeObjectSizeUsesStorageBytes)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    RawArrayBacking *backing =
        context.thread()->make_internal_raw<RawArrayBacking>(27);

    EXPECT_EQ(RawArrayBacking::size_for(27), object_size_in_bytes(backing));
}

TEST(NativeLayoutDescriptor, ValueArrayBackingUsesDynamicSmiReleaseDescriptor)
{
    const ReleaseDescriptor &release =
        release_descriptor_for(ValueArrayBacking::native_layout);

    EXPECT_EQ(ReleaseKind::DynamicSmiSpan, release.kind);
    EXPECT_EQ(ValueArrayBacking::native_value_count_offset_in_words(),
              release.count_offset_words);
    EXPECT_EQ(ValueArrayBacking::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(0u, release.additional_release_count);

    const ObjectSizeDescriptor &object_size =
        object_size_descriptor_for(ValueArrayBacking::native_layout);

    EXPECT_EQ(ObjectSizeKind::Custom, object_size.kind);
    ASSERT_NE(nullptr, object_size.custom_size_in_bytes);
}

TEST(NativeLayoutDescriptor, ValueArrayBackingNativeReleaseSpanUsesCellCount)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    ValueArrayBacking *backing =
        context.thread()->make_internal_raw<ValueArrayBacking>(7);
    const ReleaseDescriptor &release =
        release_descriptor_for(ValueArrayBacking::native_layout);

    EXPECT_EQ(7u, backing->value_cell_count());
    EXPECT_EQ(ValueArrayBacking::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(ValueArrayBacking::size_for(7), object_size_in_bytes(backing));
}

TEST(NativeLayoutDescriptor, ValueArrayBackingStoresLargeCellCount)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    size_t cell_count =
        static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1;
    ValueArrayBacking *backing =
        context.thread()->make_internal_raw<ValueArrayBacking>(cell_count);

    EXPECT_EQ(cell_count, backing->value_cell_count());
    EXPECT_EQ(0u, backing->native_layout_aux_count_value());
    EXPECT_EQ(ValueArrayBacking::size_for(cell_count),
              object_size_in_bytes(backing));
}

TEST(NativeLayoutDescriptor, HeapPtrArrayBackingUsesDynamicSmiReleaseDescriptor)
{
    const ReleaseDescriptor &release =
        release_descriptor_for(HeapPtrArrayBacking::native_layout);

    EXPECT_EQ(ReleaseKind::DynamicSmiSpan, release.kind);
    EXPECT_EQ(HeapPtrArrayBacking::native_value_count_offset_in_words(),
              release.count_offset_words);
    EXPECT_EQ(HeapPtrArrayBacking::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(0u, release.additional_release_count);

    const ObjectSizeDescriptor &object_size =
        object_size_descriptor_for(HeapPtrArrayBacking::native_layout);

    EXPECT_EQ(ObjectSizeKind::Custom, object_size.kind);
    ASSERT_NE(nullptr, object_size.custom_size_in_bytes);
}

TEST(NativeLayoutDescriptor, HeapPtrArrayBackingNativeReleaseSpanUsesCellCount)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    HeapPtrArrayBacking *backing =
        context.thread()->make_internal_raw<HeapPtrArrayBacking>(3);
    const ReleaseDescriptor &release =
        release_descriptor_for(HeapPtrArrayBacking::native_layout);

    EXPECT_EQ(3u, backing->value_cell_count());
    EXPECT_EQ(HeapPtrArrayBacking::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(reinterpret_cast<Value *>(backing) + release.value_offset_words,
              reinterpret_cast<Value *>(backing->elements));
    EXPECT_EQ(HeapPtrArrayBacking::size_for(3), object_size_in_bytes(backing));
}

TEST(NativeLayoutDescriptor, HeapPtrArrayBackingStoresLargeCellCount)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    size_t cell_count =
        static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1;
    HeapPtrArrayBacking *backing =
        context.thread()->make_internal_raw<HeapPtrArrayBacking>(cell_count);

    EXPECT_EQ(cell_count, backing->value_cell_count());
    EXPECT_EQ(0u, backing->native_layout_aux_count_value());
    EXPECT_EQ(HeapPtrArrayBacking::size_for(cell_count),
              object_size_in_bytes(backing));
}

TEST(NativeLayoutDescriptor, TestOnlyDescriptorsAreInvalid)
{
    size_t test_only_index =
        native_layout_descriptor_detail::native_layout_index(
            NativeLayoutId::TestOnly);
    const auto &release_descriptors =
        native_layout_descriptor_detail::release_descriptors;
    const auto &object_size_descriptors =
        native_layout_descriptor_detail::object_size_descriptors;
    const ReleaseDescriptor &release = release_descriptors[test_only_index];
    const ObjectSizeDescriptor &object_size =
        object_size_descriptors[test_only_index];

    EXPECT_FALSE(
        native_layout_descriptor_detail::release_descriptor_is_valid(release));
    EXPECT_FALSE(
        native_layout_descriptor_detail::object_size_descriptor_is_valid(
            object_size));
}
