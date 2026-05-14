#include "refcount.h"
#include "test_helpers.h"
#include "thread_local_heap.h"
#include "tuple.h"

#include <gtest/gtest.h>
#include <new>
#include <stdexcept>
#include <vector>

using namespace cl;

namespace
{
    class ThrowingHeapObject : public HeapObject
    {
    public:
        CL_DECLARE_STATIC_LAYOUT_NO_VALUES(ThrowingHeapObject);

        ThrowingHeapObject() : HeapObject(compact_layout())
        {
            throw std::runtime_error("construction failed");
        }
    };

    class SimpleHeapObject : public HeapObject
    {
    public:
        CL_DECLARE_STATIC_LAYOUT_NO_VALUES(SimpleHeapObject);

        SimpleHeapObject() : HeapObject(compact_layout()) {}
    };

    class LargeSimpleHeapObject : public HeapObject
    {
    public:
        CL_DECLARE_STATIC_LAYOUT_NO_VALUES(LargeSimpleHeapObject);

        LargeSimpleHeapObject() : HeapObject(compact_layout()) {}

    private:
        [[maybe_unused]] char payload[LargeAllocationSize] = {};
    };

    std::vector<HeapObject *> valid_objects_in(SlabAllocator *slab)
    {
        std::vector<HeapObject *> objects;
        slab->for_each_valid_object(
            [&objects](HeapObject *obj) { objects.push_back(obj); });
        return objects;
    }

}  // namespace

TEST(GlobalHeap, SlabMapFindsAllocatedAddresses)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap local_heap(&heap);

    char *memory = local_heap.allocate(sizeof(HeapObject));

    EXPECT_NE(nullptr, heap.slab_for_address_unlocked(memory));
}

TEST(GlobalHeap, ThreadLocalHeapPinsActiveAllocatorSlab)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    char *memory;

    {
        ThreadLocalHeap local_heap(&heap);
        memory = local_heap.allocate(sizeof(HeapObject));
        SlabAllocator *slab = heap.slab_for_address_unlocked(memory);

        EXPECT_EQ(2u, slab->slab_pin_count());
        EXPECT_EQ(1u, local_heap.epoch_slab_count());
        EXPECT_EQ(0u,
                  local_heap.ordinary_inactive_slabs_since_reclamation_count());
    }

    EXPECT_FALSE(heap.has_slab_for_address_for_testing(memory));
}

TEST(GlobalHeap, OrdinaryRawAllocationDoesNotPinObject)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap local_heap(&heap);

    char *memory = local_heap.allocate(sizeof(HeapObject));
    SlabAllocator *slab = heap.slab_for_address_unlocked(memory);

    EXPECT_EQ(2u, slab->slab_pin_count());
    EXPECT_EQ(0u, slab->count_valid_objects_slow());
}

TEST(GlobalHeap, OpeningNewOrdinarySlabDropsPreviousActivePin)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap(SlabLookupGranuleSize);
    ThreadLocalHeap local_heap(&heap);

    char *first = local_heap.allocate(SlabLookupGranuleSize / 2);
    SlabAllocator *first_slab = heap.slab_for_address_unlocked(first);
    ASSERT_EQ(2u, first_slab->slab_pin_count());

    char *second = local_heap.allocate(SlabLookupGranuleSize / 2);
    SlabAllocator *second_slab = heap.slab_for_address_unlocked(second);

    EXPECT_NE(first_slab, second_slab);
    EXPECT_TRUE(heap.has_slab_for_address_for_testing(first));
    EXPECT_EQ(1u, first_slab->slab_pin_count());
    EXPECT_EQ(2u, second_slab->slab_pin_count());
    EXPECT_EQ(2u, local_heap.epoch_slab_count());
    EXPECT_EQ(1u, local_heap.ordinary_inactive_slabs_since_reclamation_count());
}

TEST(GlobalHeap, SwitchingThreadLocalHeapToNewSlabsDropsPreviousActivePin)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap local_heap(&heap);
    char *first = local_heap.allocate(sizeof(HeapObject));
    SlabAllocator *first_slab = heap.slab_for_address_unlocked(first);
    ASSERT_EQ(2u, first_slab->slab_pin_count());

    local_heap.switch_to_new_slabs();
    char *second = local_heap.allocate(sizeof(HeapObject));
    SlabAllocator *second_slab = heap.slab_for_address_unlocked(second);

    EXPECT_NE(first_slab, second_slab);
    EXPECT_TRUE(heap.has_slab_for_address_for_testing(first));
    EXPECT_EQ(1u, first_slab->slab_pin_count());
    EXPECT_EQ(2u, second_slab->slab_pin_count());
    EXPECT_EQ(2u, local_heap.epoch_slab_count());
    EXPECT_EQ(1u, local_heap.ordinary_inactive_slabs_since_reclamation_count());
}

TEST(GlobalHeap, VmBootstrapSwitchesDefaultThreadToFreshSlab)
{
    test::VmTestContext context;
    ThreadState *thread = context.thread();
    ThreadState::ActivationScope active_thread(thread);
    SimpleHeapObject *object = thread->make_internal_raw<SimpleHeapObject>();
    SlabAllocator *slab =
        context.vm().get_refcounted_global_heap().slab_for_object_unlocked(
            object);

    EXPECT_EQ(2u, slab->slab_pin_count());
    EXPECT_EQ(1u, slab->count_valid_objects_slow());
}

TEST(GlobalHeap, DedicatedLargeRawAllocationHasEpochPin)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap local_heap(&heap);

    char *memory = local_heap.allocate(LargeAllocationSize);
    SlabAllocator *slab = heap.slab_for_address_unlocked(memory);

    EXPECT_EQ(1u, slab->slab_pin_count());
    EXPECT_EQ(0u, slab->count_valid_objects_slow());
    EXPECT_EQ(2u, local_heap.epoch_slab_count());
    EXPECT_EQ(LargeAllocationSize,
              local_heap.dedicated_large_bytes_since_reclamation_count());
    local_heap.release_for_failed_construction(memory);
    EXPECT_EQ(1u, local_heap.epoch_slab_count());
    EXPECT_EQ(LargeAllocationSize,
              local_heap.dedicated_large_bytes_since_reclamation_count());
}

TEST(GlobalHeap, DedicatedLargeAllocationConstructionFailureReleasesEmptySlab)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap local_heap(&heap);

    char *memory = local_heap.allocate(LargeAllocationSize);
    local_heap.release_for_failed_construction(memory);

    EXPECT_FALSE(heap.has_slab_for_address_for_testing(memory));
}

TEST(GlobalHeap, ThreadLocalHeapAdoptsEpochStateByMove)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap parent(&heap);
    char *first;
    char *second;
    char *large;
    SlabAllocator *first_slab;
    SlabAllocator *second_slab;
    SlabAllocator *large_slab;

    {
        ThreadLocalHeap child(&heap);
        first = child.allocate(sizeof(HeapObject));
        first_slab = heap.slab_for_address_unlocked(first);
        child.switch_to_new_slabs();
        second = child.allocate(sizeof(HeapObject));
        second_slab = heap.slab_for_address_unlocked(second);
        large = child.allocate(LargeAllocationSize);
        large_slab = heap.slab_for_address_unlocked(large);

        ASSERT_EQ(3u, child.epoch_slab_count());
        ASSERT_EQ(1u, child.ordinary_inactive_slabs_since_reclamation_count());
        ASSERT_EQ(LargeAllocationSize,
                  child.dedicated_large_bytes_since_reclamation_count());

        parent.adopt_epoch_state_from(child);

        EXPECT_EQ(4u, parent.epoch_slab_count());
        EXPECT_EQ(1u, parent.ordinary_inactive_slabs_since_reclamation_count());
        EXPECT_EQ(LargeAllocationSize,
                  parent.dedicated_large_bytes_since_reclamation_count());
        EXPECT_EQ(0u, child.epoch_slab_count());
        EXPECT_EQ(0u, child.ordinary_inactive_slabs_since_reclamation_count());
        EXPECT_EQ(0u, child.dedicated_large_bytes_since_reclamation_count());
    }

    EXPECT_TRUE(heap.has_slab_for_address_for_testing(first));
    EXPECT_TRUE(heap.has_slab_for_address_for_testing(second));
    EXPECT_TRUE(heap.has_slab_for_address_for_testing(large));
    EXPECT_EQ(1u, first_slab->slab_pin_count());
    EXPECT_EQ(1u, second_slab->slab_pin_count());
    EXPECT_EQ(1u, large_slab->slab_pin_count());
}

TEST(GlobalHeap, FailedOrdinaryConstructionLeavesActiveSlabUnmarked)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap local_heap(&heap);

    EXPECT_THROW(local_heap.make<ThrowingHeapObject>(), std::runtime_error);

    char *memory = local_heap.allocate(sizeof(HeapObject));
    SlabAllocator *slab = heap.slab_for_address_unlocked(memory);
    EXPECT_EQ(2u, slab->slab_pin_count());
    EXPECT_EQ(0u, slab->count_valid_objects_slow());
}

TEST(GlobalHeap, SuccessfulConstructionMarksValidObject)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap local_heap(&heap);

    SimpleHeapObject *object = local_heap.make<SimpleHeapObject>();
    SlabAllocator *slab = heap.slab_for_object_unlocked(object);

    std::vector<HeapObject *> objects = valid_objects_in(slab);
    ASSERT_EQ(1u, objects.size());
    EXPECT_EQ(object, objects[0]);
    EXPECT_EQ(1u, slab->count_valid_objects_slow());
}

TEST(GlobalHeap, FailedConstructionDoesNotMarkValidObject)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap local_heap(&heap);

    EXPECT_THROW(local_heap.make<ThrowingHeapObject>(), std::runtime_error);

    char *memory = local_heap.allocate(sizeof(HeapObject));
    SlabAllocator *slab = heap.slab_for_address_unlocked(memory);
    EXPECT_EQ(0u, slab->count_valid_objects_slow());
}

TEST(GlobalHeap, ValidObjectIterationSkipsAbandonedBumpGaps)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap local_heap(&heap);

    EXPECT_THROW(local_heap.make<ThrowingHeapObject>(), std::runtime_error);
    SimpleHeapObject *object = local_heap.make<SimpleHeapObject>();
    SlabAllocator *slab = heap.slab_for_object_unlocked(object);

    std::vector<HeapObject *> objects = valid_objects_in(slab);
    ASSERT_EQ(1u, objects.size());
    EXPECT_EQ(object, objects[0]);
}

TEST(GlobalHeap, DedicatedLargeObjectMarksOnlyBitZero)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap local_heap(&heap);

    LargeSimpleHeapObject *object = local_heap.make<LargeSimpleHeapObject>();
    SlabAllocator *slab = heap.slab_for_object_unlocked(object);

    std::vector<HeapObject *> objects = valid_objects_in(slab);
    ASSERT_EQ(1u, objects.size());
    EXPECT_EQ(object, objects[0]);
    EXPECT_EQ(slab->first_object_slot(), reinterpret_cast<char *>(object));
    EXPECT_EQ(1u, slab->slab_pin_count());
}

TEST(GlobalHeap, InternedHeapTracksSlabPins)
{
    GlobalHeap heap = GlobalHeap::interned_heap();
    ThreadLocalHeap local_heap(&heap);

    char *memory = local_heap.allocate(sizeof(HeapObject));
    SlabAllocator *slab = heap.slab_for_address_unlocked(memory);

    EXPECT_EQ(2u, slab->slab_pin_count());
}

TEST(GlobalHeap, ExpandedDynamicAllocationPreservesPointerTag)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap local_heap(&heap);

    Tuple *tuple =
        local_heap.make<Tuple>(BootstrapObjectTag{}, object_layout_count_mask);

    EXPECT_TRUE(layout_is_expanded(tuple->layout));
    EXPECT_TRUE(heap_ptr_is_refcounted(tuple));
    EXPECT_NE(nullptr, heap.slab_for_object_unlocked(tuple));
}

TEST(HeapLayout, CompactLayoutSupportsOffsetUpToFifteen)
{
    HeapLayout layout = encode_compact_layout_unchecked(1, 15, 1);

    EXPECT_FALSE(layout_is_expanded(layout));
    EXPECT_EQ(15u, compact_layout_value_offset_in_words(layout));
    EXPECT_EQ(1u, compact_layout_value_count(layout));
}

TEST(HeapLayout, CompactLayoutRejectsOffsetAboveFifteen)
{
    EXPECT_TRUE(compact_layout_fits(1, object_layout_offset_mask, 1));
    EXPECT_FALSE(compact_layout_fits(1, object_layout_offset_mask + 1, 1));
}

TEST(HeapLayout, CompactLayoutRejectsValueCountAboveLimit)
{
    EXPECT_TRUE(compact_layout_fits(1, 1, object_layout_count_mask));
    EXPECT_FALSE(compact_layout_fits(1, 1, object_layout_count_mask + 1));
}
