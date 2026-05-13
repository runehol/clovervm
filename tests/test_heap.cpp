#include "heap.h"

#include <gtest/gtest.h>
#include <new>

using namespace cl;

TEST(GlobalHeap, SlabMapFindsAllocatedAddresses)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap local_heap(&heap);

    HeapAllocation allocation = local_heap.allocate(sizeof(HeapObject));

    EXPECT_EQ(allocation.slab,
              heap.slab_for_address_unlocked(allocation.memory));
}

TEST(GlobalHeap, ThreadLocalHeapPinsActiveAllocatorSlab)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();

    {
        ThreadLocalHeap local_heap(&heap);
        HeapAllocation allocation = local_heap.allocate(sizeof(HeapObject));
        SlabAllocator *slab = heap.slab_for_address_unlocked(allocation.memory);

        EXPECT_EQ(1u, slab->reclaim_blocker_count());
    }
}

TEST(GlobalHeap, CommittedOrdinaryObjectAddsReclaimBlocker)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap local_heap(&heap);

    HeapAllocation allocation = local_heap.allocate(sizeof(HeapObject));
    ASSERT_EQ(1u, allocation.slab->reclaim_blocker_count());

    HeapObject *obj = new(allocation.memory) HeapObject(0);
    commit_heap_allocation(allocation, obj);

    EXPECT_EQ(2u, allocation.slab->reclaim_blocker_count());
}

TEST(GlobalHeap, OpeningNewOrdinarySlabDropsPreviousAllocatorBlocker)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap(SlabLookupGranuleSize);
    ThreadLocalHeap local_heap(&heap);

    HeapAllocation first = local_heap.allocate(SlabLookupGranuleSize / 2);
    ASSERT_EQ(1u, first.slab->reclaim_blocker_count());

    HeapAllocation second = local_heap.allocate(SlabLookupGranuleSize / 2);

    EXPECT_NE(first.slab, second.slab);
    EXPECT_EQ(1u, second.slab->reclaim_blocker_count());
}

TEST(GlobalHeap, DedicatedLargeAllocationHasNoAllocatorBlocker)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap local_heap(&heap);

    HeapAllocation allocation = local_heap.allocate(LargeAllocationSize);
    ASSERT_EQ(0u, allocation.slab->reclaim_blocker_count());

    HeapObject *obj = new(allocation.memory) HeapObject(0);
    commit_heap_allocation(allocation, obj);

    EXPECT_EQ(1u, allocation.slab->reclaim_blocker_count());
}

TEST(GlobalHeap, DedicatedLargeAllocationAbortHasNoBlockerCleanup)
{
    GlobalHeap heap = GlobalHeap::refcounted_heap();
    ThreadLocalHeap local_heap(&heap);

    HeapAllocation allocation = local_heap.allocate(LargeAllocationSize);

    EXPECT_EQ(0u, allocation.slab->reclaim_blocker_count());
}

TEST(GlobalHeap, InternedHeapTracksReclaimBlockers)
{
    GlobalHeap heap = GlobalHeap::interned_heap();
    ThreadLocalHeap local_heap(&heap);

    HeapAllocation allocation = local_heap.allocate(sizeof(HeapObject));
    ASSERT_EQ(1u, allocation.slab->reclaim_blocker_count());

    commit_heap_allocation(allocation, new(allocation.memory) HeapObject(0));

    EXPECT_EQ(2u, allocation.slab->reclaim_blocker_count());
}
