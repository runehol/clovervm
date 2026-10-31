#include "builtin_types/float.h"
#include "memory/heap_reference_slot.h"
#include "memory/native_layout_descriptor.h"
#include "object_model/instance.h"
#include "object_model/overflow_slots.h"
#include "object_model/owned.h"
#include "object_model/shape.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

using namespace cl;

TEST(HeapReferenceSlot, IgnoresImmediatesAndNullWithoutChangingBits)
{
    for(Value value:
        {Value::from_smi(0), Value::from_smi(-1),
         Value::from_smi(value_smi_min), Value::from_smi(value_smi_max),
         Value::True(), Value::False(), Value::None(), Value::NotImplemented(),
         Value::Ellipsis(), Value::not_present(), Value::exception_marker()})
    {
        Value original = value;
        EXPECT_EQ(nullptr, HeapReferenceSlot(&value).load_pointer());
        EXPECT_EQ(original, value);
    }

    HeapObject *pointer = nullptr;
    EXPECT_EQ(nullptr, HeapReferenceSlot(&pointer).load_pointer());
    EXPECT_EQ(nullptr, pointer);
}

TEST(HeapReferenceSlot, RewritesTaggedRawAndWrappedReferencesWithoutOwnership)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Owned<TValue<Float>> original(
        context.thread()->make_object_value<Float>(1));
    Owned<TValue<Float>> replacement(
        context.thread()->make_object_value<Float>(2));

    Value value = original.raw_value();
    Float *pointer = original.extract();
    Member<Value> member_value(original.raw_value());
    Member<TValue<Float>> member_typed_value(original.value());
    MemberHeapPtr<Float> member_pointer(original.extract());
    int32_t original_refcount = original.extract()->refcount;
    int32_t replacement_refcount = replacement.extract()->refcount;
    size_t zero_count_table_size = context.thread()->zero_count_table_size();
    bool original_in_zct =
        context.thread()->zero_count_table_contains_for_testing(
            original.extract());
    bool replacement_in_zct =
        context.thread()->zero_count_table_contains_for_testing(
            replacement.extract());

    for(void *address:
        std::array<void *, 5>{&value, &pointer, &member_value,
                              &member_typed_value, &member_pointer})
    {
        HeapReferenceSlot slot(address);
        EXPECT_EQ(original.extract(), slot.load_pointer());
        slot.rewrite_pointer(replacement.extract());
        EXPECT_EQ(replacement.extract(), slot.load_pointer());
        EXPECT_EQ(original_refcount, original.extract()->refcount);
        EXPECT_EQ(replacement_refcount, replacement.extract()->refcount);
        EXPECT_EQ(zero_count_table_size,
                  context.thread()->zero_count_table_size());
        EXPECT_EQ(original_in_zct,
                  context.thread()->zero_count_table_contains_for_testing(
                      original.extract()));
        EXPECT_EQ(replacement_in_zct,
                  context.thread()->zero_count_table_contains_for_testing(
                      replacement.extract()));
        // These are simulated relocations: restore the actual ownership graph
        // before ordinary refcount teardown can observe the substitutions.
        slot.rewrite_pointer(original.extract());
    }

    EXPECT_EQ(original.raw_value(), value);
    EXPECT_EQ(original.extract(), pointer);
    EXPECT_EQ(original.raw_value(), member_value.raw_value());
    EXPECT_EQ(original.extract(), member_typed_value.extract());
    EXPECT_EQ(original.extract(), member_pointer.get());
    member_value.release_ref();
    member_typed_value.release_ref();
    member_pointer.release_ref();
}

TEST(HeapReferenceSlot, PreservesInternedPointerEncoding)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    auto original = context.vm().get_or_create_interned_string_value(L"old");
    auto replacement = context.vm().get_or_create_interned_string_value(L"new");
    ASSERT_TRUE(original.raw_value().is_interned_ptr());
    ASSERT_TRUE(replacement.raw_value().is_interned_ptr());
    Value value = original.raw_value();
    String *pointer = original.extract();
    int32_t original_refcount = original.extract()->refcount;
    int32_t replacement_refcount = replacement.extract()->refcount;

    for(void *address: std::array<void *, 2>{&value, &pointer})
    {
        HeapReferenceSlot slot(address);
        EXPECT_EQ(original.extract(), slot.load_pointer());
        slot.rewrite_pointer(replacement.extract());
        EXPECT_EQ(replacement.extract(), slot.load_pointer());
    }
    EXPECT_EQ(replacement.raw_value(), value);
    EXPECT_EQ(replacement.extract(), pointer);
    EXPECT_EQ(original_refcount, original.extract()->refcount);
    EXPECT_EQ(replacement_refcount, replacement.extract()->refcount);
}

TEST(HeapReferenceSlot, WalksMixedInstanceFieldsAndOnlyInitializedInlineCells)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    auto cls_name = context.vm().get_or_create_interned_string_value(L"Slots");
    auto pointer_name = context.vm().get_or_create_interned_string_value(L"p");
    auto integer_name = context.vm().get_or_create_interned_string_value(L"n");
    ClassObject *cls = thread->make_internal_raw<ClassObject>(
        cls_name, 4, context.vm().object_class(), NativeLayoutId::Instance);
    Instance *instance = thread->make_internal_raw<Instance>(cls);
    Owned<TValue<Float>> original(thread->make_object_value<Float>(1));
    Owned<TValue<Float>> replacement(thread->make_object_value<Float>(2));
    ASSERT_TRUE(
        instance->set_own_property(pointer_name, original.raw_value()).value());
    ASSERT_TRUE(
        instance->set_own_property(integer_name, Value::from_smi(42)).value());
    OwnedHeapPtr<OverflowSlots> original_overflow(
        thread->make_internal_raw<OverflowSlots>(0, 1));
    OwnedHeapPtr<OverflowSlots> replacement_overflow(
        thread->make_internal_raw<OverflowSlots>(0, 1));
    instance->overflow_storage = original_overflow.get();
    Shape *original_shape = instance->get_shape();
    OwnedHeapPtr<Shape> replacement_shape(
        original_shape->clone_with_flags(original_shape->flags()));

    // Poison the unused tail with borrowed pointer bits. It is outside the
    // initialized prefix, so visiting or rewriting those cells is a bug.
    instance->inline_slot_base()[2] = original.raw_value();
    instance->inline_slot_base()[3] = original.raw_value();
    ASSERT_EQ(2u, instance->native_layout_aux_count_value());
    auto span = resolve_ordinary_reference_span(
        instance,
        native_layout_descriptor_for(Instance::native_layout).release);
    ASSERT_TRUE(span.has_value());
    std::array<HeapObject *, 3> sources{original_shape, original_overflow.get(),
                                        original.extract()};
    std::array<HeapObject *, 3> targets{replacement_shape.get(),
                                        replacement_overflow.get(),
                                        replacement.extract()};
    std::array<int32_t, 3> source_refcounts;
    std::array<int32_t, 3> target_refcounts;
    std::array<bool, 3> sources_in_zct;
    std::array<bool, 3> targets_in_zct;
    for(size_t idx = 0; idx < sources.size(); ++idx)
    {
        source_refcounts[idx] = sources[idx]->refcount;
        target_refcounts[idx] = targets[idx]->refcount;
        sources_in_zct[idx] =
            thread->zero_count_table_contains_for_testing(sources[idx]);
        targets_in_zct[idx] =
            thread->zero_count_table_contains_for_testing(targets[idx]);
    }
    size_t zero_count_table_size = thread->zero_count_table_size();
    size_t cells = 0;
    size_t rewrites = 0;
    visit_strong_reference_slots(instance, [&](HeapReferenceSlot slot) {
        ++cells;
        HeapObject *child = slot.load_pointer();
        for(size_t idx = 0; idx < sources.size(); ++idx)
        {
            if(child == sources[idx])
            {
                slot.rewrite_pointer(targets[idx]);
                ++rewrites;
                break;
            }
        }
    });

    EXPECT_EQ(4u, cells);
    EXPECT_EQ(3u, rewrites);
    EXPECT_EQ(replacement_shape.get(), instance->get_shape());
    EXPECT_EQ(replacement_overflow.get(), instance->overflow_storage.get());
    EXPECT_EQ(replacement.raw_value(), instance->inline_slot_base()[0]);
    EXPECT_EQ(Value::from_smi(42), instance->inline_slot_base()[1]);
    EXPECT_EQ(original.raw_value(), instance->inline_slot_base()[2]);
    EXPECT_EQ(original.raw_value(), instance->inline_slot_base()[3]);
    EXPECT_EQ(zero_count_table_size, thread->zero_count_table_size());
    for(size_t idx = 0; idx < sources.size(); ++idx)
    {
        EXPECT_EQ(source_refcounts[idx], sources[idx]->refcount);
        EXPECT_EQ(target_refcounts[idx], targets[idx]->refcount);
        EXPECT_EQ(sources_in_zct[idx],
                  thread->zero_count_table_contains_for_testing(sources[idx]));
        EXPECT_EQ(targets_in_zct[idx],
                  thread->zero_count_table_contains_for_testing(targets[idx]));
    }

    visit_strong_reference_slots(instance, [&](HeapReferenceSlot slot) {
        HeapObject *child = slot.load_pointer();
        for(size_t idx = 0; idx < targets.size(); ++idx)
        {
            if(child == targets[idx])
            {
                slot.rewrite_pointer(sources[idx]);
                break;
            }
        }
    });
    EXPECT_EQ(original_shape, instance->get_shape());
    EXPECT_EQ(original_overflow.get(), instance->overflow_storage.get());
    EXPECT_EQ(original.raw_value(), instance->inline_slot_base()[0]);
    instance->inline_slot_base()[2] = Value::not_present();
    instance->inline_slot_base()[3] = Value::not_present();
}

#ifndef NDEBUG
TEST(HeapReferenceSlotDeathTest, RejectsInvalidRelocationWrites)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation(context.thread());
    Float *object = context.thread()->make_object_raw<Float>(1);
    Value reference = Value::from_oop(object);
    Value immediate = Value::from_smi(1);
    auto interned =
        context.vm().get_or_create_interned_string_value(L"interned");

    EXPECT_DEATH(HeapReferenceSlot(&immediate).rewrite_pointer(object),
                 "current != nullptr");
    EXPECT_DEATH(HeapReferenceSlot(&reference).rewrite_pointer(nullptr),
                 "replacement != nullptr");
    EXPECT_DEATH(
        HeapReferenceSlot(&reference).rewrite_pointer(interned.extract()),
        "value_ptr_mask");
}
#endif
