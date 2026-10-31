#include "api/extension_handle.h"
#include "builtin_types/bigint.h"
#include "builtin_types/dict.h"
#include "builtin_types/float.h"
#include "builtin_types/list.h"
#include "builtin_types/list_iterator.h"
#include "builtin_types/module_object.h"
#include "builtin_types/range_iterator.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "builtin_types/tuple_iterator.h"
#include "compiler/scope.h"
#include "memory/global_heap.h"
#include "memory/native_layout_descriptor.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "object_model/instance.h"
#include "object_model/object.h"
#include "object_model/overflow_slots.h"
#include "object_model/shape.h"
#include "object_model/slot_dict.h"
#include "object_model/validity_cell.h"
#include "object_model/vm_array.h"
#include "object_model/vm_array_backing.h"
#include "runtime/exception_object.h"
#include "runtime/thread_state.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <limits>

using namespace cl;

namespace
{
    class CustomReleaseTestObject : public HeapObject
    {
    public:
        static void release_managed_references(HeapObject *) {}

        CL_DECLARE_CUSTOM_RELEASE(CustomReleaseTestObject,
                                  release_managed_references);
    };

    class StaticReleaseWithDestroyTestObject : public HeapObject
    {
    public:
        static void destroy(HeapObject *) {}
        static void trace_strong_references(HeapObject *, StrongSlotVisitor) {}
        static void visit_weak_reference_units(HeapObject *, WeakUnitVisitor) {}

        CL_DECLARE_EMPTY_VALUE_SPAN(StaticReleaseWithDestroyTestObject);
        CL_DECLARE_NATIVE_DESTROY(StaticReleaseWithDestroyTestObject, destroy);
        CL_DECLARE_CUSTOM_TRACE_STRONG(StaticReleaseWithDestroyTestObject,
                                       trace_strong_references);
        CL_DECLARE_VISIT_WEAK_UNITS(StaticReleaseWithDestroyTestObject,
                                    visit_weak_reference_units);
    };

    struct TwoValueCells
    {
        Value first;
        Value second;
    };

    template <typename T> void expect_unified_registered_descriptor()
    {
        namespace detail = native_layout_descriptor_detail;
        SCOPED_TRACE(detail::NativeLayoutTraits<T::native_layout>::cpp_name);
        const NativeLayoutDescriptor &descriptor =
            native_layout_descriptor_for(T::native_layout);
        const auto &table_entry =
            detail::native_layout_descriptors[detail::native_layout_index(
                T::native_layout)];
        EXPECT_EQ(&table_entry, &descriptor);

        // Compare every field with the unchanged type-directed builders, not
        // just the combined table's own traits or completeness predicate.
        constexpr ReleaseDescriptor release =
            detail::NativeLayoutReleaseDescriptorBuilder<T>::build();
        EXPECT_EQ(release.kind, descriptor.release.kind);
        EXPECT_EQ(release.count_offset_words,
                  descriptor.release.count_offset_words);
        EXPECT_EQ(release.static_release_count,
                  descriptor.release.static_release_count);
        EXPECT_EQ(release.value_offset_words,
                  descriptor.release.value_offset_words);
        EXPECT_EQ(release.additional_release_count,
                  descriptor.release.additional_release_count);
        EXPECT_EQ(release.custom_release, descriptor.release.custom_release);
        EXPECT_EQ(release.destroy, descriptor.release.destroy);
        EXPECT_EQ(release.custom_trace_strong,
                  descriptor.release.custom_trace_strong);
        EXPECT_EQ(release.visit_weak_units,
                  descriptor.release.visit_weak_units);

        constexpr ObjectSizeDescriptor object_size =
            detail::NativeLayoutObjectSizeDescriptorBuilder<T>::build();
        EXPECT_EQ(object_size.kind, descriptor.object_size.kind);
        EXPECT_EQ(object_size.static_size_in_bytes,
                  descriptor.object_size.static_size_in_bytes);
        EXPECT_EQ(object_size.custom_size_in_bytes,
                  descriptor.object_size.custom_size_in_bytes);
        EXPECT_EQ(detail::native_copy_size_function<T>(),
                  descriptor.custom_copy_size_in_bytes);
    }

    template <typename T> void expect_static_native_layout_descriptor()
    {
        const ReleaseDescriptor &release =
            native_layout_descriptor_for(T::native_layout).release;

        EXPECT_EQ(ReleaseKind::StaticSpan, release.kind);
        EXPECT_EQ(T::native_value_offset_in_words(),
                  release.value_offset_words);
        EXPECT_EQ(T::native_static_release_count(),
                  release.static_release_count);
        EXPECT_EQ(nullptr, release.custom_release);
        EXPECT_EQ(nullptr, release.destroy);
        EXPECT_EQ(nullptr, release.custom_trace_strong);
        EXPECT_EQ(nullptr, release.visit_weak_units);

        const ObjectSizeDescriptor &object_size =
            native_layout_descriptor_for(T::native_layout).object_size;

        EXPECT_EQ(ObjectSizeKind::StaticSize, object_size.kind);
        EXPECT_EQ(sizeof(T), object_size.static_size_in_bytes);
    }
}  // namespace

TEST(NativeLayoutDescriptor, ListUsesNativeStaticReleaseDescriptor)
{
    expect_static_native_layout_descriptor<List>();
}

TEST(NativeLayoutDescriptor, UnifiedTablePreservesEveryRegisteredLayout)
{
#define CL_EXPECT_UNIFIED_DESCRIPTOR(type)                                     \
    expect_unified_registered_descriptor<type>()
    CL_NATIVE_LAYOUT_REGISTRY(CL_EXPECT_UNIFIED_DESCRIPTOR);
#undef CL_EXPECT_UNIFIED_DESCRIPTOR
}

TEST(NativeLayoutDescriptor, UnifiedTableHasNoUnexpectedPadding)
{
    EXPECT_EQ(88u, sizeof(NativeLayoutDescriptor));
    EXPECT_EQ(
        native_layout_descriptor_count() *
            (sizeof(ReleaseDescriptor) + sizeof(ObjectSizeDescriptor) +
             sizeof(size_t (*)(const HeapObject *))),
        sizeof(native_layout_descriptor_detail::native_layout_descriptors));
}

TEST(NativeLayoutDescriptor, LayoutInfoCopiesCompleteFixedObject)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Float *object = context.thread()->make_object_raw<Float>(1.5);
    int32_t refcount = object->refcount;
    NativeLayoutInfo info = native_layout_info(object);
    EXPECT_EQ(sizeof(Float), info.allocated_size);
    EXPECT_EQ(sizeof(Float), info.copy_size);
    ASSERT_TRUE(info.strong_span.has_value());
    EXPECT_EQ(Float::native_value_offset_in_words(),
              info.strong_span->value_offset_words);
    EXPECT_EQ(Float::native_static_release_count(),
              info.strong_span->value_count);
    EXPECT_EQ(refcount, object->refcount);
    EXPECT_EQ(nullptr, native_layout_descriptor_for(Float::native_layout)
                           .custom_copy_size_in_bytes);
}

TEST(NativeLayoutDescriptor, LayoutInfoKeepsCustomScanningDistinctFromEmpty)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    CodeObject *code = context.compile_file(L"a = 1\n");
    ASSERT_NE(nullptr, code);
    NativeLayoutInfo info = native_layout_info(code);
    EXPECT_EQ(sizeof(CodeObject), info.allocated_size);
    EXPECT_EQ(sizeof(CodeObject), info.copy_size);
    EXPECT_FALSE(info.strong_span.has_value());
    EXPECT_NE(nullptr, native_layout_descriptor_for(CodeObject::native_layout)
                           .release.custom_trace_strong);
}

TEST(NativeLayoutDescriptor, LayoutInfoRawBackingCopiesAllReservedBytes)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    RawArray<uint64_t> array;
    array.reserve(16);
    array.push_back(uint64_t{42});
    // Follow the owner slot to the real live backing without constructing or
    // reading any of the spare raw elements.
    HeapObject *backing = nullptr;
    array.visit_strong_reference_slots(
        [&](HeapReferenceSlot slot) { backing = slot.load_pointer(); });
    ASSERT_NE(nullptr, backing);
    NativeLayoutInfo info = native_layout_info(backing);
    EXPECT_EQ(RawArrayBacking::allocation_size_for(16 * sizeof(uint64_t)),
              info.allocated_size);
    EXPECT_EQ(info.allocated_size, info.copy_size);
    ASSERT_TRUE(info.strong_span.has_value());
    EXPECT_EQ(0u, info.strong_span->value_count);
    EXPECT_EQ(42u, array[0]);
    array.release_refs();
}

TEST(NativeLayoutDescriptor, LayoutInfoEmptyRawBackingPreservesItsFullExtent)
{
    RawArrayBacking backing(0);
    NativeLayoutInfo info = native_layout_info(&backing);
    EXPECT_EQ(RawArrayBacking::allocation_size_for(0), info.allocated_size);
    EXPECT_EQ(info.allocated_size, info.copy_size);
    ASSERT_TRUE(info.strong_span.has_value());
    EXPECT_EQ(0u, info.strong_span->value_count);
}

TEST(NativeLayoutDescriptor, LayoutInfoTupleExcludesOnlyUnusedPlaceholder)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    for(size_t count: {size_t{0}, size_t{1}, size_t{3}})
    {
        Tuple *tuple = context.thread()->make_object_raw<Tuple>(count);
        NativeLayoutInfo info = native_layout_info(tuple);
        EXPECT_EQ(Tuple::allocation_size_for(count), info.allocated_size);
        EXPECT_EQ(info.allocated_size - (count == 0 ? sizeof(Value) : 0),
                  info.copy_size);
        ASSERT_TRUE(info.strong_span.has_value());
        EXPECT_EQ(Tuple::native_additional_release_count() + count,
                  info.strong_span->value_count);
    }
}

TEST(NativeLayoutDescriptor, LayoutInfoOverflowPreservesInitializedCapacity)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    for(uint32_t capacity: {0u, 1u, 5u})
    {
        OverflowSlots *slots =
            context.thread()->make_internal_raw<OverflowSlots>(0, capacity);
        NativeLayoutInfo info = native_layout_info(slots);
        EXPECT_EQ(OverflowSlots::allocation_size_for(capacity),
                  info.allocated_size);
        EXPECT_EQ(info.allocated_size - (capacity == 0 ? sizeof(Value) : 0),
                  info.copy_size);
        ASSERT_TRUE(info.strong_span.has_value());
        EXPECT_EQ(capacity, info.strong_span->value_count);
        // Logical size is zero, but every capacity cell was initialized.
        for(uint32_t index = 0; index < capacity; ++index)
        {
            EXPECT_TRUE(slots->get(index).is_not_present());
        }
    }
}

TEST(NativeLayoutDescriptor, LayoutInfoReferenceBackingsPreserveWideCounts)
{
    for(size_t count: {size_t{0}, size_t{1}, size_t{5}, size_t{UINT32_MAX} + 1})
    {
        // Querying a layout reads count/header fields only. Large-count cases
        // do not allocate or inspect the claimed payload, as in span tests.
        ValueArrayBacking values(count);
        HeapPtrArrayBacking pointers(count);
        for(HeapObject *backing: {static_cast<HeapObject *>(&values),
                                  static_cast<HeapObject *>(&pointers)})
        {
            NativeLayoutInfo info = native_layout_info(backing);
            EXPECT_EQ(object_size_in_bytes(backing), info.allocated_size);
            EXPECT_EQ(info.allocated_size - (count == 0 ? sizeof(Value) : 0),
                      info.copy_size);
            ASSERT_TRUE(info.strong_span.has_value());
            EXPECT_EQ(count, info.strong_span->value_count);
        }
    }
}

TEST(NativeLayoutDescriptor, UnifiedCompletenessChecksBothPartsAndCustomScanner)
{
    namespace detail = native_layout_descriptor_detail;
    auto descriptors = detail::native_layout_descriptors;
    size_t index = detail::native_layout_index(NativeLayoutId::Float);
    NativeLayoutDescriptor original = descriptors[index];
    EXPECT_TRUE(
        detail::native_layout_descriptor_table_is_complete(descriptors));

    descriptors[index].object_size = ObjectSizeDescriptor::custom(nullptr);
    EXPECT_FALSE(
        detail::native_layout_descriptor_table_is_complete(descriptors));
    descriptors[index] = original;
    descriptors[index].release = ReleaseDescriptor::custom(nullptr);
    EXPECT_FALSE(
        detail::native_layout_descriptor_table_is_complete(descriptors));
    descriptors[index].release = ReleaseDescriptor::custom(
        CustomReleaseTestObject::release_managed_references);
    EXPECT_FALSE(
        detail::native_layout_descriptor_table_is_complete(descriptors));
    descriptors[index].release.custom_trace_strong =
        StaticReleaseWithDestroyTestObject::trace_strong_references;
    EXPECT_TRUE(
        detail::native_layout_descriptor_table_is_complete(descriptors));
}

#ifndef NDEBUG
TEST(NativeLayoutDescriptor, UnifiedLookupRejectsInvalidTestOnlyAndOutOfRange)
{
    EXPECT_DEATH((void)native_layout_descriptor_for(NativeLayoutId::Invalid),
                 "native_layout");
    EXPECT_DEATH((void)native_layout_descriptor_for(NativeLayoutId::TestOnly),
                 "native_layout_descriptor_is_valid");
    EXPECT_DEATH((void)native_layout_descriptor_for(NativeLayoutId::Count),
                 "native_layout_index");
}
#endif

TEST(NativeLayoutDescriptor, CustomReleaseDeclarationBuildsDescriptor)
{
    const ReleaseDescriptor release =
        native_layout_descriptor_detail::NativeLayoutReleaseDescriptorBuilder<
            CustomReleaseTestObject>::build();

    EXPECT_EQ(ReleaseKind::CustomRelease, release.kind);
    EXPECT_EQ(CustomReleaseTestObject::release_managed_references,
              release.custom_release);
    EXPECT_EQ(nullptr, release.destroy);
    EXPECT_EQ(nullptr, release.custom_trace_strong);
}

TEST(NativeLayoutDescriptor,
     OrdinaryReleaseCanHaveIndependentScanningAndDestruction)
{
    const ReleaseDescriptor release =
        native_layout_descriptor_detail::NativeLayoutReleaseDescriptorBuilder<
            StaticReleaseWithDestroyTestObject>::build();

    EXPECT_EQ(ReleaseKind::StaticSpan, release.kind);
    EXPECT_EQ(nullptr, release.custom_release);
    EXPECT_EQ(StaticReleaseWithDestroyTestObject::destroy, release.destroy);
    EXPECT_EQ(StaticReleaseWithDestroyTestObject::trace_strong_references,
              release.custom_trace_strong);
    EXPECT_EQ(StaticReleaseWithDestroyTestObject::visit_weak_reference_units,
              release.visit_weak_units);
}

TEST(NativeLayoutDescriptor, ResolvesOrdinaryStaticReferenceSpan)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Float *object = context.thread()->make_object_raw<Float>(1.5);
    int32_t refcount = object->refcount;
    auto span = resolve_ordinary_reference_span(
        object, native_layout_descriptor_for(Float::native_layout).release);

    ASSERT_TRUE(span.has_value());
    EXPECT_EQ(Object::native_value_offset_in_words(), span->value_offset_words);
    EXPECT_EQ(Object::native_static_release_count(), span->value_count);
    EXPECT_EQ(refcount, object->refcount);
}

TEST(NativeLayoutDescriptor, EmptyOrdinarySpanIsSupported)
{
    RawArrayBacking backing(0);
    auto span = resolve_ordinary_reference_span(
        &backing,
        native_layout_descriptor_for(RawArrayBacking::native_layout).release);

    ASSERT_TRUE(span.has_value());
    EXPECT_EQ(0u, span->value_offset_words);
    EXPECT_EQ(0u, span->value_count);
}

TEST(NativeLayoutDescriptor, OrdinarySpanPreservesLargeStartingOffset)
{
    // Resolve metadata only; no storage at this offset needs to be accessed.
    HeapObject header(NativeLayoutId::TestOnly);
    uint64_t offset = uint64_t{UINT32_MAX} + 1;
    ReleaseDescriptor descriptor = ReleaseDescriptor::static_span(offset, 0);
    auto span = resolve_ordinary_reference_span(&header, descriptor);

    ASSERT_TRUE(span.has_value());
    EXPECT_EQ(offset, span->value_offset_words);
    EXPECT_EQ(0u, span->value_count);
}

TEST(NativeLayoutDescriptor, ResolvesSmiReferenceCountWithoutNarrowing)
{
    // The query reads only the stored count, not the unallocated trailing
    // cells.
    uint64_t count = uint64_t{UINT32_MAX} + 1;
    ValueArrayBacking backing(count);
    auto span = resolve_ordinary_reference_span(
        &backing,
        native_layout_descriptor_for(ValueArrayBacking::native_layout).release);

    ASSERT_TRUE(span.has_value());
    EXPECT_EQ(ValueArrayBacking::native_value_offset_in_words(),
              span->value_offset_words);
    EXPECT_EQ(count, span->value_count);
}

TEST(NativeLayoutDescriptor, CustomReleaseDoesNotResolveAsAnEmptySpan)
{
    for(NativeLayoutId layout:
        {NativeLayoutId::CodeObject, NativeLayoutId::Shape,
         NativeLayoutId::JitCodeObject})
    {
        SCOPED_TRACE(static_cast<uint32_t>(layout));
        // Custom handling must not inspect payload fields or run a deallocator.
        HeapObject header(layout);
        EXPECT_FALSE(resolve_ordinary_reference_span(
                         &header, native_layout_descriptor_for(layout).release)
                         .has_value());
    }
}

TEST(NativeLayoutDescriptor, ListNativeReleaseCountIncludesInheritedObjectCells)
{
    EXPECT_EQ(Object::native_static_release_count() +
                  ValueArray<Value>::embedded_value_count,
              List::native_static_release_count());
}

TEST(NativeLayoutDescriptor, ObjectCarriesSharedOverflowStorage)
{
    EXPECT_TRUE(native_layout_has_inline_slots(NativeLayoutId::Instance));
    EXPECT_TRUE(native_layout_has_inline_slots(NativeLayoutId::ClassObject));
    EXPECT_TRUE(native_layout_has_inline_slots(NativeLayoutId::Function));
    EXPECT_TRUE(native_layout_has_inline_slots(NativeLayoutId::Exception));
    EXPECT_TRUE(native_layout_has_inline_slots(NativeLayoutId::StopIteration));
    EXPECT_FALSE(native_layout_has_inline_slots(NativeLayoutId::List));
    EXPECT_EQ(Object::native_static_release_count(),
              InlineSlotObject::native_static_release_count());
}

TEST(NativeLayoutDescriptor, NonSlotBuiltinClassesHaveZeroInlineCapacity)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    for(uint32_t idx = 1; idx < static_cast<uint32_t>(NativeLayoutId::Count);
        ++idx)
    {
        NativeLayoutId layout = static_cast<NativeLayoutId>(idx);
        ClassObject *cls = context.vm().class_for_native_layout(layout);
        if(cls == nullptr || native_layout_has_inline_slots(layout))
        {
            continue;
        }
        SCOPED_TRACE(idx);
        EXPECT_EQ(0u, cls->get_instance_inline_slot_capacity());
        EXPECT_EQ(0u,
                  cls->get_instance_root_shape()->get_inline_slot_capacity());
    }
}

TEST(NativeLayoutDescriptor, InlineCapacityRespectsPredefinedAndPhysicalBounds)
{
    EXPECT_FALSE(
        native_inline_slot_capacity_is_valid(NativeLayoutId::Exception, 0));
    EXPECT_TRUE(
        native_inline_slot_capacity_is_valid(NativeLayoutId::Exception, 1));
    EXPECT_FALSE(
        native_inline_slot_capacity_is_valid(NativeLayoutId::Exception, 2));
    EXPECT_FALSE(
        native_inline_slot_capacity_is_valid(NativeLayoutId::StopIteration, 1));
    EXPECT_TRUE(
        native_inline_slot_capacity_is_valid(NativeLayoutId::StopIteration, 2));
    EXPECT_FALSE(
        native_inline_slot_capacity_is_valid(NativeLayoutId::ClassObject, 4));
    EXPECT_TRUE(
        native_inline_slot_capacity_is_valid(NativeLayoutId::ClassObject, 5));
    EXPECT_TRUE(
        native_inline_slot_capacity_is_valid(NativeLayoutId::ClassObject, 48));
    EXPECT_FALSE(
        native_inline_slot_capacity_is_valid(NativeLayoutId::ClassObject, 49));
    EXPECT_TRUE(native_inline_slot_capacity_is_valid(NativeLayoutId::Instance,
                                                     UINT16_MAX));
    EXPECT_FALSE(native_inline_slot_capacity_is_valid(
        NativeLayoutId::Instance, uint32_t{UINT16_MAX} + 1));
}

#ifndef NDEBUG
TEST(NativeLayoutDescriptor, CachedSliceAllocationRejectsInvalidInlineCapacity)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TValue<ClassObject> cls =
        TValue<ClassObject>::from_oop(context.vm().slice_class());
    for(uint32_t capacity: {2u, 4u})
    {
        OwnedHeapPtr<Shape> shape(Shape::make_root_with_descriptors(
            cls, nullptr, 0, 0, 0, capacity, immutable_shape_flags()));
        EXPECT_DEATH(
            context.thread()->make_object_raw_with_initial_shape<Slice>(
                shape.extract(), Value::None(), Value::None(), Value::None()),
            "native_inline_slot_capacity_is_valid");
    }
}

TEST(NativeLayoutDescriptor, ShapeInstallationRejectsFixedCapacityOverflow)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Owned<TValue<Slice>> slice(
        context.thread()->make_object_value_with_initial_shape<Slice>(
            context.vm().slice_class()->get_instance_root_shape(),
            Value::None(), Value::None(), Value::None()));
    OwnedHeapPtr<Shape> shape(Shape::make_root_with_descriptors(
        TValue<ClassObject>::from_oop(context.vm().slice_class()), nullptr, 0,
        0, 0, 4, immutable_shape_flags()));
    EXPECT_DEATH(slice.extract()->set_shape(shape.extract()),
                 "native_inline_slot_capacity_is_valid");
}

TEST(NativeLayoutDescriptor, DynamicInstanceCapacityIsCheckedBeforeAllocation)
{
    EXPECT_DEATH(Instance::allocation_size_for(uint32_t{UINT16_MAX} + 1),
                 "dynamic_inline_slot_count <= UINT16_MAX");
}
#endif

TEST(NativeLayoutDescriptor, ModuleObjectReleaseCountCoversManagedFields)
{
    EXPECT_EQ(InlineSlotObject::native_static_release_count() +
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
    EXPECT_TRUE(has_native_layout<String>(object));
    EXPECT_FALSE(has_native_layout<Dict>(object));
    EXPECT_EQ(string, try_native_cast<String>(object));
    EXPECT_EQ(nullptr, try_native_cast<Dict>(object));
    EXPECT_EQ(string, native_cast<String>(object));
    EXPECT_TRUE(has_native_layout<String>(string_value));
    EXPECT_EQ(string, try_native_cast<String>(string_value));
    EXPECT_EQ(string, native_cast<String>(string_value));
    EXPECT_EQ(nullptr, try_native_cast<String>(Value::None()));
}

TEST(NativeLayoutDescriptor, ListNativeReleaseSpanStartsAtInheritedObjectCells)
{
    const ReleaseDescriptor &release =
        native_layout_descriptor_for(List::native_layout).release;

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
        native_layout_descriptor_for(BigInt::native_layout).release;

    EXPECT_EQ(ReleaseKind::StaticSpan, release.kind);
    EXPECT_EQ(Object::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(Object::native_static_release_count(),
              release.static_release_count);

    const ObjectSizeDescriptor &object_size =
        native_layout_descriptor_for(BigInt::native_layout).object_size;

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
    EXPECT_EQ(BigInt::allocation_size_for(3), object_size_in_bytes(bigint));
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
    EXPECT_EQ(sizeof(BigInt), BigInt::allocation_size_for(0));
    EXPECT_EQ(BigInt::allocation_size_for(0), object_size_in_bytes(bigint));
}

TEST(NativeLayoutDescriptor, ValidityCellUsesEmptyStaticDescriptor)
{
    expect_static_native_layout_descriptor<ValidityCell>();
    EXPECT_EQ(0u, ValidityCell::native_static_release_count());
}

TEST(NativeLayoutDescriptor, HandleChunkUsesStaticManagedCellSpan)
{
    if constexpr(!native_handle_detail::cl_indirect_handles)
    {
        GTEST_SKIP() << "indirect handles are disabled";
    }

    expect_static_native_layout_descriptor<HandleChunk>();
    EXPECT_EQ(HandleChunk::CellCount,
              HandleChunk::native_static_release_count());
}

TEST(NativeHandle, ArgumentHandleAliasesOriginalRootedSlot)
{
    if constexpr(!native_handle_detail::cl_indirect_handles)
    {
        GTEST_SKIP() << "indirect handles are disabled";
    }

    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Value slot = Value::from_smi(1);
    clover_context handle_context{context.thread(), nullptr, nullptr, false};

    clover_handle handle = handle_from_rooted_slot(&handle_context, &slot);

    EXPECT_EQ(reinterpret_cast<uintptr_t>(&slot), handle);
    EXPECT_EQ(Value::from_smi(1), resolve_handle(handle));
    slot = Value::from_smi(2);
    EXPECT_EQ(Value::from_smi(2), resolve_handle(handle));
}

TEST(NativeHandle, OverflowStorageRetainsValuesButFrameStorageDoesNot)
{
    if constexpr(!native_handle_detail::cl_indirect_handles)
    {
        GTEST_SKIP() << "indirect handles are disabled";
    }

    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Value frame_cells[native_handle_detail::frame_handle_cell_count];
    for(Value &cell: frame_cells)
    {
        cell = Value::not_present();
    }
    clover_context handle_context{
        context.thread(), frame_cells,
        frame_cells + native_handle_detail::frame_handle_cell_count - 1, false};

    Float *frame_float =
        context.thread()->make_object_value<Float>(1.5).extract();
    EXPECT_EQ(0, frame_float->refcount);
    (void)allocate_handle(&handle_context, Value::from_oop(frame_float));
    EXPECT_EQ(0, frame_float->refcount);

    for(size_t idx = 1; idx < native_handle_detail::frame_handle_cell_count - 1;
        ++idx)
    {
        (void)allocate_handle(&handle_context, Value::from_smi(int64_t(idx)));
    }
    ASSERT_TRUE(handle_context.handle_chunk_is_overflow);
    ASSERT_TRUE(frame_cells[native_handle_detail::frame_handle_cell_count - 1]
                    .is_refcounted_ptr());
    EXPECT_EQ(NativeLayoutId::HandleChunk,
              frame_cells[native_handle_detail::frame_handle_cell_count - 1]
                  .as.ptr->native_layout_id());

    Float *overflow_float =
        context.thread()->make_object_value<Float>(2.5).extract();
    EXPECT_EQ(0, overflow_float->refcount);
    clover_handle overflow_handle =
        allocate_handle(&handle_context, Value::from_oop(overflow_float));
    EXPECT_EQ(1, overflow_float->refcount);
    EXPECT_EQ(Value::from_oop(overflow_float), resolve_handle(overflow_handle));
}

TEST(NativeHandle, PublishedFrameAndOverflowHandlesSurviveReclamation)
{
    if constexpr(!native_handle_detail::cl_indirect_handles)
    {
        GTEST_SKIP() << "indirect handles are disabled";
    }

    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation_scope(thread);
    context.vm().run_heap_reclamation();

    Float *frame_float = nullptr;
    Float *overflow_float = nullptr;
    HandleChunk *first_chunk = nullptr;
    {
        NativeHandleRootRegion root_region(thread);
        clover_context handle_context = root_region.make_context();
        Value *frame_cells = thread->clover_frame_frontier();

        frame_float = thread->make_object_value<Float>(1.5).extract();
        clover_handle frame_handle =
            allocate_handle(&handle_context, Value::from_oop(frame_float));
        thread->add_to_zero_count_table_if_needed(frame_float);

        for(size_t idx = 1;
            idx < native_handle_detail::frame_handle_cell_count - 1; ++idx)
        {
            (void)allocate_handle(&handle_context,
                                  Value::from_smi(int64_t(idx)));
        }
        Value chunk_value =
            *(thread->clover_frame_frontier() +
              native_handle_detail::frame_handle_cell_count - 1);
        ASSERT_TRUE(chunk_value.is_refcounted_ptr());
        first_chunk = static_cast<HandleChunk *>(chunk_value.as.ptr);
        thread->add_to_zero_count_table_if_needed(first_chunk);

        overflow_float = thread->make_object_value<Float>(2.5).extract();
        clover_handle overflow_handle =
            allocate_handle(&handle_context, Value::from_oop(overflow_float));

        context.vm().run_heap_reclamation();

        EXPECT_EQ(Value::from_oop(frame_float), resolve_handle(frame_handle));
        EXPECT_EQ(Value::from_oop(overflow_float),
                  resolve_handle(overflow_handle));
        EXPECT_TRUE(thread->zero_count_table_contains_for_testing(frame_float));
        EXPECT_TRUE(thread->zero_count_table_contains_for_testing(first_chunk));
        EXPECT_DOUBLE_EQ(1.5, frame_float->value());
        EXPECT_DOUBLE_EQ(2.5, overflow_float->value());

        // Explicitly discard our roots: a restored conservative scan range may
        // still cover these cells after the handle region ends.
        for(size_t idx = 0; idx < native_handle_detail::frame_handle_cell_count;
            ++idx)
            frame_cells[idx] = Value::not_present();
    }

    context.vm().run_heap_reclamation();

    EXPECT_FALSE(thread->zero_count_table_contains_for_testing(frame_float));
    EXPECT_FALSE(thread->zero_count_table_contains_for_testing(first_chunk));
    EXPECT_FALSE(thread->zero_count_table_contains_for_testing(overflow_float));
}

TEST(NativeLayoutDescriptor, ScopeUsesNativeStaticReleaseDescriptor)
{
    expect_static_native_layout_descriptor<Scope>();
}

TEST(NativeLayoutDescriptor, ScopeNativeReleaseSpanStartsAtParentScope)
{
    const ReleaseDescriptor &release =
        native_layout_descriptor_for(Scope::native_layout).release;

    EXPECT_EQ(Scope::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(Scope::native_static_release_count(),
              release.static_release_count);
}

TEST(NativeLayoutDescriptor, StringUsesStaticReleaseAndCustomObjectSize)
{
    const ReleaseDescriptor &release =
        native_layout_descriptor_for(String::native_layout).release;

    EXPECT_EQ(ReleaseKind::StaticSpan, release.kind);
    EXPECT_EQ(String::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(String::native_static_release_count(),
              release.static_release_count);

    const ObjectSizeDescriptor &object_size =
        native_layout_descriptor_for(String::native_layout).object_size;

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

    size_t expected_string_size = String::allocation_size_for(5);
    String *str = context.thread()->make_object_raw<String>(L"hello");
    EXPECT_EQ(expected_string_size, object_size_in_bytes(str));

    size_t expected_tuple_size = Tuple::allocation_size_for(5);
    Tuple *tuple = context.thread()->make_object_raw<Tuple>(5);
    EXPECT_EQ(expected_tuple_size, object_size_in_bytes(tuple));

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"SizedInstance"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 4, context.vm().object_class(), NativeLayoutId::Instance);
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);
    EXPECT_EQ(Instance::allocation_size_for(uint32_t{4}),
              object_size_in_bytes(instance));
}

TEST(NativeLayoutDescriptor, StringCustomObjectSizeUsesStoredCount)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    String *str = context.thread()->make_object_raw<String>(L"hello");

    const ObjectSizeDescriptor &object_size =
        native_layout_descriptor_for(String::native_layout).object_size;

    ASSERT_EQ(ObjectSizeKind::Custom, object_size.kind);
    ASSERT_NE(nullptr, object_size.custom_size_in_bytes);
    EXPECT_EQ(String::allocation_size_for(5),
              object_size.custom_size_in_bytes(str));
    EXPECT_EQ(String::allocation_size_for(5), object_size_in_bytes(str));
}

TEST(NativeLayoutDescriptor,
     StringNativeReleaseSpanStartsAtInheritedObjectCells)
{
    const ReleaseDescriptor &release =
        native_layout_descriptor_for(String::native_layout).release;

    EXPECT_EQ(Object::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(String::native_static_release_count(),
              release.static_release_count);
}

TEST(NativeLayoutDescriptor, TupleUsesDynamicSmiReleaseAndCustomObjectSize)
{
    const ReleaseDescriptor &release =
        native_layout_descriptor_for(Tuple::native_layout).release;

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
        native_layout_descriptor_for(Tuple::native_layout).object_size;

    EXPECT_EQ(ObjectSizeKind::Custom, object_size.kind);
    ASSERT_NE(nullptr, object_size.custom_size_in_bytes);
}

TEST(NativeLayoutDescriptor, CompactTupleDynamicSmiReleaseSpanUsesStoredCount)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Tuple *tuple = context.thread()->make_object_raw<Tuple>(3);
    const ReleaseDescriptor &release =
        native_layout_descriptor_for(Tuple::native_layout).release;

    Value count_value =
        *(reinterpret_cast<Value *>(tuple) + release.count_offset_words);
    ASSERT_TRUE(count_value.is_smi());
    EXPECT_EQ(3, count_value.get_smi());
    EXPECT_EQ(Object::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(Tuple::native_additional_release_count(),
              release.additional_release_count);

    auto span = resolve_ordinary_reference_span(tuple, release);
    ASSERT_TRUE(span.has_value());
    EXPECT_EQ(Object::native_value_offset_in_words(), span->value_offset_words);
    EXPECT_EQ(3u + Object::native_static_release_count() + 1,
              span->value_count);
}

TEST(NativeLayoutDescriptor, TupleCustomObjectSizeUsesStoredCount)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Tuple *tuple = context.thread()->make_object_raw<Tuple>(5);

    const ObjectSizeDescriptor &object_size =
        native_layout_descriptor_for(Tuple::native_layout).object_size;

    ASSERT_EQ(ObjectSizeKind::Custom, object_size.kind);
    ASSERT_NE(nullptr, object_size.custom_size_in_bytes);
    EXPECT_EQ(Tuple::allocation_size_for(5),
              object_size.custom_size_in_bytes(tuple));
    EXPECT_EQ(Tuple::allocation_size_for(5), object_size_in_bytes(tuple));
}

TEST(NativeLayoutDescriptor, InstanceUsesDynamicAuxReleaseAndCustomObjectSize)
{
    const ReleaseDescriptor &release =
        native_layout_descriptor_for(Instance::native_layout).release;

    EXPECT_EQ(ReleaseKind::DynamicAuxSpan, release.kind);
    EXPECT_EQ(Instance::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(Instance::native_additional_release_count(),
              release.additional_release_count);
    EXPECT_EQ(InlineSlotObject::native_static_release_count(),
              release.additional_release_count);

    const ObjectSizeDescriptor &object_size =
        native_layout_descriptor_for(Instance::native_layout).object_size;

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
        native_layout_descriptor_for(Instance::native_layout).release;

    EXPECT_EQ(Object::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(Instance::native_additional_release_count(),
              release.additional_release_count);

    for(Instance *instance: {small, large})
    {
        auto span = resolve_ordinary_reference_span(instance, release);
        ASSERT_TRUE(span.has_value());
        EXPECT_EQ(Object::native_value_offset_in_words(),
                  span->value_offset_words);
        EXPECT_EQ(InlineSlotObject::native_static_release_count(),
                  span->value_count);
        NativeLayoutInfo info = native_layout_info(instance);
        EXPECT_EQ(object_size_in_bytes(instance), info.allocated_size);
        EXPECT_EQ(sizeof(Instance), info.copy_size);
        ASSERT_TRUE(info.strong_span.has_value());
        EXPECT_EQ(span->value_offset_words,
                  info.strong_span->value_offset_words);
        EXPECT_EQ(span->value_count, info.strong_span->value_count);
    }

    ASSERT_TRUE(
        large->set_own_property(small_name, Value::from_smi(1)).value());
    auto span = resolve_ordinary_reference_span(large, release);
    ASSERT_TRUE(span.has_value());
    // Seven cells are reserved, but only the initialized cell joins the span.
    EXPECT_EQ(InlineSlotObject::native_static_release_count() + 1,
              span->value_count);
    NativeLayoutInfo info = native_layout_info(large);
    EXPECT_EQ(Instance::allocation_size_for(uint32_t{7}), info.allocated_size);
    EXPECT_EQ(sizeof(Instance) + sizeof(Value), info.copy_size);
    ASSERT_TRUE(info.strong_span.has_value());
    EXPECT_EQ(span->value_count, info.strong_span->value_count);
}

TEST(NativeLayoutDescriptor, InstanceCustomObjectSizeUsesShapeCapacity)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"SizedInstance"));
    const ObjectSizeDescriptor &object_size =
        native_layout_descriptor_for(Instance::native_layout).object_size;

    ASSERT_EQ(ObjectSizeKind::Custom, object_size.kind);
    ASSERT_NE(nullptr, object_size.custom_size_in_bytes);
    for(uint32_t capacity: {0u, 1u, 4u})
    {
        ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
            cls_name, capacity, context.vm().object_class(),
            NativeLayoutId::Instance);
        Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

        EXPECT_EQ(0u, instance->native_layout_aux_count_value());
        EXPECT_EQ(Instance::allocation_size_for(capacity),
                  object_size.custom_size_in_bytes(instance));
        EXPECT_EQ(Instance::allocation_size_for(capacity),
                  object_size_in_bytes(instance));
    }
}

TEST(NativeLayoutDescriptor, InstanceSizeStaysFixedAsInitializedSlotsChange)
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

    size_t allocated_size = Instance::allocation_size_for(uint32_t{1});
    ASSERT_EQ(0u, instance->native_layout_aux_count_value());
    EXPECT_EQ(allocated_size, object_size_in_bytes(instance));
    ASSERT_TRUE(instance->set_own_property(a_name, Value::from_smi(1)).value());
    EXPECT_EQ(1u, instance->native_layout_aux_count_value());
    EXPECT_EQ(allocated_size, object_size_in_bytes(instance));
    ASSERT_TRUE(instance->set_own_property(b_name, Value::from_smi(2)).value());
    ASSERT_TRUE(instance->set_own_property(c_name, Value::from_smi(3)).value());

    EXPECT_EQ(Value::from_smi(1), instance->get_own_property(a_name));
    EXPECT_EQ(Value::from_smi(2), instance->get_own_property(b_name));
    EXPECT_EQ(Value::from_smi(3), instance->get_own_property(c_name));
    EXPECT_EQ(1u, instance->native_layout_aux_count_value());
    EXPECT_EQ(allocated_size, object_size_in_bytes(instance));

    ASSERT_TRUE(instance->delete_own_property(a_name));
    ASSERT_TRUE(instance->delete_own_property(b_name));
    EXPECT_TRUE(instance->inline_slot_base()[0].is_not_present());
    EXPECT_TRUE(instance->get_own_property(a_name).is_not_present());
    EXPECT_TRUE(instance->get_own_property(b_name).is_not_present());
    EXPECT_EQ(1u, instance->native_layout_aux_count_value());
    EXPECT_EQ(allocated_size, object_size_in_bytes(instance));
    NativeLayoutInfo info = native_layout_info(instance);
    EXPECT_EQ(allocated_size, info.allocated_size);
    // Deleted inline slots remain initialized; overflow properties do not
    // extend the inline copy prefix.
    EXPECT_EQ(sizeof(Instance) + sizeof(Value), info.copy_size);
    ASSERT_TRUE(info.strong_span.has_value());
    EXPECT_EQ(InlineSlotObject::native_static_release_count() + 1,
              info.strong_span->value_count);
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

    ASSERT_TRUE(
        instance->set_own_property(attr_name, Value::from_smi(1)).value());

    EXPECT_EQ(Value::from_smi(1), instance->get_own_property(attr_name));
    EXPECT_EQ(0u, instance->native_layout_aux_count_value());
    NativeLayoutInfo info = native_layout_info(instance);
    EXPECT_EQ(sizeof(Instance), info.allocated_size);
    EXPECT_EQ(sizeof(Instance), info.copy_size);
    ASSERT_TRUE(info.strong_span.has_value());
    EXPECT_EQ(InlineSlotObject::native_static_release_count(),
              info.strong_span->value_count);
}

TEST(NativeLayoutDescriptor, CodeObjectUsesCustomReleaseDescriptor)
{
    const ReleaseDescriptor &release =
        native_layout_descriptor_for(CodeObject::native_layout).release;

    EXPECT_EQ(ReleaseKind::CustomRelease, release.kind);
    EXPECT_EQ(CodeObject::release_managed_references, release.custom_release);
    EXPECT_EQ(CodeObject::destroy, release.destroy);
    EXPECT_EQ(CodeObject::trace_strong_references, release.custom_trace_strong);
    EXPECT_EQ(CodeObject::visit_weak_reference_units, release.visit_weak_units);

    const ObjectSizeDescriptor &object_size =
        native_layout_descriptor_for(CodeObject::native_layout).object_size;

    EXPECT_EQ(ObjectSizeKind::StaticSize, object_size.kind);
    EXPECT_EQ(sizeof(CodeObject), object_size.static_size_in_bytes);
}

TEST(NativeLayoutDescriptor, ShapeUsesCustomReleaseAndCustomObjectSize)
{
    const ReleaseDescriptor &release =
        native_layout_descriptor_for(Shape::native_layout).release;

    EXPECT_EQ(ReleaseKind::CustomRelease, release.kind);
    EXPECT_EQ(Shape::release_managed_references, release.custom_release);
    EXPECT_EQ(Shape::destroy, release.destroy);
    EXPECT_EQ(Shape::trace_strong_references, release.custom_trace_strong);
    EXPECT_EQ(Shape::visit_weak_reference_units, release.visit_weak_units);

    const ObjectSizeDescriptor &object_size =
        native_layout_descriptor_for(Shape::native_layout).object_size;

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

    EXPECT_EQ(Shape::allocation_size_for(3), object_size_in_bytes(shape));
}

TEST(NativeLayoutDescriptor, OverflowSlotsUsesDynamicAuxReleaseDescriptor)
{
    const ReleaseDescriptor &release =
        native_layout_descriptor_for(OverflowSlots::native_layout).release;

    EXPECT_EQ(ReleaseKind::DynamicAuxSpan, release.kind);
    EXPECT_EQ(OverflowSlots::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(0u, release.additional_release_count);

    const ObjectSizeDescriptor &object_size =
        native_layout_descriptor_for(OverflowSlots::native_layout).object_size;

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
        native_layout_descriptor_for(OverflowSlots::native_layout).release;

    EXPECT_EQ(OverflowSlots::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(0u, release.additional_release_count);
    EXPECT_EQ(5u, overflow_slots->native_layout_aux_count_value());

    auto span = resolve_ordinary_reference_span(overflow_slots, release);
    ASSERT_TRUE(span.has_value());
    EXPECT_EQ(OverflowSlots::native_value_offset_in_words(),
              span->value_offset_words);
    EXPECT_EQ(5u, span->value_count);
}

TEST(NativeLayoutDescriptor, OverflowSlotsNativeObjectSizeUsesCapacity)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    OverflowSlots *overflow_slots =
        context.thread()->make_internal_raw<OverflowSlots>(2, 5);

    EXPECT_EQ(OverflowSlots::allocation_size_for(5),
              object_size_in_bytes(overflow_slots));
}

TEST(NativeLayoutDescriptor, RawArrayBackingUsesEmptyReleaseDescriptor)
{
    const ReleaseDescriptor &release =
        native_layout_descriptor_for(RawArrayBacking::native_layout).release;

    EXPECT_EQ(ReleaseKind::StaticSpan, release.kind);
    EXPECT_EQ(0u, release.static_release_count);

    const ObjectSizeDescriptor &object_size =
        native_layout_descriptor_for(RawArrayBacking::native_layout)
            .object_size;

    EXPECT_EQ(ObjectSizeKind::Custom, object_size.kind);
    ASSERT_NE(nullptr, object_size.custom_size_in_bytes);
}

TEST(NativeLayoutDescriptor, RawArrayBackingNativeObjectSizeUsesStorageBytes)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    RawArrayBacking *backing =
        context.thread()->make_internal_raw<RawArrayBacking>(27);

    EXPECT_EQ(RawArrayBacking::allocation_size_for(27),
              object_size_in_bytes(backing));
}

TEST(NativeLayoutDescriptor, ValueArrayBackingUsesDynamicSmiReleaseDescriptor)
{
    const ReleaseDescriptor &release =
        native_layout_descriptor_for(ValueArrayBacking::native_layout).release;

    EXPECT_EQ(ReleaseKind::DynamicSmiSpan, release.kind);
    EXPECT_EQ(ValueArrayBacking::native_value_count_offset_in_words(),
              release.count_offset_words);
    EXPECT_EQ(ValueArrayBacking::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(0u, release.additional_release_count);

    const ObjectSizeDescriptor &object_size =
        native_layout_descriptor_for(ValueArrayBacking::native_layout)
            .object_size;

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
        native_layout_descriptor_for(ValueArrayBacking::native_layout).release;

    EXPECT_EQ(7u, backing->value_cell_count());
    EXPECT_EQ(ValueArrayBacking::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(ValueArrayBacking::allocation_size_for(7),
              object_size_in_bytes(backing));
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
    EXPECT_EQ(ValueArrayBacking::allocation_size_for(cell_count),
              object_size_in_bytes(backing));
}

TEST(NativeLayoutDescriptor, ValueArrayRejectsBackingCellCountOutsideSmiRange)
{
    ValueArray<TwoValueCells> array;

    EXPECT_DEATH(
        { array.reserve(static_cast<size_t>(value_smi_max) / 2 + 1); },
        "VM value array backing cell count exceeds SMI range");
}

TEST(NativeLayoutDescriptor, ValueArrayBackingRejectsCellCountOutsideSmiRange)
{
    EXPECT_DEATH(
        {
            ValueArrayBacking backing(static_cast<size_t>(value_smi_max) + 1);
            (void)backing;
        },
        "VM array backing count exceeds SMI range");
}

TEST(NativeLayoutDescriptor, HeapPtrArrayBackingUsesDynamicSmiReleaseDescriptor)
{
    const ReleaseDescriptor &release =
        native_layout_descriptor_for(HeapPtrArrayBacking::native_layout)
            .release;

    EXPECT_EQ(ReleaseKind::DynamicSmiSpan, release.kind);
    EXPECT_EQ(HeapPtrArrayBacking::native_value_count_offset_in_words(),
              release.count_offset_words);
    EXPECT_EQ(HeapPtrArrayBacking::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(0u, release.additional_release_count);

    const ObjectSizeDescriptor &object_size =
        native_layout_descriptor_for(HeapPtrArrayBacking::native_layout)
            .object_size;

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
        native_layout_descriptor_for(HeapPtrArrayBacking::native_layout)
            .release;

    EXPECT_EQ(3u, backing->value_cell_count());
    EXPECT_EQ(HeapPtrArrayBacking::native_value_offset_in_words(),
              release.value_offset_words);
    EXPECT_EQ(reinterpret_cast<Value *>(backing) + release.value_offset_words,
              reinterpret_cast<Value *>(backing->elements));
    EXPECT_EQ(HeapPtrArrayBacking::allocation_size_for(3),
              object_size_in_bytes(backing));
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
    EXPECT_EQ(HeapPtrArrayBacking::allocation_size_for(cell_count),
              object_size_in_bytes(backing));
}

TEST(NativeLayoutDescriptor, HeapPtrArrayRejectsCapacityOutsideSmiRange)
{
    HeapPtrArray<ValidityCell> array;

    EXPECT_DEATH(
        { array.reserve(static_cast<size_t>(value_smi_max) + 1); },
        "VM array size exceeds SMI range");
}

TEST(NativeLayoutDescriptor, HeapPtrArrayBackingRejectsCellCountOutsideSmiRange)
{
    EXPECT_DEATH(
        {
            HeapPtrArrayBacking backing(static_cast<size_t>(value_smi_max) + 1);
            (void)backing;
        },
        "VM array backing count exceeds SMI range");
}

TEST(NativeLayoutDescriptor, TestOnlyDescriptorsAreInvalid)
{
    size_t test_only_index =
        native_layout_descriptor_detail::native_layout_index(
            NativeLayoutId::TestOnly);
    const auto &descriptors =
        native_layout_descriptor_detail::native_layout_descriptors;
    const NativeLayoutDescriptor &descriptor = descriptors[test_only_index];
    const ReleaseDescriptor &release = descriptor.release;
    const ObjectSizeDescriptor &object_size = descriptor.object_size;

    EXPECT_FALSE(
        native_layout_descriptor_detail::release_descriptor_is_valid(release));
    EXPECT_FALSE(
        native_layout_descriptor_detail::object_size_descriptor_is_valid(
            object_size));
    EXPECT_FALSE(
        native_layout_descriptor_detail::native_layout_descriptor_is_valid(
            descriptor));
}
