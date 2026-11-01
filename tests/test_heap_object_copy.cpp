#include "builtin_types/float.h"
#include "builtin_types/tuple.h"
#include "memory/heap_constants.h"
#include "memory/heap_object_copy.h"
#include "memory/heap_space.h"
#include "memory/native_layout_descriptor.h"
#include "object_model/class_object.h"
#include "object_model/instance.h"
#include "object_model/overflow_slots.h"
#include "object_model/shape.h"
#include "object_model/vm_array_backing.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace cl;

namespace
{
    HeapObject *expect_dynamic_copy(HeapSpace *destination,
                                    const HeapObject *source,
                                    size_t allocated_size, size_t copy_size)
    {
        NativeLayoutInfo layout = native_layout_info(source);
        EXPECT_EQ(allocated_size, layout.allocated_size);
        EXPECT_EQ(copy_size, layout.copy_size);
        std::vector<std::byte> before(copy_size);
        std::memcpy(before.data(), source, copy_size);

        HeapObject *copy = copy_object_bytes(destination, source);
        EXPECT_NE(source, copy);
        EXPECT_EQ(0, std::memcmp(before.data(), copy, copy_size));
        EXPECT_EQ(0, std::memcmp(before.data(), source, copy_size));
        EXPECT_FALSE(source->is_forwarded());

        // These small fixtures fit in one fresh slab. Allocation spacing must
        // include reserved capacity, even when only a prefix was copied.
        uintptr_t next = reinterpret_cast<uintptr_t>(destination->allocate(1));
        size_t rounded = (allocated_size + value_ptr_granularity - 1) &
                         ~(value_ptr_granularity - 1);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(copy) + rounded, next);
        // Do not inspect the uncopied tail: it is still uninitialized storage.
        return copy;
    }
}  // namespace

TEST(HeapObjectCopy, PreservesExactFloatBitsAndHeaderWithoutChangingSource)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    HeapSpace destination;
    for(uint64_t bits:
        {uint64_t{0}, uint64_t{0x8000000000000000},
         uint64_t{0x3ff8000000000000}, uint64_t{0xbfe3333333333333},
         uint64_t{1}, uint64_t{0x7ff0000000000000},
         uint64_t{0xfff0000000000000}, uint64_t{0x7ff8000000000042},
         uint64_t{0xfff8000000000123}})
    {
        SCOPED_TRACE(bits);
        Owned<TValue<Float>> source(context.thread()->make_object_value<Float>(
            std::bit_cast<double>(bits)));
        std::array<std::byte, sizeof(Float)> before;
        std::memcpy(before.data(), source.extract(), before.size());

        // This is a borrowed byte copy, never an additional RC owner.
        Float *copy = static_cast<Float *>(
            copy_object_bytes(&destination, source.extract()));
        EXPECT_NE(source.extract(), copy);
        EXPECT_EQ(value_refcounted_ptr_tag,
                  reinterpret_cast<uintptr_t>(copy) & value_tag_mask);
        EXPECT_EQ(bits, std::bit_cast<uint64_t>(copy->value()));
        EXPECT_EQ(source.extract()->get_shape(), copy->get_shape());
        EXPECT_EQ(nullptr, copy->overflow_storage.get());
        EXPECT_EQ(0, std::memcmp(before.data(), copy, before.size()));
        EXPECT_EQ(0,
                  std::memcmp(before.data(), source.extract(), before.size()));
        EXPECT_FALSE(source.extract()->is_forwarded());
    }
}

TEST(HeapObjectCopy, CopiesMemberPointersWithoutRetainingOrReleasingThem)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Owned<TValue<Float>> source(
        context.thread()->make_object_value<Float>(2.5));
    OverflowSlots *overflow =
        context.thread()->make_internal_raw<OverflowSlots>(0u, 2u);
    // Exercise the inherited member's retaining-copy hazard without requiring
    // an attribute mutation on an immutable Float. Slot lookup is not involved.
    source.extract()->overflow_storage = overflow;
    int32_t source_refs = source.extract()->refcount();
    int32_t overflow_refs = overflow->refcount();
    Shape *shape = source.extract()->get_shape();
    int32_t shape_refs = shape->refcount();
    {
        HeapSpace destination;
        Float *first = static_cast<Float *>(
            copy_object_bytes(&destination, source.extract()));
        Float *second = static_cast<Float *>(
            copy_object_bytes(&destination, source.extract()));
        EXPECT_NE(first, second);
        EXPECT_EQ(overflow, first->overflow_storage.get());
        EXPECT_EQ(overflow, second->overflow_storage.get());
        EXPECT_EQ(shape, first->get_shape());
        EXPECT_EQ(source_refs, source.extract()->refcount());
        EXPECT_EQ(overflow_refs, overflow->refcount());
        EXPECT_EQ(shape_refs, shape->refcount());
    }
    // Discarding raw destination storage must not destroy copied members.
    EXPECT_EQ(source_refs, source.extract()->refcount());
    EXPECT_EQ(overflow_refs, overflow->refcount());
    EXPECT_EQ(shape_refs, shape->refcount());
    EXPECT_EQ(2.5, source.extract()->value());
}

TEST(HeapObjectCopy, CopiesSurviveDestinationSlabGrowth)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Owned<TValue<Float>> source(
        context.thread()->make_object_value<Float>(1.5));
    HeapSpace destination;
    Float *first =
        static_cast<Float *>(copy_object_bytes(&destination, source.extract()));
    // More than two slabs of Float copies, without constructing VM sources in
    // the loop or registering any destination with RC reclamation.
    for(size_t idx = 0; idx < 2 * DefaultSlabSize / sizeof(Float); ++idx)
    {
        Float *copy = static_cast<Float *>(
            copy_object_bytes(&destination, source.extract()));
        EXPECT_NE(first, copy);
        EXPECT_EQ(1.5, copy->value());
        EXPECT_EQ(source.extract()->get_shape(), copy->get_shape());
    }
    EXPECT_EQ(1.5, first->value());
    EXPECT_EQ(source.extract()->get_shape(), first->get_shape());
}

TEST(HeapObjectCopy, CopiesEmptyAndNonemptyTuples)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Owned<TValue<Float>> child(context.thread()->make_object_value<Float>(3.5));
    for(size_t count: {size_t{0}, size_t{1}, size_t{5}})
    {
        SCOPED_TRACE(count);
        Owned<TValue<Tuple>> source(
            context.thread()->make_object_value<Tuple>(count));
        for(size_t idx = 0; idx < count; ++idx)
        {
            source.extract()->initialize_item_unchecked(idx, child.raw_value());
        }
        int32_t child_refs = child.extract()->refcount();
        HeapSpace destination;
        size_t allocated = Tuple::allocation_size_for(count);
        Tuple *copy = static_cast<Tuple *>(
            expect_dynamic_copy(&destination, source.extract(), allocated,
                                allocated - (count == 0 ? sizeof(Value) : 0)));
        EXPECT_EQ(count, copy->size());
        for(size_t idx = 0; idx < count; ++idx)
        {
            EXPECT_EQ(child.raw_value(), copy->item_unchecked(idx));
        }
        EXPECT_EQ(child_refs, child.extract()->refcount());
    }
}

TEST(HeapObjectCopy, CopiesOverflowCapacityNotJustLogicalSize)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    for(uint32_t capacity: {0u, 1u, 5u})
    {
        SCOPED_TRACE(capacity);
        OwnedHeapPtr<OverflowSlots> source(
            context.thread()->make_internal_raw<OverflowSlots>(0u, capacity));
        if(capacity != 0)
        {
            source->set(capacity - 1, Value::from_smi(42));
        }
        HeapSpace destination;
        size_t allocated = OverflowSlots::allocation_size_for(capacity);
        OverflowSlots *copy = static_cast<OverflowSlots *>(expect_dynamic_copy(
            &destination, source.get(), allocated,
            allocated - (capacity == 0 ? sizeof(Value) : 0)));
        EXPECT_EQ(0u, copy->get_size());
        EXPECT_EQ(capacity, copy->get_capacity());
        for(uint32_t idx = 0; idx < capacity; ++idx)
        {
            EXPECT_EQ(idx + 1 == capacity ? Value::from_smi(42)
                                          : Value::not_present(),
                      copy->get(idx));
        }
    }
}

TEST(HeapObjectCopy, ReservesInstanceCapacityButCopiesOnlyInitializedPrefix)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    TValue<String> name =
        context.vm().get_or_create_interned_string_value(L"CopyInstance");
    ClassObject *cls = context.thread()->make_internal_raw<ClassObject>(
        name, 7, context.vm().object_class(), NativeLayoutId::Instance);
    OwnedHeapPtr<Instance> source(
        context.thread()->make_internal_raw<Instance>(cls));
    // The source's reserved tail is deliberately never initialized or read.
    for(uint16_t initialized: {uint16_t{0}, uint16_t{1}, uint16_t{2}})
    {
        SCOPED_TRACE(initialized);
        if(initialized != 0)
        {
            TValue<String> attr =
                context.vm().get_or_create_interned_string_value(
                    initialized == 1 ? L"a" : L"b");
            ASSERT_TRUE(
                source->set_own_property(attr, Value::from_smi(initialized))
                    .value());
        }
        ASSERT_EQ(initialized, source->native_layout_aux_count_value());
        HeapSpace destination;
        Instance *copy = static_cast<Instance *>(expect_dynamic_copy(
            &destination, source.get(), Instance::allocation_size_for(7u),
            sizeof(Instance) + initialized * sizeof(Value)));
        EXPECT_EQ(7u, copy->get_shape()->get_inline_slot_capacity());
        EXPECT_EQ(initialized, copy->native_layout_aux_count_value());
        for(uint16_t idx = 0; idx < initialized; ++idx)
        {
            EXPECT_EQ(Value::from_smi(idx + 1), copy->inline_slot_base()[idx]);
        }
    }
}

TEST(HeapObjectCopy, CopiesLiveValueArrayBackings)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Owned<TValue<Float>> child(context.thread()->make_object_value<Float>(2.5));
    for(size_t count: {size_t{0}, size_t{1}, size_t{5}})
    {
        SCOPED_TRACE(count);
        OwnedHeapPtr<ValueArrayBacking> source(
            context.thread()->make_internal_raw<ValueArrayBacking>(count));
        for(size_t idx = 0; idx < count; ++idx)
        {
            source->elements[idx] = incref(child.raw_value());
        }
        int32_t child_refs = child.extract()->refcount();
        HeapSpace destination;
        size_t allocated = ValueArrayBacking::allocation_size_for(count);
        ValueArrayBacking *copy = static_cast<ValueArrayBacking *>(
            expect_dynamic_copy(&destination, source.get(), allocated,
                                allocated - (count == 0 ? sizeof(Value) : 0)));
        EXPECT_EQ(count, copy->value_cell_count());
        for(size_t idx = 0; idx < count; ++idx)
        {
            EXPECT_EQ(child.raw_value(), copy->elements[idx]);
        }
        EXPECT_EQ(child_refs, child.extract()->refcount());
    }
}

TEST(HeapObjectCopy, CopiesLiveHeapPointerArrayBackings)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    Owned<TValue<Float>> child(context.thread()->make_object_value<Float>(2.5));
    for(size_t count: {size_t{0}, size_t{1}, size_t{5}})
    {
        SCOPED_TRACE(count);
        OwnedHeapPtr<HeapPtrArrayBacking> source(
            context.thread()->make_internal_raw<HeapPtrArrayBacking>(count));
        for(size_t idx = 0; idx < count; ++idx)
        {
            incref_heap_ptr(child.extract());
            source->elements[idx] = child.extract();
        }
        int32_t child_refs = child.extract()->refcount();
        HeapSpace destination;
        size_t allocated = HeapPtrArrayBacking::allocation_size_for(count);
        HeapPtrArrayBacking *copy =
            static_cast<HeapPtrArrayBacking *>(expect_dynamic_copy(
                &destination, source.get(), allocated,
                allocated - (count == 0 ? sizeof(HeapObject *) : 0)));
        EXPECT_EQ(count, copy->value_cell_count());
        for(size_t idx = 0; idx < count; ++idx)
        {
            EXPECT_EQ(child.extract(), copy->elements[idx]);
        }
        EXPECT_EQ(child_refs, child.extract()->refcount());
    }
}

TEST(HeapObjectCopy, CopiesCompleteRawBackingExtent)
{
    test::VmTestContext context;
    ThreadState::ActivationScope activation_scope(context.thread());
    for(size_t count: {size_t{0}, size_t{1}, size_t{129}})
    {
        SCOPED_TRACE(count);
        OwnedHeapPtr<RawArrayBacking> source(
            context.thread()->make_internal_raw<RawArrayBacking>(count));
        for(size_t idx = 0; idx < count; ++idx)
        {
            source->bytes[idx] = static_cast<uint8_t>(idx ^ 0xa5);
        }
        HeapSpace destination;
        size_t allocated = RawArrayBacking::allocation_size_for(count);
        RawArrayBacking *copy =
            static_cast<RawArrayBacking *>(expect_dynamic_copy(
                &destination, source.get(), allocated, allocated));
        EXPECT_EQ(count, copy->storage_size_in_bytes());
        for(size_t idx = 0; idx < count; ++idx)
        {
            EXPECT_EQ(static_cast<uint8_t>(idx ^ 0xa5), copy->bytes[idx]);
        }
    }
}
