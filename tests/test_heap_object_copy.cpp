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
#include <initializer_list>
#include <utility>
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

    void repair_copies(
        ThreadState *thread,
        std::initializer_list<std::pair<HeapObject *, HeapObject *>> copies)
    {
        std::vector<std::vector<std::byte>> originals;
        for(auto [source, copy]: copies)
        {
            (void)copy;
            originals.emplace_back(native_layout_info(source).copy_size);
            std::memcpy(originals.back().data(), source,
                        originals.back().size());
        }
        size_t zct_size = thread->zero_count_table_size();
        for(auto [source, copy]: copies)
        {
            (void)source;
            visit_strong_reference_slots(copy, [&](HeapReferenceSlot slot) {
                HeapObject *current = slot.load_pointer();
                for(auto [old_object, new_object]: copies)
                {
                    if(current == old_object)
                    {
                        slot.rewrite_pointer(new_object);
                        return;
                    }
                }
            });
        }
        size_t idx = 0;
        for(auto [source, copy]: copies)
        {
            (void)copy;
            EXPECT_EQ(0, std::memcmp(originals[idx].data(), source,
                                     originals[idx].size()));
            ++idx;
        }
        EXPECT_EQ(zct_size, thread->zero_count_table_size());
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

TEST(HeapObjectCopy, RepairsSharedTaggedAndRawReferencesInCopies)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    Owned<TValue<Float>> child(thread->make_object_value<Float>(4.5));
    Owned<TValue<Tuple>> tuple(thread->make_object_value<Tuple>(5u));
    TValue<String> interned =
        context.vm().get_or_create_interned_string_value(L"unmoved");
    std::array<Value, 5> values = {child.raw_value(), child.raw_value(),
                                   Value::from_smi(42), Value::None(),
                                   interned.raw_value()};
    for(size_t idx = 0; idx < values.size(); ++idx)
    {
        tuple.extract()->initialize_item_unchecked(idx, values[idx]);
    }
    OwnedHeapPtr<HeapPtrArrayBacking> pointers(
        thread->make_internal_raw<HeapPtrArrayBacking>(3u));
    pointers->elements[0] = child.extract();
    pointers->elements[1] = child.extract();
    pointers->elements[2] = nullptr;
    incref_heap_ptr(child.extract());
    incref_heap_ptr(child.extract());
    int32_t child_refs = child.extract()->refcount();

    HeapSpace destination;
    Float *new_child =
        static_cast<Float *>(copy_object_bytes(&destination, child.extract()));
    Tuple *new_tuple =
        static_cast<Tuple *>(copy_object_bytes(&destination, tuple.extract()));
    HeapPtrArrayBacking *new_pointers = static_cast<HeapPtrArrayBacking *>(
        copy_object_bytes(&destination, pointers.get()));
    repair_copies(thread, {{child.extract(), new_child},
                           {tuple.extract(), new_tuple},
                           {pointers.get(), new_pointers}});

    EXPECT_EQ(Value::from_oop(new_child), new_tuple->item_unchecked(0));
    EXPECT_EQ(Value::from_oop(new_child), new_tuple->item_unchecked(1));
    for(size_t idx = 2; idx < values.size(); ++idx)
    {
        EXPECT_EQ(values[idx], new_tuple->item_unchecked(idx));
    }
    EXPECT_EQ(new_child, new_pointers->elements[0]);
    EXPECT_EQ(new_child, new_pointers->elements[1]);
    EXPECT_EQ(nullptr, new_pointers->elements[2]);
    EXPECT_EQ(tuple.extract()->get_shape(), new_tuple->get_shape());
    EXPECT_EQ(child.extract()->get_shape(), new_child->get_shape());
    EXPECT_EQ(4.5, new_child->value());
    EXPECT_EQ(child_refs, child.extract()->refcount());
}

TEST(HeapObjectCopy, RepairsInlineSelfReferenceWithoutScanningLazyTail)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    TValue<String> name =
        context.vm().get_or_create_interned_string_value(L"SelfCopy");
    TValue<String> attr =
        context.vm().get_or_create_interned_string_value(L"self");
    ClassObject *cls = thread->make_internal_raw<ClassObject>(
        name, 7, context.vm().object_class(), NativeLayoutId::Instance);
    OwnedHeapPtr<Instance> source(thread->make_internal_raw<Instance>(cls));
    ASSERT_TRUE(
        source->set_own_property(attr, Value::from_oop(source.get())).value());
    ASSERT_EQ(1u, source->native_layout_aux_count_value());

    HeapSpace destination;
    Instance *copy =
        static_cast<Instance *>(copy_object_bytes(&destination, source.get()));
    repair_copies(thread, {{source.get(), copy}});
    EXPECT_EQ(Value::from_oop(copy), copy->get_own_property(attr));
    EXPECT_EQ(source->get_shape(), copy->get_shape());
    EXPECT_EQ(1u, copy->native_layout_aux_count_value());

    // The copied graph is raw storage. Break the original RC cycle using a
    // normal store; never release members of the byte copy.
    ASSERT_TRUE(source->set_own_property(attr, Value::None()).value());
}

TEST(HeapObjectCopy, RepairsCycleBetweenTwoCopiedInstances)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    TValue<String> name =
        context.vm().get_or_create_interned_string_value(L"CycleCopy");
    TValue<String> attr =
        context.vm().get_or_create_interned_string_value(L"next");
    ClassObject *cls = thread->make_internal_raw<ClassObject>(
        name, 1, context.vm().object_class(), NativeLayoutId::Instance);
    OwnedHeapPtr<Instance> first(thread->make_internal_raw<Instance>(cls));
    OwnedHeapPtr<Instance> second(thread->make_internal_raw<Instance>(cls));
    ASSERT_TRUE(
        first->set_own_property(attr, Value::from_oop(second.get())).value());
    ASSERT_TRUE(
        second->set_own_property(attr, Value::from_oop(first.get())).value());

    HeapSpace destination;
    Instance *new_first =
        static_cast<Instance *>(copy_object_bytes(&destination, first.get()));
    Instance *new_second =
        static_cast<Instance *>(copy_object_bytes(&destination, second.get()));
    repair_copies(thread,
                  {{first.get(), new_first}, {second.get(), new_second}});
    EXPECT_EQ(Value::from_oop(new_second), new_first->get_own_property(attr));
    EXPECT_EQ(Value::from_oop(new_first), new_second->get_own_property(attr));
    EXPECT_EQ(first->get_shape(), new_first->get_shape());
    EXPECT_EQ(second->get_shape(), new_second->get_shape());

    ASSERT_TRUE(first->set_own_property(attr, Value::None()).value());
    ASSERT_TRUE(second->set_own_property(attr, Value::None()).value());
}

TEST(HeapObjectCopy, RepairsOwnerBackingAndSharedChildLinks)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope activation(thread);
    TValue<String> name =
        context.vm().get_or_create_interned_string_value(L"OverflowCopy");
    TValue<String> self =
        context.vm().get_or_create_interned_string_value(L"self");
    TValue<String> a = context.vm().get_or_create_interned_string_value(L"a");
    TValue<String> b = context.vm().get_or_create_interned_string_value(L"b");
    ClassObject *cls = thread->make_internal_raw<ClassObject>(
        name, 0, context.vm().object_class(), NativeLayoutId::Instance);
    OwnedHeapPtr<Instance> owner(thread->make_internal_raw<Instance>(cls));
    Owned<TValue<Float>> child(thread->make_object_value<Float>(6.5));
    ASSERT_TRUE(owner->set_own_property(a, child.raw_value()).value());
    ASSERT_TRUE(owner->set_own_property(b, child.raw_value()).value());
    ASSERT_TRUE(
        owner->set_own_property(self, Value::from_oop(owner.get())).value());
    OverflowSlots *backing = owner->overflow_storage.get();
    ASSERT_NE(nullptr, backing);
    int32_t child_refs = child.extract()->refcount();

    HeapSpace destination;
    Instance *new_owner =
        static_cast<Instance *>(copy_object_bytes(&destination, owner.get()));
    OverflowSlots *new_backing =
        static_cast<OverflowSlots *>(copy_object_bytes(&destination, backing));
    Float *new_child =
        static_cast<Float *>(copy_object_bytes(&destination, child.extract()));
    // Visit the backing first: repair uses the complete mapping, not discovery
    // order or mutation of source headers.
    repair_copies(thread, {{backing, new_backing},
                           {owner.get(), new_owner},
                           {child.extract(), new_child}});
    EXPECT_EQ(new_backing, new_owner->overflow_storage.get());
    EXPECT_EQ(Value::from_oop(new_child), new_owner->get_own_property(a));
    EXPECT_EQ(Value::from_oop(new_child), new_owner->get_own_property(b));
    EXPECT_EQ(Value::from_oop(new_owner), new_owner->get_own_property(self));
    EXPECT_EQ(owner->get_shape(), new_owner->get_shape());
    EXPECT_EQ(6.5, new_child->value());
    EXPECT_EQ(child_refs, child.extract()->refcount());

    ASSERT_TRUE(owner->set_own_property(self, Value::None()).value());
}
