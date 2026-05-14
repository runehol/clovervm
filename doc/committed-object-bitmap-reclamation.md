# Committed-Object Bitmap Reclamation

This document records the proposed slab bitmap design for young-object discovery
and slab release accounting. It complements the broader reclamation docs rather
than replacing them. The current implementation still uses eager
allocation-time ZCT enqueue and slab reclaim blockers, but the direction below
is the next target design.

## Core Model

Use a committed-object header bitmap per slab as the authoritative set of
constructed heap objects in that slab.

One bit represents one possible object header slot. For the ordinary 64 KiB slab
and 32-byte heap pointer granularity:

```text
65536 bytes / 32 bytes = 2048 bits = 256 bytes = 32 uint64_t words
```

Failed construction never sets the bit, so abandoned bump memory is naturally
skipped by slab walks.

Dedicated large-object slabs can use the same bitmap API. They have one valid
object header slot, so bit 0 is the only meaningful bit. The bitmap does not
scale with dedicated slab byte size.

## Slab Metadata

`SlabAllocator` should become passive allocator metadata. It should not remember
which `GlobalHeap` owns it. `GlobalHeap` owns the slab list, slab lookup table,
and all release/unmap decisions.

The ordinary baseline metadata shape is:

```cpp
static constexpr size_t CommittedHeaderBitmapGranule = 32;
static constexpr size_t CommittedHeaderBitmapBits =
    DefaultSlabSize / CommittedHeaderBitmapGranule;
static constexpr size_t CommittedHeaderBitmapWords =
    (CommittedHeaderBitmapBits + 63) / 64;

class SlabAllocator {
    char *start_ptr;
    char *curr_ptr;
    char *end_ptr;
    char *first_object_header;

    std::array<uint64_t, CommittedHeaderBitmapWords>
        committed_header_bitmap = {};
    uint32_t active_allocator_pins = 0;
};
```

Keep the bitmap as the initial source of truth for committed-object presence.
A default slab has only `CommittedHeaderBitmapWords` bitmap words, and release
checks are batched, so additional summary state should wait for measurement.

## Bitmap Indexing

Use allocation-lattice-relative indexing:

```cpp
bit_index = (object_header - slab->first_object_header) >> 5;
```

with debug checks:

```cpp
object_header >= slab->first_object_header
(object_header - slab->first_object_header) % 32 == 0
bit_index < CommittedHeaderBitmapBits
```

For ordinary refcounted slabs, `first_object_header` is the first possible
object header address for that heap's pointer tag offset. For dedicated slabs,
`first_object_header` is the dedicated object's header address, and the
dedicated allocation path should only ever set bit 0.

## Allocator Pins

The bitmap replaces per-object slab reclaim blockers. It does not replace active
allocator pins.

A slab is releasable when:

```text
committed_header_bitmap is empty
&& active_allocator_pins == 0
```

`ThreadLocalHeap` pins the slabs it currently has open for allocation. Dropping
an allocator pin is heap-mediated:

```cpp
global_heap.drop_allocator_pin(slab);
```

That operation drops the pin and calls `release_slab_if_empty(slab)`.
`release_slab_if_empty` is owned by `GlobalHeap`, because only `GlobalHeap` can
unregister slab lookup entries and erase/unmap slab storage.

Object reclamation does not release slabs eagerly. It clears committed-object
bits and batches slab release checks until reclamation teardown is complete.

## Allocation

Reservation remains bump-pointer allocation.

On successful construction/commit:

```cpp
slab->mark_committed_object(obj);
```

Construction failure does not set a committed-object bit. There is no object
bitmap bit to clear and no object reclaim blocker to drop.

Do not update the young-object slab list on every allocation. The allocation
fast path should not do per-object list bookkeeping.

## Active-Slab Epoch Lists

`ThreadLocalHeap` owns the list of slabs that should be scanned for young
zero-refcount objects:

```cpp
std::vector<SlabAllocator *> slabs_active_since_reclamation;
```

This is thread-local allocator epoch state, not VM-global state.

The list is updated when the thread-local heap installs an active slab:

```cpp
remember_active_slab_since_reclamation(new_slab);
```

That happens on construction, `switch_to_new_slabs()`, ordinary slab exhaustion,
and future size-class active slab switches. It does not happen on every
allocation.

After each reclamation, reset the list to the slabs that are active right now:

```cpp
slabs_active_since_reclamation = current_active_slabs();
```

This is required because an active slab may receive more allocations before the
next slab switch. Today there is one ordinary active slab. With size classes,
`current_active_slabs()` becomes one active slab per size class.

Use simple uniqueness for the first implementation, such as a small vector with
a linear duplicate check. This bookkeeping is off the per-allocation fast path.

## Candidate Discovery

Reclamation has two candidate sources:

- **ZCT entries**: older zero-refcount objects and objects that reached zero
  through heap `DECREF`.
- **Active-since-reclamation slabs**: young objects that may never have entered
  the ZCT because their only references were managed stack values.

VM-global reclamation iterates registered `ThreadState`s. For each thread, it
processes that thread's ZCT and scans the committed-object bitmap for the slabs
in that thread's `ThreadLocalHeap::slabs_active_since_reclamation`.

For each bitmap-discovered young object:

```text
if refcount > 0:
    ignore
if lifecycle_state != Normal:
    ignore; the ZCT/lifecycle path already owns it
if root_set contains object identity:
    Normal -> InZct
    append to a ZCT
else:
    Normal -> Reclaiming
    reclaim through the normal teardown path
```

If a young zero-refcount object is stack-rooted, it must be moved into a ZCT.
When that stack root disappears later, no heap `DECREF` may occur to rediscover
the object.

ZCT processing and young-slab discovery are candidate producers for one
reclamation mechanism. They must share lifecycle transitions, root filtering,
owned-value scanning, and slab release batching.

## Root Filtering

Stack roots remain a conservative identity filter.

Root collection inserts refcounted-pointer-shaped values into the root set. It
must not dereference pointer-shaped stack values and must not consult allocator
metadata to validate them. The root set is then used to filter both ZCT entries
and bitmap-discovered young candidates.

## Teardown

Native-layout descriptors are currently needed for owned-value scanning and
native teardown. They are not required for slab walking when the committed-header
bitmap enumerates object starts.

When reclaiming an object:

```text
derive owned-value scan recipe from native layout / current HeapLayout bridge
for each owned Value slot:
    copy value
    clear slot to not_present
    release copied value through reclamation path
clear committed-object bit
remember owning slab in reclaimed_slabs
Reclaiming -> Dead
```

Child releases that reach zero append to the ZCT currently being processed.

After all candidate processing is complete:

```cpp
for(SlabAllocator *slab: reclaimed_slabs)
    global_heap.release_slab_if_empty(slab);
```

## Descriptor Direction

The immediate descriptor facade should focus on:

- owned `Value` scanning
- optional native teardown hooks
- future C-extension teardown boundaries

Object extent can still be a future descriptor concern. It may be useful for
debug validation, allocation accounting, size-class policies, or non-bitmap
iteration strategies. It is not required for young-object slab discovery in the
bitmap design.

## Policy

Reclamation request policy can be added after the mechanism is stable. Useful
triggers include:

- ZCT length
- bytes allocated since the previous reclamation
- number of slabs active since the previous reclamation
- total committed slab bytes

Every-safepoint reclamation remains a testing mode, not a production policy.
