#include "builtin_types/float.h"
#include "builtin_types/list.h"
#include "builtin_types/slice.h"
#include "builtin_types/tuple.h"
#include "bytecode/bytecode_decoder.h"
#include "bytecode/code_object.h"
#include "object_model/attr.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "object_model/instance.h"
#include "object_model/shape.h"
#include "runtime/exception_object.h"
#include "runtime/thread_state.h"
#include "test_helpers.h"
#include <cassert>
#include <gtest/gtest.h>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

using namespace cl;

static uint32_t class_property_count(ClassObject *cls)
{
    constexpr uint32_t class_metadata_descriptor_count =
        ClassObject::class_metadata_slot_count + 2;
    Shape *shape = cls->get_shape();
    assert(shape->present_count() >= class_metadata_descriptor_count);
    return shape->present_count() - class_metadata_descriptor_count;
}

static TValue<String> class_property_name(ClassObject *cls, uint32_t idx)
{
    constexpr uint32_t class_metadata_descriptor_count =
        ClassObject::class_metadata_slot_count + 2;
    return cls->get_shape()->get_property_name(class_metadata_descriptor_count +
                                               idx);
}

static Value class_property_value(ClassObject *cls, uint32_t idx)
{
    constexpr uint32_t class_metadata_descriptor_count =
        ClassObject::class_metadata_slot_count + 2;
    DescriptorInfo info = cls->get_shape()->get_descriptor_info(
        class_metadata_descriptor_count + idx);
    return cls->read_storage_location(info.storage_location());
}

static Expected<TValue<ClassObject>> make_storage_selection_class(
    test::VmTestContext &context, std::initializer_list<ClassObject *> bases,
    NativeLayoutId layout = NativeLayoutId::Instance, uint32_t capacity = 10)
{
    Owned<TValue<Tuple>> retained_bases(
        context.thread()->make_object_value<Tuple>(bases.size()));
    size_t idx = 0;
    for(ClassObject *base: bases)
    {
        retained_bases.extract()->initialize_item_unchecked(
            idx++, Value::from_oop(base));
    }
    if(layout != NativeLayoutId::Instance)
    {
        assert(bases.size() == 1);
        return Expected<TValue<ClassObject>>::ok(
            context.thread()->make_internal_value<ClassObject>(
                context.vm().get_or_create_interned_string_value(
                    L"StorageFixture"),
                capacity, *bases.begin(), layout));
    }
    return ClassObject::make(
        context.thread(), context.vm().type_class(),
        context.vm().get_or_create_interned_string_value(L"StorageCandidate"),
        capacity, retained_bases.value());
}

TEST(InstanceStorageSelection, InstanceOnlyUsesSuppliedCapacity)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    auto mixin = make_storage_selection_class(
        context, {context.vm().object_class()}, NativeLayoutId::Instance, 7);
    ASSERT_FALSE(mixin.has_exception());
    Owned<TValue<ClassObject>> retained_mixin(mixin.value());
    auto candidate =
        make_storage_selection_class(context, {retained_mixin.extract()});
    ASSERT_FALSE(candidate.has_exception());
    Owned<TValue<ClassObject>> retained_candidate(candidate.value());
    auto selected = select_instance_storage(
        TValue<Tuple>::from_value_assumed(
            retained_candidate.extract()->get_mro_value()),
        4);
    ASSERT_FALSE(selected.has_exception());
    EXPECT_EQ(nullptr, selected.value().builtin_provider);
    EXPECT_EQ(NativeLayoutId::Instance, selected.value().layout);
    EXPECT_EQ(4u, selected.value().inline_slot_capacity);
}

TEST(InstanceStorageSelection, FindsBuiltinBehindInstanceMixinsAndSubclasses)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    auto first =
        make_storage_selection_class(context, {context.vm().list_class()});
    ASSERT_FALSE(first.has_exception());
    Owned<TValue<ClassObject>> retained_first(first.value());
    auto second =
        make_storage_selection_class(context, {context.vm().list_class()});
    ASSERT_FALSE(second.has_exception());
    Owned<TValue<ClassObject>> retained_second(second.value());
    auto mixin =
        make_storage_selection_class(context, {context.vm().object_class()});
    ASSERT_FALSE(mixin.has_exception());
    Owned<TValue<ClassObject>> retained_mixin(mixin.value());
    auto candidate = make_storage_selection_class(
        context, {retained_mixin.extract(), retained_first.extract(),
                  retained_second.extract()});
    ASSERT_FALSE(candidate.has_exception());
    Owned<TValue<ClassObject>> retained_candidate(candidate.value());
    auto selected = select_instance_storage(
        TValue<Tuple>::from_value_assumed(
            retained_candidate.extract()->get_mro_value()),
        10);
    ASSERT_FALSE(selected.has_exception());
    EXPECT_EQ(retained_first.extract(), selected.value().builtin_provider);
    EXPECT_EQ(NativeLayoutId::List, selected.value().layout);
    EXPECT_EQ(0u, selected.value().inline_slot_capacity);
    EXPECT_EQ(NativeLayoutId::List,
              retained_candidate.extract()->instance_native_layout_id());
    EXPECT_EQ(
        0u, retained_candidate.extract()->get_instance_inline_slot_capacity());
}

TEST(InstanceStorageSelection, SkipsCandidateStorageAndUsesAncestorCapacity)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    // The candidate's provisional storage is not an ancestor requirement.
    auto candidate = make_storage_selection_class(
        context, {context.vm().slice_class()}, NativeLayoutId::List, 0);
    ASSERT_FALSE(candidate.has_exception());
    Owned<TValue<ClassObject>> retained_candidate(candidate.value());
    auto selected = select_instance_storage(
        TValue<Tuple>::from_value_assumed(
            retained_candidate.extract()->get_mro_value()),
        10);
    ASSERT_FALSE(selected.has_exception());
    EXPECT_EQ(context.vm().slice_class(), selected.value().builtin_provider);
    EXPECT_EQ(NativeLayoutId::Slice, selected.value().layout);
    EXPECT_EQ(3u, selected.value().inline_slot_capacity);
}

TEST(InstanceStorageSelection, StopIterationKeepsTwoSlotsWithExceptionAncestors)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    ClassObject *stop =
        context.thread()->class_for_builtin_name(L"StopIteration");
    ClassObject *exception =
        context.thread()->class_for_builtin_name(L"Exception");
    for(bool redundant_base: {false, true})
    {
        auto candidate =
            redundant_base
                ? make_storage_selection_class(context, {stop, exception})
                : make_storage_selection_class(context, {stop});
        ASSERT_FALSE(candidate.has_exception());
        Owned<TValue<ClassObject>> retained_candidate(candidate.value());
        auto selected = select_instance_storage(
            TValue<Tuple>::from_value_assumed(
                retained_candidate.extract()->get_mro_value()),
            10);
        ASSERT_FALSE(selected.has_exception());
        EXPECT_EQ(stop, selected.value().builtin_provider);
        EXPECT_EQ(NativeLayoutId::StopIteration, selected.value().layout);
        EXPECT_EQ(2u, selected.value().inline_slot_capacity);
    }
}

TEST(InstanceStorageSelection, RepeatedExceptionLayoutsKeepFirstProvider)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    ClassObject *type_error =
        context.thread()->class_for_builtin_name(L"TypeError");
    auto candidate = make_storage_selection_class(context, {type_error});
    ASSERT_FALSE(candidate.has_exception());
    Owned<TValue<ClassObject>> retained_candidate(candidate.value());
    auto selected = select_instance_storage(
        TValue<Tuple>::from_value_assumed(
            retained_candidate.extract()->get_mro_value()),
        10);
    ASSERT_FALSE(selected.has_exception());
    EXPECT_EQ(type_error, selected.value().builtin_provider);
    EXPECT_EQ(NativeLayoutId::Exception, selected.value().layout);
    EXPECT_EQ(1u, selected.value().inline_slot_capacity);
}

TEST(InstanceStorageSelection, RejectsDistinctLayouts)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    ClassObject *exception =
        context.thread()->class_for_builtin_name(L"Exception");
    for(auto bases: {std::initializer_list<ClassObject *>{
                         context.vm().list_class(), context.vm().dict_class()},
                     std::initializer_list<ClassObject *>{
                         context.vm().slice_class(), exception}})
    {
        Owned<TValue<Tuple>> mro(
            context.thread()->make_object_value<Tuple>(bases.size() + 1));
        mro.extract()->initialize_item_unchecked(
            0, Value::from_oop(context.vm().object_class()));
        size_t index = 1;
        for(ClassObject *base: bases)
        {
            mro.extract()->initialize_item_unchecked(index++,
                                                     Value::from_oop(base));
        }
        auto selected = select_instance_storage(mro.value(), 10);
        ASSERT_TRUE(selected.has_exception());
        EXPECT_EQ(context.thread()->class_for_builtin_name(L"TypeError"),
                  context.thread()
                      ->pending_exception_object()
                      .extract()
                      ->get_shape()
                      ->get_class());
        context.thread()->clear_pending_exception();
    }
}

TEST(InstanceStorageSelection, UpgradesExceptionSubclassToStopIterationStorage)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    ClassObject *exception =
        context.thread()->class_for_builtin_name(L"Exception");
    ClassObject *stop =
        context.thread()->class_for_builtin_name(L"StopIteration");
    // Prepare the subclass storage metadata that stage 4 will install.
    auto e = make_storage_selection_class(context, {exception},
                                          NativeLayoutId::Exception, 1);
    ASSERT_FALSE(e.has_exception());
    Owned<TValue<ClassObject>> retained_e(e.value());
    auto stop_subclass = make_storage_selection_class(
        context, {stop}, NativeLayoutId::StopIteration, 2);
    ASSERT_FALSE(stop_subclass.has_exception());
    Owned<TValue<ClassObject>> retained_stop_subclass(stop_subclass.value());
    for(ClassObject *stop_provider: {stop, retained_stop_subclass.extract()})
    {
        for(bool exception_first: {true, false})
        {
            auto candidate =
                exception_first
                    ? make_storage_selection_class(
                          context, {retained_e.extract(), stop_provider})
                    : make_storage_selection_class(
                          context, {stop_provider, retained_e.extract()});
            ASSERT_FALSE(candidate.has_exception());
            Owned<TValue<ClassObject>> retained_candidate(candidate.value());
            auto mro = TValue<Tuple>::from_value_assumed(
                retained_candidate.extract()->get_mro_value());
            EXPECT_EQ(Value::from_oop(exception_first ? retained_e.extract()
                                                      : stop_provider),
                      mro.extract()->item_unchecked(1));
            auto selected = select_instance_storage(mro, 10);
            ASSERT_FALSE(selected.has_exception());
            EXPECT_EQ(stop_provider, selected.value().builtin_provider);
            EXPECT_EQ(NativeLayoutId::StopIteration, selected.value().layout);
            EXPECT_EQ(2u, selected.value().inline_slot_capacity);
        }
    }
}

TEST(ClassSubclassPolicy, PermitsSupportedBuiltinBasesAndTheirSubclasses)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    for(ClassObject *base:
        {context.vm().object_class(), context.vm().type_class(),
         context.vm().int_class(), context.vm().float_class(),
         context.vm().str_class(), context.vm().bytes_class(),
         context.vm().list_class(), context.vm().tuple_class(),
         context.vm().dict_class(), context.vm().module_class(),
         context.thread()->class_for_builtin_name(L"BaseException"),
         context.thread()->class_for_builtin_name(L"StopIteration")})
    {
        ASSERT_TRUE(base->allows_subclasses());
        auto candidate = make_storage_selection_class(context, {base});
        ASSERT_FALSE(candidate.has_exception());
        Owned<TValue<ClassObject>> cls(candidate.value());
        EXPECT_TRUE(cls.extract()->allows_subclasses());
        auto child = make_storage_selection_class(context, {cls.extract()});
        ASSERT_FALSE(child.has_exception());
        EXPECT_EQ(cls.extract()->instance_native_layout_id(),
                  child.value().extract()->instance_native_layout_id());
    }
}

TEST(ClassSubclassPolicy, RejectsProhibitedBuiltinBases)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    for(ClassObject *base:
        {context.vm().bool_class(), context.vm().none_type_class(),
         context.vm().ellipsis_type_class(),
         context.vm().not_implemented_type_class(), context.vm().slice_class(),
         context.vm().function_class(), context.vm().code_class(),
         context.vm().list_iterator_class(),
         context.vm().tuple_iterator_class(),
         context.vm().range_iterator_class(), context.vm().slotdict_class(),
         context.vm().module_loader_class(), context.vm().module_spec_class()})
    {
        ASSERT_FALSE(base->allows_subclasses());
        auto candidate = make_storage_selection_class(context, {base});
        ASSERT_TRUE(candidate.has_exception());
        EXPECT_EQ(context.thread()->class_for_builtin_name(L"TypeError"),
                  context.thread()
                      ->pending_exception_object()
                      .extract()
                      ->get_shape()
                      ->get_class());
        context.thread()->clear_pending_exception();
        // A permissible base alongside a prohibited one cannot bypass policy.
        candidate = make_storage_selection_class(
            context, {context.vm().object_class(), base});
        EXPECT_TRUE(candidate.has_exception());
        context.thread()->clear_pending_exception();
    }
}

TEST(NativeAllocationValidation, CachedSliceShapeInitializesPredefinedFields)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Shape *root = context.vm().slice_class()->get_instance_root_shape();
    Owned<TValue<Slice>> slice(context.thread()->make_internal_value<Slice>(
        root, Value::from_smi(1), Value::from_smi(9), Value::from_smi(2)));
    EXPECT_EQ(root, slice.extract()->get_shape());
    EXPECT_EQ(Value::from_smi(1), slice.extract()->start.value());
    EXPECT_EQ(Value::from_smi(9), slice.extract()->stop.value());
    EXPECT_EQ(Value::from_smi(2), slice.extract()->step.value());
}

#ifndef NDEBUG
TEST(NativeAllocationValidationDeathTest, RejectsMismatchedZeroSlotLayouts)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    EXPECT_DEATH(
        context.thread()->make_internal_raw<List>(context.vm().float_class()),
        "instance_native_layout_id");
    EXPECT_DEATH(context.thread()->make_internal_raw<Float>(
                     context.vm().list_class(), 1.5),
                 "instance_native_layout_id");
    OwnedHeapPtr<Shape> mismatched(Shape::make_root_with_descriptors(
        TValue<ClassObject>::from_oop(context.vm().float_class()), nullptr, 0,
        0, 0, Slice::kInlineSlotCount, mutable_attribute_shape_flags()));
    EXPECT_DEATH(
        context.thread()->make_internal_raw<Slice>(
            mismatched.extract(), Value::None(), Value::None(), Value::None()),
        "instance_native_layout_id");
}

TEST(NativeAllocationValidationDeathTest, RejectsBootstrapAndShapeMismatch)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Owned<TValue<List>> list(context.thread()->make_object_value<List>());
    EXPECT_DEATH(
        list.extract()->install_bootstrap_class(context.vm().float_class()),
        "instance_native_layout_id");
    EXPECT_DEATH(list.extract()->set_shape(
                     context.vm().float_class()->get_instance_root_shape()),
                 "instance_native_layout_id");
}

TEST(NativeAllocationValidationDeathTest,
     RejectsOversizedInstanceBeforeAllocation)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Owned<TValue<ClassObject>> cls(
        context.thread()->make_internal_value<ClassObject>(
            context.vm().get_or_create_interned_string_value(L"Oversized"),
            uint32_t(UINT16_MAX) + 1, context.vm().object_class(),
            NativeLayoutId::Instance));
    EXPECT_DEATH(Instance::size_for(cls.extract()),
                 "native_inline_slot_capacity_is_valid");
}
#endif

TEST(ConstructorAllocatorSelection, RejectsUnsupportedLayoutBeforeCachingThunk)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    {
        // A native layout without an inherited builtin __new__ must still
        // reject generic instance allocation. Tuple subclasses now inherit
        // tuple.__new__, so use a raw storage fixture rooted at object here.
        auto candidate = make_storage_selection_class(
            context, {context.vm().object_class()}, NativeLayoutId::Tuple, 0);
        ASSERT_FALSE(candidate.has_exception());
        Owned<TValue<ClassObject>> cls(candidate.value());
        auto thunk = cls.extract()->get_or_create_constructor_thunk();
        ASSERT_TRUE(thunk.has_exception());
        EXPECT_EQ(nullptr, cls.extract()->current_constructor_thunk());
        EXPECT_EQ(context.thread()->class_for_builtin_name(L"TypeError"),
                  context.thread()
                      ->pending_exception_object()
                      .extract()
                      ->get_shape()
                      ->get_class());
        context.thread()->clear_pending_exception();
    }
    auto ordinary =
        make_storage_selection_class(context, {context.vm().object_class()});
    ASSERT_FALSE(ordinary.has_exception());
    Owned<TValue<ClassObject>> cls(ordinary.value());
    auto thunk = cls.extract()->get_or_create_constructor_thunk();
    ASSERT_FALSE(thunk.has_exception());
    EXPECT_TRUE(thunk.value().is_found());
}

TEST(ConstructorAllocatorSelection, TupleUsesInheritedAllocator)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    auto candidate =
        make_storage_selection_class(context, {context.vm().tuple_class()});
    ASSERT_FALSE(candidate.has_exception());
    Owned<TValue<ClassObject>> cls(candidate.value());
    auto thunk = cls.extract()->get_or_create_constructor_thunk();
    ASSERT_FALSE(thunk.has_exception());
    ASSERT_TRUE(thunk.value().is_found());
    EXPECT_EQ(thunk.value().thunk, cls.extract()->current_constructor_thunk());
    Owned<Value> result(context.thread()->call_clovervm_function(
        TValue<Function>::from_oop(thunk.value().thunk)));
    ASSERT_TRUE(has_native_layout<Tuple>(result.value()));
    EXPECT_EQ(cls.extract(),
              native_cast<Tuple>(result.value())->get_shape()->get_class());
}

TEST(ConstructorAllocatorSelection, ListUsesDedicatedOpcode)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    auto candidate =
        make_storage_selection_class(context, {context.vm().list_class()});
    ASSERT_FALSE(candidate.has_exception());
    Owned<TValue<ClassObject>> cls(candidate.value());
    auto thunk = cls.extract()->get_or_create_constructor_thunk();
    ASSERT_FALSE(thunk.has_exception());
    ASSERT_TRUE(thunk.value().is_found());
    CodeObject *code = thunk.value().thunk->code_object.extract();
    ASSERT_GE(code->code.size(), 2u);
    EXPECT_EQ(static_cast<uint8_t>(Bytecode::CreateListKnownClass),
              code->code[0]);
    EXPECT_EQ(cls.raw_value(), code->constant_table[code->code[1]].value());
    EXPECT_EQ(
        0u,
        cls.extract()->get_instance_root_shape()->get_inline_slot_capacity());
}

TEST(ConstructorAllocatorSelection, ExceptionUsesDedicatedOpcode)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    auto candidate = make_storage_selection_class(
        context, {context.thread()->class_for_builtin_name(L"Exception")});
    ASSERT_FALSE(candidate.has_exception());
    Owned<TValue<ClassObject>> cls(candidate.value());
    auto thunk = cls.extract()->get_or_create_constructor_thunk();
    ASSERT_FALSE(thunk.has_exception());
    ASSERT_TRUE(thunk.value().is_found());
    CodeObject *code = thunk.value().thunk->code_object.extract();
    ASSERT_GE(code->code.size(), 2u);
    EXPECT_EQ(static_cast<uint8_t>(Bytecode::CreateExceptionKnownClass),
              code->code[0]);
    EXPECT_EQ(cls.raw_value(), code->constant_table[code->code[1]].value());
}

TEST(ConstructorAllocatorSelection, StopIterationUsesDedicatedOpcode)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    auto candidate = make_storage_selection_class(
        context, {context.thread()->class_for_builtin_name(L"StopIteration")});
    ASSERT_FALSE(candidate.has_exception());
    Owned<TValue<ClassObject>> cls(candidate.value());
    auto thunk = cls.extract()->get_or_create_constructor_thunk();
    ASSERT_FALSE(thunk.has_exception());
    ASSERT_TRUE(thunk.value().is_found());
    CodeObject *code = thunk.value().thunk->code_object.extract();
    ASSERT_GE(code->code.size(), 2u);
    EXPECT_EQ(static_cast<uint8_t>(Bytecode::CreateStopIterationKnownClass),
              code->code[0]);
    EXPECT_EQ(cls.raw_value(), code->constant_table[code->code[1]].value());
}

TEST(ClassStorageSelection, InheritsPredefinedRootDescriptors)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    for(ClassObject *provider:
        {context.thread()->class_for_builtin_name(L"Exception"),
         context.thread()->class_for_builtin_name(L"StopIteration")})
    {
        auto candidate = make_storage_selection_class(context, {provider});
        ASSERT_FALSE(candidate.has_exception());
        Owned<TValue<ClassObject>> cls(candidate.value());
        Shape *source = provider->get_instance_root_shape();
        Shape *root = cls.extract()->get_instance_root_shape();
        EXPECT_NE(source, root);
        EXPECT_EQ(cls.extract(), root->get_class());
        EXPECT_EQ(provider->instance_native_layout_id(),
                  cls.extract()->instance_native_layout_id());
        EXPECT_EQ(source->get_inline_slot_capacity(),
                  root->get_inline_slot_capacity());
        EXPECT_EQ(source->get_next_slot_index(), root->get_next_slot_index());
        ASSERT_EQ(source->property_count(), root->property_count());
        EXPECT_FALSE(root->has_flag(ShapeFlag::IsImmutable));
        EXPECT_TRUE(root->allows_attribute_add_delete());
        for(uint32_t idx = 0; idx < source->property_count(); ++idx)
        {
            EXPECT_EQ(source->get_property_name(idx),
                      root->get_property_name(idx));
            auto expected = source->get_descriptor_info(idx);
            auto actual = root->get_descriptor_info(idx);
            EXPECT_EQ(expected.physical_idx, actual.physical_idx);
            EXPECT_EQ(expected.kind, actual.kind);
            EXPECT_EQ(expected.flags, actual.flags);
            EXPECT_EQ(expected.special_kind(), actual.special_kind());
        }
    }
}

TEST(ClassStorageSelection,
     NativeAllocationRetainsSubclassShapeAndOverflowAttributes)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    auto candidate =
        make_storage_selection_class(context, {context.vm().list_class()});
    ASSERT_FALSE(candidate.has_exception());
    Owned<TValue<ClassObject>> cls(candidate.value());
    Owned<TValue<List>> list(
        context.thread()->make_internal_value<List>(cls.extract()));
    EXPECT_EQ(cls.extract(), list.extract()->get_shape()->get_class());
    EXPECT_EQ(0u, list.extract()->get_shape()->get_inline_slot_capacity());
    list.extract()->append(Value::from_smi(7));
    auto attr = context.vm().get_or_create_interned_string_value(L"extra");
    ASSERT_TRUE(
        list.extract()->set_own_property(attr, Value::from_smi(42)).value());
    EXPECT_EQ(StorageKind::Overflow,
              list.extract()->get_shape()->resolve_present_property(attr).kind);
    EXPECT_EQ(Value::from_smi(42), list.extract()->get_own_property(attr));
    EXPECT_EQ(Value::from_smi(7), list.extract()->item_unchecked(0));
    ASSERT_TRUE(list.extract()->delete_own_property(attr));
    EXPECT_EQ(Value::not_present(), list.extract()->get_own_property(attr));
}

TEST(ClassStorageSelection, NativeExceptionFactoriesInitializeInheritedFields)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    ClassObject *base = context.thread()->class_for_builtin_name(L"Exception");
    ClassObject *stop =
        context.thread()->class_for_builtin_name(L"StopIteration");
    auto e = make_storage_selection_class(context, {base});
    ASSERT_FALSE(e.has_exception());
    Owned<TValue<ClassObject>> retained_e(e.value());
    auto candidate =
        make_storage_selection_class(context, {retained_e.extract(), stop});
    ASSERT_FALSE(candidate.has_exception());
    Owned<TValue<ClassObject>> cls(candidate.value());
    Owned<TValue<Exception>> exception(make_exception_object_with_arg(
        context.thread(), cls.value(), Value::from_smi(19)));
    EXPECT_EQ(NativeLayoutId::StopIteration,
              exception.extract()->native_layout_id());
    EXPECT_EQ(cls.extract(), exception.extract()->get_shape()->get_class());
    EXPECT_EQ(1u, exception.extract()->args.extract()->size());
    auto value_name =
        context.vm().get_or_create_interned_string_value(L"value");
    EXPECT_EQ(Value::from_smi(19),
              exception.extract()->get_own_property(value_name));
    auto args_name = context.vm().get_or_create_interned_string_value(L"args");
    EXPECT_FALSE(exception.extract()
                     ->set_own_property(args_name, Value::from_smi(0))
                     .value());
}

TEST(Shape, InstanceRootShapeCarriesPresentedClass)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        name, 2, context.vm().object_class(), NativeLayoutId::Instance);

    Shape *root_shape = cls->get_instance_root_shape();
    ASSERT_NE(nullptr, root_shape);
    EXPECT_EQ(cls, root_shape->get_class());
    EXPECT_EQ(nullptr, root_shape->get_previous_shape());
    ASSERT_EQ(2u, root_shape->property_count());
    EXPECT_EQ(2u, root_shape->present_count());
    EXPECT_EQ(0u, root_shape->latent_count());
    EXPECT_FALSE(root_shape->has_flag(ShapeFlag::IsClassObject));
    EXPECT_STREQ(L"__class__",
                 root_shape->get_property_name(0).extract()->data);
    EXPECT_FALSE(root_shape->get_property_storage_location(0).is_found());
    EXPECT_TRUE(
        root_shape->get_descriptor_info(0).has_flag(DescriptorFlag::ReadOnly));
    EXPECT_TRUE(root_shape->get_descriptor_info(0).has_flag(
        DescriptorFlag::StableSlot));
    EXPECT_TRUE(root_shape->get_descriptor_info(0).has_flag(
        DescriptorFlag::SpecialRead));
    EXPECT_TRUE(root_shape->get_descriptor_info(0).has_flag(
        DescriptorFlag::SpecialMutate));
    EXPECT_EQ(DescriptorSpecialKind::ShapeClass,
              root_shape->get_descriptor_info(0).special_kind());
    EXPECT_STREQ(L"__dict__", root_shape->get_property_name(1).extract()->data);
    EXPECT_FALSE(root_shape->get_property_storage_location(1).is_found());
    EXPECT_TRUE(
        root_shape->get_descriptor_info(1).has_flag(DescriptorFlag::ReadOnly));
    EXPECT_TRUE(root_shape->get_descriptor_info(1).has_flag(
        DescriptorFlag::StableSlot));
    EXPECT_TRUE(root_shape->get_descriptor_info(1).has_flag(
        DescriptorFlag::SpecialRead));
    EXPECT_TRUE(root_shape->get_descriptor_info(1).has_flag(
        DescriptorFlag::SpecialMutate));
    EXPECT_EQ(DescriptorSpecialKind::SlotDict,
              root_shape->get_descriptor_info(1).special_kind());
    EXPECT_EQ(0, root_shape->get_next_slot_index());
    EXPECT_EQ(2u, root_shape->get_inline_slot_capacity());
}

TEST(Shape, BuiltinInstanceRootsMarkSemanticImmutability)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    VirtualMachine &vm = context.vm();

    ClassObject *immutable_classes[] = {
        vm.object_class(),        vm.str_class(),
        vm.bytes_class(),         vm.int_class(),
        vm.bool_class(),          vm.float_class(),
        vm.tuple_class(),         vm.slice_class(),
        vm.none_type_class(),     vm.not_implemented_type_class(),
        vm.ellipsis_type_class(), vm.code_class(),
    };
    for(ClassObject *cls: immutable_classes)
    {
        SCOPED_TRACE(cls->get_name().extract()->data);
        EXPECT_TRUE(
            cls->get_instance_root_shape()->has_flag(ShapeFlag::IsImmutable));
    }

    Shape *immutable_inline_shapes[] = {
        vm.smi_shape(),      vm.bool_shape(),
        vm.none_shape(),     vm.not_implemented_shape(),
        vm.ellipsis_shape(),
    };
    for(Shape *shape: immutable_inline_shapes)
    {
        EXPECT_TRUE(shape->has_flag(ShapeFlag::IsImmutable));
    }

    EXPECT_TRUE(vm.slice_step_none_shape()->has_flag(ShapeFlag::IsImmutable));
    EXPECT_TRUE(vm.slice_general_shape()->has_flag(ShapeFlag::IsImmutable));

    ClassObject *mutable_classes[] = {
        vm.list_class(),   vm.dict_class(),           vm.function_class(),
        vm.module_class(), vm.range_iterator_class(),
    };
    for(ClassObject *cls: mutable_classes)
    {
        SCOPED_TRACE(cls->get_name().extract()->data);
        EXPECT_FALSE(
            cls->get_instance_root_shape()->has_flag(ShapeFlag::IsImmutable));
    }
}

TEST(Shape, ClassObjectShapeCarriesMetaclass)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        name, 2, context.vm().object_class(), NativeLayoutId::Instance);

    EXPECT_EQ(context.vm().type_class(), cls->get_shape()->get_class());
}

TEST(ClassObject, BuiltinClassesCarryInstanceNativeLayoutIds)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    EXPECT_EQ(NativeLayoutId::Instance,
              context.vm().object_class()->instance_native_layout_id());
    EXPECT_EQ(NativeLayoutId::ModuleObject,
              context.vm().module_class()->instance_native_layout_id());
    EXPECT_EQ(NativeLayoutId::List,
              context.vm().list_class()->instance_native_layout_id());
    EXPECT_EQ(NativeLayoutId::ClassObject,
              context.vm().type_class()->instance_native_layout_id());
    EXPECT_EQ(NativeLayoutId::BigInt,
              context.vm().int_class()->instance_native_layout_id());
    EXPECT_EQ(NativeLayoutId::Invalid,
              context.vm().bool_class()->instance_native_layout_id());
    EXPECT_EQ(NativeLayoutId::Invalid,
              context.vm().none_type_class()->instance_native_layout_id());
    EXPECT_EQ(
        NativeLayoutId::Invalid,
        context.vm().not_implemented_type_class()->instance_native_layout_id());
    EXPECT_EQ(NativeLayoutId::Invalid,
              context.vm().ellipsis_type_class()->instance_native_layout_id());
}

TEST(Shape, ShapeFlagsAreStoredOnShape)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class(), NativeLayoutId::Instance);
    ShapeFlags flags =
        shape_flag(ShapeFlag::IsClassObject) | immutable_shape_flags();

    Shape *shape = context.thread()->make_internal_raw<Shape>(
        TValue<ClassObject>::from_oop(cls), nullptr, 0, 0,
        ClassObject::class_inline_storage_slot_count, flags, 0);

    EXPECT_EQ(flags, shape->flags());
    EXPECT_TRUE(shape->has_flag(ShapeFlag::IsClassObject));
    EXPECT_TRUE(shape->has_flag(ShapeFlag::IsImmutable));
    EXPECT_FALSE(shape->has_flag(ShapeFlag::HasCustomGetAttribute));
}

TEST(Shape, ImmutableShapesRequireFixedAttributeNamespaces)
{
    EXPECT_FALSE(valid_shape_flags(shape_flag(ShapeFlag::IsImmutable)));
    EXPECT_FALSE(
        valid_shape_flags(shape_flag(ShapeFlag::IsImmutable) |
                          shape_flag(ShapeFlag::DisallowAttributeUpdates)));
    EXPECT_TRUE(valid_shape_flags(immutable_shape_flags()));
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
        cls_name, 2, context.vm().object_class(), NativeLayoutId::Instance);

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

    ASSERT_EQ(3u, shape_with_a->property_count());
    EXPECT_EQ(3u, shape_with_a->present_count());
    EXPECT_EQ(0u, shape_with_a->latent_count());
    EXPECT_STREQ(L"__class__",
                 shape_with_a->get_property_name(0).extract()->data);
    EXPECT_STREQ(L"__dict__",
                 shape_with_a->get_property_name(1).extract()->data);
    EXPECT_STREQ(L"a", shape_with_a->get_property_name(2).extract()->data);
    EXPECT_EQ(StorageKind::Inline,
              shape_with_a->get_property_storage_location(2).kind);
    EXPECT_EQ(0, shape_with_a->get_property_storage_location(2).physical_idx);
    EXPECT_FALSE(shape_with_a->get_descriptor_info(2).has_flag(
        DescriptorFlag::ReadOnly));
    EXPECT_EQ(1, shape_with_a->get_next_slot_index());

    ASSERT_EQ(4u, shape_with_ab->property_count());
    EXPECT_EQ(4u, shape_with_ab->present_count());
    EXPECT_EQ(0u, shape_with_ab->latent_count());
    EXPECT_STREQ(L"__class__",
                 shape_with_ab->get_property_name(0).extract()->data);
    EXPECT_STREQ(L"__dict__",
                 shape_with_ab->get_property_name(1).extract()->data);
    EXPECT_STREQ(L"a", shape_with_ab->get_property_name(2).extract()->data);
    EXPECT_STREQ(L"b", shape_with_ab->get_property_name(3).extract()->data);
    EXPECT_EQ(StorageKind::Inline,
              shape_with_ab->get_property_storage_location(3).kind);
    EXPECT_EQ(1, shape_with_ab->get_property_storage_location(3).physical_idx);
    EXPECT_EQ(shape_with_a, shape_with_ab->get_previous_shape());
    EXPECT_EQ(2, shape_with_ab->get_next_slot_index());

    ASSERT_EQ(3u, shape_with_b->property_count());
    EXPECT_EQ(3u, shape_with_b->present_count());
    EXPECT_EQ(0u, shape_with_b->latent_count());
    EXPECT_STREQ(L"__class__",
                 shape_with_b->get_property_name(0).extract()->data);
    EXPECT_STREQ(L"__dict__",
                 shape_with_b->get_property_name(1).extract()->data);
    EXPECT_STREQ(L"b", shape_with_b->get_property_name(2).extract()->data);
    EXPECT_EQ(shape_with_ab, shape_with_b->get_previous_shape());
    EXPECT_EQ(2, shape_with_b->get_next_slot_index());
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
        cls_name, 2, context.vm().object_class(), NativeLayoutId::Instance);

    Shape *root_shape = cls->get_instance_root_shape();
    Shape *shape_with_a =
        root_shape->derive_transition(a_name, ShapeTransitionVerb::Add);

    DescriptorLookup a_lookup =
        shape_with_a->lookup_descriptor_including_latent(a_name);
    EXPECT_EQ(DescriptorPresence::Present, a_lookup.presence);
    EXPECT_TRUE(a_lookup.is_present());
    EXPECT_FALSE(a_lookup.is_latent());
    EXPECT_EQ(2, a_lookup.descriptor_idx);
    EXPECT_TRUE(a_lookup.storage_location().is_found());

    DescriptorLookup present_a_lookup =
        shape_with_a->lookup_present_descriptor(a_name);
    EXPECT_EQ(DescriptorPresence::Present, present_a_lookup.presence);
    EXPECT_TRUE(present_a_lookup.is_present());
    EXPECT_EQ(2, present_a_lookup.descriptor_idx);

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
        cls_name, 2, context.vm().object_class(), NativeLayoutId::Instance);
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
        cls_name, 2, context.vm().object_class(), NativeLayoutId::Instance);
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

    EXPECT_FALSE(
        instance->set_own_property(a_name, Value::from_smi(9)).value());
    EXPECT_EQ(Value::from_smi(7), instance->get_own_property(a_name));
    EXPECT_FALSE(
        store_attr(Value::from_oop(instance), a_name, Value::from_smi(9))
            .value());
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
        cls_name, 2, context.vm().object_class(), NativeLayoutId::Instance);
    DescriptorFlags flags = descriptor_flag(DescriptorFlag::StableSlot);

    Shape *root_shape = cls->get_instance_root_shape();
    Shape *shape_with_a =
        root_shape->derive_transition(a_name, ShapeTransitionVerb::Add, flags);
    Shape *shape_without_a =
        shape_with_a->derive_transition(a_name, ShapeTransitionVerb::Delete);

    EXPECT_EQ(3u, shape_without_a->property_count());
    EXPECT_EQ(2u, shape_without_a->present_count());
    EXPECT_EQ(1u, shape_without_a->latent_count());
    DescriptorLookup latent_lookup =
        shape_without_a->lookup_descriptor_including_latent(a_name);
    EXPECT_TRUE(latent_lookup.is_latent());
    EXPECT_EQ(0, latent_lookup.info.physical_idx);
    EXPECT_FALSE(
        shape_without_a->lookup_present_descriptor(a_name).is_present());
    EXPECT_EQ(1, shape_without_a->get_next_slot_index());
    EXPECT_FALSE(shape_without_a->resolve_present_property(a_name).is_found());

    Shape *shape_with_a_again =
        shape_without_a->derive_transition(a_name, ShapeTransitionVerb::Add);
    DescriptorLookup present_lookup =
        shape_with_a_again->lookup_descriptor_including_latent(a_name);
    EXPECT_TRUE(present_lookup.is_present());
    EXPECT_EQ(0, present_lookup.info.physical_idx);
    EXPECT_EQ(3u, shape_with_a_again->property_count());
    EXPECT_EQ(3u, shape_with_a_again->present_count());
    EXPECT_EQ(0u, shape_with_a_again->latent_count());
    EXPECT_EQ(1, shape_with_a_again->get_next_slot_index());
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
        cls_name, 1, context.vm().object_class(), NativeLayoutId::Instance);

    Shape *root_shape = cls->get_instance_root_shape();
    Shape *shape_with_a =
        root_shape->derive_transition(a_name, ShapeTransitionVerb::Add);
    Shape *shape_with_ab =
        shape_with_a->derive_transition(b_name, ShapeTransitionVerb::Add);
    Shape *shape_with_b =
        shape_with_ab->derive_transition(a_name, ShapeTransitionVerb::Delete);
    Shape *shape_with_ba =
        shape_with_b->derive_transition(a_name, ShapeTransitionVerb::Add);

    ASSERT_EQ(4u, shape_with_ba->property_count());
    EXPECT_STREQ(L"__class__",
                 shape_with_ba->get_property_name(0).extract()->data);
    EXPECT_STREQ(L"__dict__",
                 shape_with_ba->get_property_name(1).extract()->data);
    EXPECT_STREQ(L"b", shape_with_ba->get_property_name(2).extract()->data);
    EXPECT_STREQ(L"a", shape_with_ba->get_property_name(3).extract()->data);
    EXPECT_EQ(StorageKind::Overflow,
              shape_with_ba->get_property_storage_location(3).kind);
    EXPECT_EQ(1, shape_with_ba->get_property_storage_location(3).physical_idx);
    EXPECT_EQ(3, shape_with_ba->get_next_slot_index());
    EXPECT_EQ(1u, shape_with_ba->get_inline_slot_capacity());
}

TEST(Shape, InstanceStoresClassAndShapeSeparately)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class(), NativeLayoutId::Instance);
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

    EXPECT_EQ(cls, instance->get_shape()->get_class());
    EXPECT_EQ(cls->get_instance_root_shape(), instance->get_shape());
}

TEST(Shape, InstanceShapeDescribesDunderClassWithoutStorageSlot)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> dunder_class_name(
        context.vm().get_or_create_interned_string_value(L"__class__"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class(), NativeLayoutId::Instance);
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

    Shape *shape = instance->get_shape();
    StorageLocation class_location =
        shape->resolve_present_property(dunder_class_name);
    EXPECT_FALSE(class_location.is_found());
    EXPECT_TRUE(
        shape->get_descriptor_info(0).has_flag(DescriptorFlag::ReadOnly));
    EXPECT_TRUE(
        shape->get_descriptor_info(0).has_flag(DescriptorFlag::StableSlot));
    EXPECT_TRUE(
        shape->get_descriptor_info(0).has_flag(DescriptorFlag::SpecialRead));
    EXPECT_TRUE(
        shape->get_descriptor_info(0).has_flag(DescriptorFlag::SpecialMutate));
    EXPECT_EQ(DescriptorSpecialKind::ShapeClass,
              shape->get_descriptor_info(0).special_kind());
    EXPECT_EQ(Value::not_present(),
              instance->get_own_property(dunder_class_name));

    EXPECT_TRUE(
        instance->set_own_property(dunder_class_name, Value::from_oop(cls))
            .value());
    EXPECT_EQ(Value::not_present(),
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
        cls_name, 2, context.vm().object_class(), NativeLayoutId::Instance);
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

    instance->set_own_property(a_name, Value::from_smi(7)).value();

    EXPECT_EQ(Value::from_smi(7), instance->get_own_property(a_name));
    EXPECT_EQ(3u, instance->get_shape()->property_count());
    EXPECT_EQ(1, instance->get_shape()->get_next_slot_index());
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
        cls_name, 1, context.vm().object_class(), NativeLayoutId::Instance);
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

    instance->set_own_property(a_name, Value::from_smi(1)).value();
    instance->set_own_property(b_name, Value::from_smi(2)).value();
    instance->set_own_property(c_name, Value::from_smi(3)).value();
    instance->set_own_property(d_name, Value::from_smi(4)).value();
    instance->set_own_property(e_name, Value::from_smi(5)).value();
    instance->set_own_property(f_name, Value::from_smi(6)).value();

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
        cls_name, 1, context.vm().object_class(), NativeLayoutId::Instance);
    Instance *instance = context.thread()->make_internal_raw<Instance>(cls);

    instance->set_own_property(a_name, Value::from_smi(10)).value();
    instance->set_own_property(b_name, Value::from_smi(11)).value();

    EXPECT_TRUE(instance->delete_own_property(a_name));
    EXPECT_EQ(Value::not_present(), instance->get_own_property(a_name));
    EXPECT_EQ(Value::from_smi(11), instance->get_own_property(b_name));

    instance->set_own_property(a_name, Value::from_smi(12)).value();
    EXPECT_EQ(Value::from_smi(12), instance->get_own_property(a_name));
    EXPECT_EQ(3, instance->get_shape()->get_next_slot_index());
}

TEST(Shape, CachedOverflowAccessSurvivesGrowthDeletionAndReaddition)
{
    test::VmTestContext context;
    EXPECT_EQ(Value::True(), context.run_file(LR"(
class C:
    pass
c = C()
c.a0 = 0
c.a1 = 1
c.a2 = 2
c.a3 = 3
c.a4 = 4
c.a5 = 5
c.a6 = 6
c.a7 = 7
c.a8 = 8
c.a9 = 9
c.target = 10
def read(obj):
    return obj.target
def write(obj, value):
    obj.target = value
for i in range(20):
    write(c, i)
    assert read(c) == i
c.b0 = 0
c.b1 = 1
c.b2 = 2
c.b3 = 3
c.b4 = 4
c.b5 = 5
c.b6 = 6
c.b7 = 7
c.b8 = 8
for i in range(20):
    write(c, i + 100)
    assert read(c) == i + 100
del c.target
write(c, 42)
assert read(c) == 42
assert c.a0 == 0
assert c.a9 == 9
assert c.b8 == 8
)"));
}

TEST(Shape, ZeroInlineNativeLayoutUsesCommonOverflowStorage)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    auto cls_name =
        context.vm().get_or_create_interned_string_value(L"NativeList");
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 0, context.vm().list_class(), NativeLayoutId::List);
    Owned<TValue<List>> list(
        TValue<List>::from_oop(context.thread()->make_internal_raw<List>(cls)));
    list.extract()->append(Value::from_smi(99));
    std::vector<TValue<String>> names;
    for(int idx = 0; idx < 12; ++idx)
    {
        names.push_back(context.vm().get_or_create_interned_string_value(
            L"attr" + std::to_wstring(idx)));
    }
    String *child = context.thread()->make_object_raw<String>(L"attribute");
    ASSERT_TRUE(list.extract()
                    ->set_own_property(names[0], Value::from_oop(child))
                    .value());
    ASSERT_EQ(1, child->refcount);
    auto read = resolve_attr_read_descriptor(list.raw_value(), names[0]);
    auto write = resolve_attr_write_descriptor(list.raw_value(), names[0]);
    ASSERT_TRUE(read.is_cacheable());
    ASSERT_TRUE(write.is_found());
    EXPECT_EQ(
        StorageKind::Overflow,
        list.extract()->get_shape()->resolve_present_property(names[0]).kind);
    for(int idx = 1; idx < 12; ++idx)
    {
        ASSERT_TRUE(list.extract()
                        ->set_own_property(names[idx], Value::from_smi(idx))
                        .value());
    }
    EXPECT_EQ(Value::from_oop(child),
              load_attr_from_plan(list.raw_value(), read.plan));
    // Replaced arrays retain their cells until their zero-count entries are
    // reclaimed.
    context.vm().run_heap_reclamation();
    EXPECT_EQ(1, child->refcount);
    ASSERT_TRUE(
        store_attr_from_plan(list.raw_value(), write.plan, Value::from_smi(42))
            .value());
    EXPECT_EQ(0, child->refcount);
    EXPECT_EQ(Value::from_smi(42),
              load_attr_from_plan(list.raw_value(), read.plan));
    ASSERT_TRUE(list.extract()->delete_own_property(names[0]));
    EXPECT_EQ(Value::not_present(), list.extract()->get_own_property(names[0]));
    ASSERT_TRUE(
        list.extract()->set_own_property(names[0], Value::from_smi(7)).value());
    EXPECT_EQ(Value::from_smi(7), list.extract()->get_own_property(names[0]));
    EXPECT_EQ(Value::from_smi(99), list.extract()->item_unchecked(0));
    EXPECT_EQ(0u, list.extract()->get_shape()->get_inline_slot_capacity());
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
        cls_name, 1, context.vm().object_class(), NativeLayoutId::Instance);
    Instance *first = context.thread()->make_internal_raw<Instance>(cls);
    Instance *second = context.thread()->make_internal_raw<Instance>(cls);

    first->set_own_property(a_name, Value::from_smi(1)).value();
    first->set_own_property(b_name, Value::from_smi(2)).value();
    second->set_own_property(a_name, Value::from_smi(10)).value();
    second->set_own_property(b_name, Value::from_smi(20)).value();

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
        cls_name, 2, context.vm().object_class(), NativeLayoutId::Instance);

    cls->set_own_property(a_name, Value::from_smi(1)).value();
    cls->set_own_property(b_name, Value::from_smi(2)).value();

    ASSERT_EQ(2u, class_property_count(cls));
    EXPECT_STREQ(L"a", class_property_name(cls, 0).extract()->data);
    EXPECT_STREQ(L"b", class_property_name(cls, 1).extract()->data);

    EXPECT_TRUE(cls->delete_own_property(a_name));

    ASSERT_EQ(1u, class_property_count(cls));
    EXPECT_STREQ(L"b", class_property_name(cls, 0).extract()->data);

    cls->set_own_property(a_name, Value::from_smi(3)).value();

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
        ClassObject::class_inline_storage_slot_count -
        ClassObject::class_predefined_slot_count + 1;
    for(uint32_t idx = 0; idx < property_count; ++idx)
    {
        std::wstring name = L"p" + std::to_wstring(idx);
        names.push_back(context.vm().get_or_create_interned_string_value(name));
    }
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class(), NativeLayoutId::Instance);

    for(uint32_t idx = 0; idx < property_count; ++idx)
    {
        cls->set_own_property(names[idx], Value::from_smi(idx + 1)).value();
    }

    Shape *shape = cls->get_shape();
    constexpr uint32_t class_metadata_descriptor_count =
        ClassObject::class_metadata_slot_count + 2;
    ASSERT_EQ(ClassObject::class_predefined_descriptor_count + property_count,
              shape->property_count());
    EXPECT_EQ(class_metadata_descriptor_count + property_count,
              shape->present_count());
    EXPECT_EQ(ClassObject::class_predefined_descriptor_count -
                  class_metadata_descriptor_count,
              shape->latent_count());
    EXPECT_EQ(int32_t(ClassObject::class_inline_storage_slot_count + 1),
              shape->get_next_slot_index());
    EXPECT_EQ(property_count, class_property_count(cls));
    EXPECT_EQ(Value::from_smi(1), class_property_value(cls, 0));
    EXPECT_EQ(Value::from_smi(property_count),
              class_property_value(cls, property_count - 1));

    StorageLocation first_location = shape->resolve_present_property(names[0]);
    ASSERT_TRUE(first_location.is_found());
    EXPECT_EQ(StorageKind::Inline, first_location.kind);
    EXPECT_EQ(int32_t(ClassObject::class_predefined_slot_count),
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
        cls_name, 2, context.vm().object_class(), NativeLayoutId::Instance);

    Shape *shape = cls->get_shape();
    ASSERT_NE(nullptr, shape);
    EXPECT_TRUE(shape->has_flag(ShapeFlag::IsClassObject));
    EXPECT_FALSE(shape->has_flag(ShapeFlag::IsImmutable));
    ASSERT_EQ(7u, shape->property_count());
    EXPECT_EQ(5u, shape->present_count());
    EXPECT_EQ(2u, shape->latent_count());
    EXPECT_EQ(5, shape->get_next_slot_index());
    EXPECT_EQ(ClassObject::class_inline_storage_slot_count,
              shape->get_inline_slot_capacity());

    const cl_wchar *expected_names[] = {L"__class__", L"__dict__", L"__name__",
                                        L"__bases__", L"__mro__"};
    const int32_t expected_slots[] = {-1, -1,
                                      ClassObject::class_metadata_slot_name,
                                      ClassObject::class_metadata_slot_bases,
                                      ClassObject::class_metadata_slot_mro};
    for(uint32_t idx = 0; idx < std::size(expected_names); ++idx)
    {
        EXPECT_STREQ(expected_names[idx],
                     shape->get_property_name(idx).extract()->data);
        if(expected_slots[idx] < 0)
        {
            EXPECT_FALSE(shape->get_property_storage_location(idx).is_found());
        }
        else
        {
            EXPECT_EQ(StorageKind::Inline,
                      shape->get_property_storage_location(idx).kind);
            EXPECT_EQ(expected_slots[idx],
                      shape->get_property_storage_location(idx).physical_idx);
        }
        EXPECT_TRUE(
            shape->get_descriptor_info(idx).has_flag(DescriptorFlag::ReadOnly));
        EXPECT_TRUE(shape->get_descriptor_info(idx).has_flag(
            DescriptorFlag::StableSlot));
        if(idx == 0)
        {
            EXPECT_TRUE(shape->get_descriptor_info(idx).has_flag(
                DescriptorFlag::SpecialRead));
            EXPECT_TRUE(shape->get_descriptor_info(idx).has_flag(
                DescriptorFlag::SpecialMutate));
            EXPECT_EQ(DescriptorSpecialKind::ShapeClass,
                      shape->get_descriptor_info(idx).special_kind());
        }
        else if(idx == 1)
        {
            EXPECT_TRUE(shape->get_descriptor_info(idx).has_flag(
                DescriptorFlag::SpecialRead));
            EXPECT_TRUE(shape->get_descriptor_info(idx).has_flag(
                DescriptorFlag::SpecialMutate));
            EXPECT_EQ(DescriptorSpecialKind::SlotDict,
                      shape->get_descriptor_info(idx).special_kind());
        }
        else
        {
            EXPECT_FALSE(shape->get_descriptor_info(idx).has_flag(
                DescriptorFlag::SpecialRead));
            EXPECT_FALSE(shape->get_descriptor_info(idx).has_flag(
                DescriptorFlag::SpecialMutate));
        }
    }

    DescriptorLookup new_lookup =
        shape->lookup_descriptor_including_latent(dunder_new_name);
    ASSERT_TRUE(new_lookup.is_latent());
    EXPECT_EQ(int32_t(ClassObject::class_predefined_slot_new),
              new_lookup.info.physical_idx);
    EXPECT_TRUE(new_lookup.info.has_flag(DescriptorFlag::StableSlot));
    EXPECT_FALSE(new_lookup.info.has_flag(DescriptorFlag::ReadOnly));
    EXPECT_EQ(Value::not_present(), cls->get_own_property(dunder_new_name));
    EXPECT_EQ(Value::not_present(),
              cls->read_storage_location(new_lookup.info.storage_location()));

    DescriptorLookup init_lookup =
        shape->lookup_descriptor_including_latent(dunder_init_name);
    ASSERT_TRUE(init_lookup.is_latent());
    EXPECT_EQ(int32_t(ClassObject::class_predefined_slot_init),
              init_lookup.info.physical_idx);
    EXPECT_TRUE(init_lookup.info.has_flag(DescriptorFlag::StableSlot));
    EXPECT_FALSE(init_lookup.info.has_flag(DescriptorFlag::ReadOnly));
    EXPECT_EQ(Value::not_present(), cls->get_own_property(dunder_init_name));
    EXPECT_EQ(Value::not_present(),
              cls->read_storage_location(init_lookup.info.storage_location()));

    EXPECT_EQ(Value::not_present(), cls->get_own_property(dunder_class_name));
    EXPECT_EQ(cls_name.raw_value(), cls->get_own_property(dunder_name_name));

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
    Value readonly_values[] = {Value::not_present(), cls_name.raw_value(),
                               bases_value, mro_value};
    for(uint32_t idx = 0; idx < std::size(readonly_names); ++idx)
    {
        Shape *before_shape = cls->get_shape();
        Expected<bool> stored =
            cls->set_own_property(readonly_names[idx], other_name.raw_value());
        if(idx == 0)
        {
            // __class__ reaches the fallible actual-type mutation path.
            EXPECT_TRUE(stored.has_exception());
            EXPECT_TRUE(context.thread()->has_pending_exception());
            context.thread()->clear_pending_exception();
        }
        else
        {
            ASSERT_TRUE(stored.has_value());
            EXPECT_FALSE(stored.value());
            EXPECT_FALSE(context.thread()->has_pending_exception());
        }
        EXPECT_FALSE(cls->delete_own_property(readonly_names[idx]));
        EXPECT_EQ(before_shape, cls->get_shape());
        EXPECT_EQ(readonly_values[idx],
                  cls->get_own_property(readonly_names[idx]));
    }
    EXPECT_EQ(cls_name.raw_value(), cls->get_own_property(dunder_name_name));
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
        cls_name, 2, context.vm().object_class(), NativeLayoutId::Instance);

    DescriptorLookup latent_lookup =
        cls->get_shape()->lookup_descriptor_including_latent(dunder_init_name);
    ASSERT_TRUE(latent_lookup.is_latent());
    EXPECT_EQ(int32_t(ClassObject::class_predefined_slot_init),
              latent_lookup.info.physical_idx);
    EXPECT_EQ(Value::not_present(), cls->get_own_property(dunder_init_name));

    ASSERT_TRUE(
        cls->set_own_property(dunder_init_name, Value::from_smi(42)).value());
    DescriptorLookup present_lookup =
        cls->get_shape()->lookup_descriptor_including_latent(dunder_init_name);
    ASSERT_TRUE(present_lookup.is_present());
    EXPECT_EQ(int32_t(ClassObject::class_predefined_slot_init),
              present_lookup.info.physical_idx);
    EXPECT_EQ(Value::from_smi(42), cls->get_own_property(dunder_init_name));
    EXPECT_EQ(Value::from_smi(42), cls->read_storage_location(
                                       present_lookup.info.storage_location()));

    ASSERT_TRUE(cls->delete_own_property(dunder_init_name));
    DescriptorLookup deleted_lookup =
        cls->get_shape()->lookup_descriptor_including_latent(dunder_init_name);
    ASSERT_TRUE(deleted_lookup.is_latent());
    EXPECT_EQ(int32_t(ClassObject::class_predefined_slot_init),
              deleted_lookup.info.physical_idx);
    EXPECT_EQ(Value::not_present(), cls->get_own_property(dunder_init_name));
    EXPECT_EQ(
        Value::not_present(),
        cls->read_storage_location(deleted_lookup.info.storage_location()));
}

TEST(ClassObject, BuiltinClassFreezesAfterMethodInstallation)
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
        cls_name, 2, methods, 2, context.vm().object_class(),
        NativeLayoutId::Instance, SubclassPolicy::Allowed);

    Shape *shape = cls->get_shape();
    ASSERT_NE(nullptr, shape);
    EXPECT_TRUE(shape->has_flag(ShapeFlag::IsClassObject));
    EXPECT_FALSE(shape->has_flag(ShapeFlag::IsImmutable));
    EXPECT_TRUE(shape->allows_attribute_updates());
    EXPECT_TRUE(shape->allows_attribute_add_delete());
    ASSERT_EQ(2u, class_property_count(cls));
    EXPECT_STREQ(L"upper", class_property_name(cls, 0).extract()->data);
    EXPECT_STREQ(L"lower", class_property_name(cls, 1).extract()->data);
    EXPECT_EQ(Value::from_smi(11), class_property_value(cls, 0));
    EXPECT_EQ(Value::from_smi(23), class_property_value(cls, 1));

    for(uint32_t idx = ClassObject::class_metadata_slot_count;
        idx < shape->present_count(); ++idx)
    {
        DescriptorInfo info = shape->get_descriptor_info(idx);
        EXPECT_TRUE(info.has_flag(DescriptorFlag::ReadOnly));
        EXPECT_TRUE(info.has_flag(DescriptorFlag::StableSlot));
    }

    TValue<String> late_name(
        context.vm().get_or_create_interned_string_value(L"late"));
    EXPECT_TRUE(cls->define_own_property(
        late_name, Value::from_smi(31),
        descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot)));

    cls->freeze();
    Shape *before_shape = cls->get_shape();
    EXPECT_TRUE(before_shape->has_flag(ShapeFlag::IsImmutable));
    EXPECT_FALSE(before_shape->allows_attribute_updates());
    EXPECT_FALSE(before_shape->allows_attribute_add_delete());
    EXPECT_FALSE(
        cls->set_own_property(method_name, Value::from_smi(99)).value());
    EXPECT_FALSE(cls->delete_own_property(other_method_name));
    EXPECT_FALSE(
        cls->set_own_property(
               context.vm().get_or_create_interned_string_value(L"new_method"),
               Value::from_smi(47))
            .value());
    EXPECT_EQ(before_shape, cls->get_shape());
    EXPECT_EQ(Value::from_smi(11), cls->get_own_property(method_name));
    EXPECT_EQ(Value::from_smi(23), cls->get_own_property(other_method_name));
    EXPECT_EQ(Value::from_smi(31), cls->get_own_property(late_name));
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
        cls_name, 2, context.vm().object_class(), NativeLayoutId::Instance);

    EXPECT_FALSE(
        cls->set_existing_own_property(attr_name, Value::from_smi(1)).value());
    EXPECT_TRUE(
        cls->define_own_property(attr_name, Value::from_smi(1),
                                 descriptor_flag(DescriptorFlag::StableSlot)));
    EXPECT_EQ(Value::from_smi(1), cls->get_own_property(attr_name));
    EXPECT_FALSE(
        cls->define_own_property(attr_name, Value::from_smi(2),
                                 descriptor_flag(DescriptorFlag::StableSlot)));
    EXPECT_TRUE(
        cls->set_existing_own_property(attr_name, Value::from_smi(3)).value());
    EXPECT_EQ(Value::from_smi(3), cls->get_own_property(attr_name));
    EXPECT_FALSE(
        cls->set_existing_own_property(missing_name, Value::from_smi(4))
            .value());
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
        base_name, 2, context.vm().object_class(), NativeLayoutId::Instance);
    ClassObject *child = context.thread()->make_internal_raw<ClassObject>(
        child_name, 2, base, NativeLayoutId::Instance);

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

TEST(ClassObject, IsSubclassOfAndIsInstanceOfFollowMro)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> base_name(
        context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> child_name(
        context.vm().get_or_create_interned_string_value(L"Child"));
    TValue<String> sibling_name(
        context.vm().get_or_create_interned_string_value(L"Sibling"));
    ClassObject *base = context.thread()->make_internal_raw<ClassObject>(
        base_name, 2, context.vm().object_class(), NativeLayoutId::Instance);
    ClassObject *child = context.thread()->make_internal_raw<ClassObject>(
        child_name, 2, base, NativeLayoutId::Instance);
    ClassObject *sibling = context.thread()->make_internal_raw<ClassObject>(
        sibling_name, 2, context.vm().object_class(), NativeLayoutId::Instance);
    Instance *instance = context.thread()->make_internal_raw<Instance>(child);

    EXPECT_TRUE(is_subclass_of(child, child));
    EXPECT_TRUE(is_subclass_of(child, base));
    EXPECT_TRUE(is_subclass_of(child, context.vm().object_class()));
    EXPECT_FALSE(is_subclass_of(base, child));
    EXPECT_FALSE(is_subclass_of(child, sibling));

    EXPECT_TRUE(is_instance_of(instance, child));
    EXPECT_TRUE(is_instance_of(instance, base));
    EXPECT_TRUE(is_instance_of(instance, context.vm().object_class()));
    EXPECT_FALSE(is_instance_of(instance, sibling));
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
        base_name, 2, context.vm().object_class(), NativeLayoutId::Instance);
    ClassObject *child = context.thread()->make_internal_raw<ClassObject>(
        child_name, 2, base, NativeLayoutId::Instance);

    base->set_own_property(attr_name, Value::from_smi(7)).value();

    EXPECT_EQ(Value::from_smi(7), base->get_own_property(attr_name));
    EXPECT_EQ(Value::not_present(), child->get_own_property(attr_name));
    EXPECT_EQ(Value::from_smi(7), load_attr(Value::from_oop(child), attr_name));

    child->set_own_property(attr_name, Value::from_smi(8)).value();
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
        cls_name, 2, context.vm().object_class(), NativeLayoutId::Instance);

    Shape *root_shape = cls->get_shape();
    EXPECT_TRUE(root_shape->has_flag(ShapeFlag::IsClassObject));
    EXPECT_TRUE(cls->set_own_property(attr_name, Value::from_smi(1)).value());
    Shape *shape_with_attr = cls->get_shape();
    EXPECT_NE(root_shape, shape_with_attr);
    EXPECT_TRUE(shape_with_attr->has_flag(ShapeFlag::IsClassObject));
    EXPECT_EQ(Value::from_smi(1), cls->get_own_property(attr_name));

    StorageLocation location =
        shape_with_attr->resolve_present_property(attr_name);
    ASSERT_TRUE(location.is_found());
    EXPECT_TRUE(cls->set_own_property(attr_name, Value::from_smi(2)).value());
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
        cls_name, 2, context.vm().object_class(), NativeLayoutId::Instance);

    EXPECT_EQ(nullptr, cls->current_mro_shape_and_contents_validity_cell());

    ValidityCell *cell =
        cls->get_or_create_mro_shape_and_contents_validity_cell();

    ASSERT_NE(nullptr, cell);
    EXPECT_TRUE(cell->is_valid());
    EXPECT_EQ(ValidityCellDependencyMutability::Mutable,
              cell->dependency_mutability());
    EXPECT_EQ(cell, cls->current_mro_shape_and_contents_validity_cell());
}

TEST(ClassObject, FrozenBuiltinMroProducesImmutableValidityCell)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    ValidityCell *cell =
        context.vm()
            .float_class()
            ->get_or_create_mro_shape_and_contents_validity_cell();

    ASSERT_NE(nullptr, cell);
    EXPECT_EQ(ValidityCellDependencyMutability::Immutable,
              cell->dependency_mutability());

    ValidityCell *combined_cell =
        context.vm()
            .float_class()
            ->get_or_create_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell();
    ASSERT_NE(nullptr, combined_cell);
    EXPECT_EQ(ValidityCellDependencyMutability::Immutable,
              combined_cell->dependency_mutability());
}

TEST(ClassObject, FreezeInvalidatesMutableShapeValidityCells)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> cls_name(
        context.vm().get_or_create_interned_string_value(L"Cls"));
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        cls_name, 2, context.vm().object_class(), NativeLayoutId::Instance);
    ValidityCell *cell =
        cls->get_or_create_mro_shape_and_contents_validity_cell();
    ASSERT_NE(nullptr, cell);
    ASSERT_TRUE(cell->is_valid());
    ASSERT_EQ(ValidityCellDependencyMutability::Mutable,
              cell->dependency_mutability());

    cls->freeze();

    EXPECT_FALSE(cell->is_valid());
    EXPECT_EQ(nullptr, cls->current_mro_shape_and_contents_validity_cell());
    EXPECT_TRUE(cls->get_shape()->has_flag(ShapeFlag::IsImmutable));

    ValidityCell *frozen_cell =
        cls->get_or_create_mro_shape_and_contents_validity_cell();
    ASSERT_NE(nullptr, frozen_cell);
    EXPECT_EQ(ValidityCellDependencyMutability::Immutable,
              frozen_cell->dependency_mutability());
}

TEST(ClassObject, MutableBaseKeepsFrozenChildValidityCellMutable)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());

    TValue<String> base_name(
        context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> child_name(
        context.vm().get_or_create_interned_string_value(L"Child"));
    ClassObject *base = context.thread()->make_internal_raw<ClassObject>(
        base_name, 2, context.vm().object_class(), NativeLayoutId::Instance);
    ClassObject *child = context.thread()->make_internal_raw<ClassObject>(
        child_name, 2, base, NativeLayoutId::Instance);
    child->freeze();

    ValidityCell *cell =
        child->get_or_create_mro_shape_and_contents_validity_cell();
    ASSERT_NE(nullptr, cell);
    EXPECT_EQ(ValidityCellDependencyMutability::Mutable,
              cell->dependency_mutability());

    base->freeze();

    EXPECT_FALSE(cell->is_valid());
    ValidityCell *frozen_cell =
        child->get_or_create_mro_shape_and_contents_validity_cell();
    ASSERT_NE(nullptr, frozen_cell);
    EXPECT_EQ(ValidityCellDependencyMutability::Immutable,
              frozen_cell->dependency_mutability());
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
        base_name, 2, context.vm().object_class(), NativeLayoutId::Instance);
    ClassObject *child = context.thread()->make_internal_raw<ClassObject>(
        child_name, 2, base, NativeLayoutId::Instance);

    ValidityCell *child_cell =
        child->get_or_create_mro_shape_and_contents_validity_cell();
    ASSERT_NE(nullptr, child_cell);
    ASSERT_TRUE(child_cell->is_valid());
    EXPECT_EQ(1u, base->attached_mro_shape_and_contents_validity_cell_count());

    EXPECT_TRUE(base->set_own_property(attr_name, Value::from_smi(1)).value());

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
        cls_name, 2, context.vm().object_class(), NativeLayoutId::Instance);

    ValidityCell *cell =
        cls->get_or_create_mro_shape_and_contents_validity_cell();
    ASSERT_NE(nullptr, cell);
    ASSERT_TRUE(cell->is_valid());

    EXPECT_TRUE(cls->set_own_property(attr_name, Value::from_smi(1)).value());

    EXPECT_FALSE(cell->is_valid());
    EXPECT_EQ(nullptr, cls->current_mro_shape_and_contents_validity_cell());

    ValidityCell *updated_cell =
        cls->get_or_create_mro_shape_and_contents_validity_cell();
    ASSERT_NE(nullptr, updated_cell);
    ASSERT_TRUE(updated_cell->is_valid());

    EXPECT_TRUE(cls->set_own_property(attr_name, Value::from_smi(2)).value());

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
        base_name, 2, context.vm().object_class(), NativeLayoutId::Instance);
    ClassObject *child = context.thread()->make_internal_raw<ClassObject>(
        child_name, 2, base, NativeLayoutId::Instance);

    ValidityCell *first_cell =
        child->get_or_create_mro_shape_and_contents_validity_cell();
    ASSERT_NE(nullptr, first_cell);
    ASSERT_TRUE(first_cell->is_valid());
    EXPECT_EQ(1u, base->attached_mro_shape_and_contents_validity_cell_count());

    EXPECT_TRUE(child->set_own_property(attr_name, Value::from_smi(1)).value());
    EXPECT_FALSE(first_cell->is_valid());
    EXPECT_EQ(1u, base->attached_mro_shape_and_contents_validity_cell_count());

    ValidityCell *second_cell =
        child->get_or_create_mro_shape_and_contents_validity_cell();
    ASSERT_NE(nullptr, second_cell);
    EXPECT_NE(first_cell, second_cell);
    EXPECT_TRUE(second_cell->is_valid());
    EXPECT_EQ(1u, base->attached_mro_shape_and_contents_validity_cell_count());

    EXPECT_TRUE(base->set_own_property(attr_name, Value::from_smi(2)).value());
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
        context.vm().type_class(), meta_name,
        ClassObject::class_inline_storage_slot_count, context.vm().type_class(),
        NativeLayoutId::ClassObject);
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        meta, cls_name, 2, context.vm().object_class(),
        NativeLayoutId::Instance);

    EXPECT_EQ(
        nullptr,
        cls->current_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell());
    EXPECT_EQ(0u, meta->attached_mro_shape_and_contents_validity_cell_count());

    ValidityCell *cell =
        cls->get_or_create_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell();

    ASSERT_NE(nullptr, cell);
    EXPECT_TRUE(cell->is_valid());
    EXPECT_EQ(ValidityCellDependencyMutability::Mutable,
              cell->dependency_mutability());
    EXPECT_EQ(
        cell,
        cls->current_mro_shape_and_metaclass_mro_shape_and_contents_validity_cell());
    EXPECT_EQ(1u, meta->attached_mro_shape_and_contents_validity_cell_count());

    EXPECT_TRUE(meta->set_own_property(attr_name, Value::from_smi(1)).value());

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
        base_name, 2, context.vm().object_class(), NativeLayoutId::Instance);
    ClassObject *child = context.thread()->make_internal_raw<ClassObject>(
        child_name, 2, base, NativeLayoutId::Instance);

    base->set_own_property(method_name, Value::from_smi(7)).value();

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
        base_name, 2, context.vm().object_class(), NativeLayoutId::Instance);
    ClassObject *child = context.thread()->make_internal_raw<ClassObject>(
        child_name, 2, base, NativeLayoutId::Instance);
    DescriptorFlags flags = descriptor_flag(DescriptorFlag::StableSlot);

    base->set_own_property(attr_name, Value::from_smi(7)).value();
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

TEST(InstanceStorageSelection, OSErrorPreservesProviderAndCapacity)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    ClassObject *os = context.thread()->class_for_builtin_name(L"OSError");
    ClassObject *exception =
        context.thread()->class_for_builtin_name(L"Exception");
    auto ordinary = make_storage_selection_class(context, {exception});
    ASSERT_FALSE(ordinary.has_exception());
    Owned<TValue<ClassObject>> e(ordinary.value());
    for(bool first: {false, true})
    {
        auto candidate =
            first ? make_storage_selection_class(context, {e.extract(), os})
                  : make_storage_selection_class(context, {os, e.extract()});
        ASSERT_FALSE(candidate.has_exception());
        Owned<TValue<ClassObject>> cls(candidate.value());
        auto selected = select_instance_storage(
            TValue<Tuple>::from_value_assumed(cls.extract()->get_mro_value()),
            10);
        ASSERT_FALSE(selected.has_exception());
        EXPECT_EQ(os, selected.value().builtin_provider);
        EXPECT_EQ(NativeLayoutId::OSError, selected.value().layout);
        EXPECT_EQ(OSErrorObject::kInlineSlotCount,
                  selected.value().inline_slot_capacity);
    }
}

TEST(ConstructorAllocatorSelection,
     OSErrorUsesDynamicClassAllocationAndLatentSlot)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    ClassObject *os = context.thread()->class_for_builtin_name(L"OSError");
    auto thunk = os->get_or_create_constructor_thunk();
    ASSERT_FALSE(thunk.has_exception());
    ASSERT_TRUE(thunk.value().is_found());
    BytecodeDecoder decoder(*thunk.value().thunk->code_object.extract());
    bool found = false;
    for(const auto &block: decoder.blocks())
        for(const auto &instruction: block.instructions())
            if(instruction.semantic_opcode() == Bytecode::CreateOSError)
                found = true;
    EXPECT_TRUE(found);
    auto descriptor =
        os->get_instance_root_shape()->lookup_descriptor_including_latent(
            context.vm().get_or_create_interned_string_value(
                L"characters_written"));
    EXPECT_TRUE(descriptor.is_latent());
    EXPECT_EQ(int32_t(OSErrorObject::kCharactersWrittenSlot),
              descriptor.info.physical_idx);
    EXPECT_TRUE(descriptor.info.has_flag(DescriptorFlag::StableSlot));
    for(const wchar_t *name: {L"errno", L"strerror", L"filename", L"filename2"})
    {
        auto field =
            os->get_instance_root_shape()->lookup_descriptor_including_latent(
                context.vm().get_or_create_interned_string_value(name));
        EXPECT_TRUE(field.is_latent());
        EXPECT_TRUE(field.info.has_flag(DescriptorFlag::StableSlot));
        EXPECT_TRUE(
            os->get_own_property(
                  context.vm().get_or_create_interned_string_value(name))
                .is_none());
    }
}

TEST(ConstructorAllocatorSelection, OSErrorRebuildsDoNotAllocateImmortalHelpers)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    auto candidate = make_storage_selection_class(
        context, {context.thread()->class_for_builtin_name(L"OSError")});
    ASSERT_FALSE(candidate.has_exception());
    Owned<TValue<ClassObject>> cls(candidate.value());
    auto warm = cls.extract()->get_or_create_constructor_thunk();
    ASSERT_FALSE(warm.has_exception());
    ASSERT_TRUE(warm.value().is_found());
    auto &immortal_heap = context.vm().get_interned_global_heap();
    uint64_t immortal_before = immortal_heap.count_valid_objects_slow();
    for(int i = 0; i < 100; ++i)
    {
        // Python initializer reassignment uses this same invalidation path.
        cls.extract()->invalidate_lookup_validity_cells_for_contents_change();
        auto lookup = cls.extract()->get_or_create_constructor_thunk();
        ASSERT_FALSE(lookup.has_exception());
        ASSERT_TRUE(lookup.value().is_found());
        CodeObject *code = lookup.value().thunk->code_object.extract();
        uint32_t helpers = 0;
        for(size_t index = 0; index < code->constant_table.size(); ++index)
        {
            Value constant = code->constant_table[index].value();
            if(has_native_layout<CodeObject>(constant))
            {
                EXPECT_TRUE(constant.is_refcounted_ptr());
                ++helpers;
            }
        }
        EXPECT_EQ(2u, helpers);
        context.vm().run_heap_reclamation();
    }
    EXPECT_EQ(immortal_before, immortal_heap.count_valid_objects_slow());
}
