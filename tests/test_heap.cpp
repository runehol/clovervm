#include "refcount.h"
#include "test_helpers.h"
#include "thread_local_heap.h"
#include "tuple.h"

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
