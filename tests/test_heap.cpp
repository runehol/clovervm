#include "heap_object_scan.h"
#include "thread_local_heap.h"

#include <gtest/gtest.h>
#include <new>
#include <stdexcept>

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

    class ScanObject : public HeapObject
    {
    public:
        CL_DECLARE_STATIC_LAYOUT_WITH_VALUES(ScanObject, values, 2);

        ScanObject() : HeapObject(compact_layout()) {}

        Value values[2];
    };
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

    {
        ThreadLocalHeap local_heap(&heap);
        char *memory = local_heap.allocate(sizeof(HeapObject));
        SlabAllocator *slab = heap.slab_for_address_unlocked(memory);

        EXPECT_EQ(2u, slab->reclaim_blocker_count());
    }
}

TEST(GlobalHeap, OrdinaryAllocationAddsObjectBlocker)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap local_heap(&heap);

    char *memory = local_heap.allocate(sizeof(HeapObject));
    SlabAllocator *slab = heap.slab_for_address_unlocked(memory);

    EXPECT_EQ(2u, slab->reclaim_blocker_count());
}

TEST(GlobalHeap, OpeningNewOrdinarySlabDropsPreviousAllocatorBlocker)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap(SlabLookupGranuleSize);
    ThreadLocalHeap local_heap(&heap);

    char *first = local_heap.allocate(SlabLookupGranuleSize / 2);
    SlabAllocator *first_slab = heap.slab_for_address_unlocked(first);
    ASSERT_EQ(2u, first_slab->reclaim_blocker_count());

    char *second = local_heap.allocate(SlabLookupGranuleSize / 2);
    SlabAllocator *second_slab = heap.slab_for_address_unlocked(second);

    EXPECT_NE(first_slab, second_slab);
    EXPECT_EQ(1u, first_slab->reclaim_blocker_count());
    EXPECT_EQ(2u, second_slab->reclaim_blocker_count());
}

TEST(GlobalHeap, DedicatedLargeAllocationAddsObjectBlocker)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap local_heap(&heap);

    char *memory = local_heap.allocate(LargeAllocationSize);
    SlabAllocator *slab = heap.slab_for_address_unlocked(memory);

    EXPECT_EQ(1u, slab->reclaim_blocker_count());
}

TEST(GlobalHeap, DedicatedLargeAllocationConstructionFailureDropsObjectBlocker)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap local_heap(&heap);

    char *memory = local_heap.allocate(LargeAllocationSize);
    local_heap.drop_reclaim_blocker_for_failed_construction(memory);

    SUCCEED();
}

TEST(GlobalHeap, FailedConstructionDropsObjectBlocker)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap local_heap(&heap);

    EXPECT_THROW(local_heap.make<ThrowingHeapObject>(), std::runtime_error);

    char *memory = local_heap.allocate(sizeof(HeapObject));
    SlabAllocator *slab = heap.slab_for_address_unlocked(memory);
    EXPECT_EQ(2u, slab->reclaim_blocker_count());
}

TEST(GlobalHeap, InternedHeapTracksReclaimBlockers)
{
    GlobalHeap heap = GlobalHeap::interned_heap();
    ThreadLocalHeap local_heap(&heap);

    char *memory = local_heap.allocate(sizeof(HeapObject));
    SlabAllocator *slab = heap.slab_for_address_unlocked(memory);

    EXPECT_EQ(2u, slab->reclaim_blocker_count());
}

TEST(HeapObjectScan, CompactLayoutWithNoValues)
{
    HeapObject obj(ThrowingHeapObject::compact_layout());

    HeapScanDescriptor descriptor = heap_scan_descriptor_for_object(&obj);

    EXPECT_EQ(0u, descriptor.first_value_offset_in_words);
    EXPECT_EQ(0u, descriptor.value_count);
}

TEST(HeapObjectScan, CompactLayoutWithValueSpan)
{
    static_assert(!ScanObject::has_dynamic_layout);
    ScanObject obj;

    HeapScanDescriptor descriptor = heap_scan_descriptor_for_object(&obj);

    EXPECT_EQ(ScanObject::static_value_offset_in_words(),
              descriptor.first_value_offset_in_words);
    EXPECT_EQ(2u, descriptor.value_count);
    EXPECT_EQ(&obj.values[0], heap_first_value_slot(&obj, descriptor));
}

TEST(HeapObjectScan, ExpandedLayoutWithValueSpan)
{
    alignas(ExpandedHeader) char
        storage[sizeof(ExpandedHeader) + sizeof(HeapObject)];
    ExpandedHeader *header = reinterpret_cast<ExpandedHeader *>(storage);
    header->object_size_in_16byte_units = 1;
    header->value_count = 7;

    HeapObject *obj = new(storage + sizeof(ExpandedHeader))
        HeapObject(encode_expanded_layout_unchecked(3));

    HeapScanDescriptor descriptor = heap_scan_descriptor_for_object(obj);

    EXPECT_EQ(3u, descriptor.first_value_offset_in_words);
    EXPECT_EQ(7u, descriptor.value_count);
}
