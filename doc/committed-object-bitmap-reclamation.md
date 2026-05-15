# Valid-Object Bitmap Reclamation

This document records the slab bitmap design for young-object discovery and slab
release accounting. It complements the broader reclamation docs rather than
replacing them. The implementation now uses per-slab valid-object bitmaps,
thread-local epoch slab lists, slab pins, and heap-owned slab release.

## Core Model

Use a valid-object bitmap per slab as the authoritative set of constructed heap
object headers in that slab.

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

`SlabAllocator` is passive allocator metadata. It does not remember which
`GlobalHeap` owns it. `GlobalHeap` owns the slab list, slab lookup table, and
all release/unmap decisions.

The ordinary baseline metadata shape is:

```cpp
static constexpr size_t ValidObjectBitmapGranule = 32;
static constexpr size_t ValidObjectBitmapBits =
    DefaultSlabSize / ValidObjectBitmapGranule;
static constexpr size_t ValidObjectBitmapWords =
    (ValidObjectBitmapBits + 63) / 64;

class SlabAllocator {
    char *start_ptr;
    char *curr_ptr;
    char *end_ptr;
    char *first_object_header;

    std::array<uint64_t, ValidObjectBitmapWords> valid_object_bitmap = {};
    uint32_t n_slab_pins = 0;
};
```

Keep the bitmap as the source of truth for valid object presence. A default slab
has only `ValidObjectBitmapWords` bitmap words, and release
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
bit_index < ValidObjectBitmapBits
```

For ordinary refcounted slabs, `first_object_header` is the first possible
object header address for that heap's pointer tag offset. For dedicated slabs,
`first_object_header` is the dedicated object's header address, and the
dedicated allocation path should only ever set bit 0.

## Slab Pins

The bitmap replaces per-object slab reclaim blockers. It does not replace slab
lifetime pins.

Use one underlying slab pin count, with named methods for the ownership role at
the call site:

```cpp
slab->add_active_allocator_pin();
slab->drop_active_allocator_pin();

slab->add_epoch_discovery_pin();
slab->drop_epoch_discovery_pin();
```

The two method categories update the same `n_slab_pins` counter. They exist so
call sites say why the slab is being kept alive:

- an active allocator pin is held while a `ThreadLocalHeap` may allocate from
  that slab;
- an epoch discovery pin is held while a thread-local reclamation epoch list
  needs to scan that slab's valid-object bitmap.

A slab is releasable when:

```text
valid_object_bitmap is empty
&& n_slab_pins == 0
```

Pin drops never release or unmap a slab as a side effect. `SlabAllocator` owns
local pin accounting; `GlobalHeap` owns registration, slab lookup removal, and
unmapping.

```cpp
slab->drop_epoch_discovery_pin();
global_heap.release_slab_if_empty(slab);
```

Callers are responsible for checking releasability at explicit batch points
after they are done touching the slab. `release_slab_if_empty` is owned by
`GlobalHeap`, because only `GlobalHeap` can unregister slab lookup entries and
erase/unmap slab storage.

Object reclamation does not release slabs eagerly. It clears valid-object bits
and batches slab release checks until reclamation teardown is complete.

## Allocation

Reservation remains bump-pointer allocation.

On successful construction/commit:

```cpp
slab->mark_valid_object(obj);
```

Construction failure does not set a valid-object bit, so there is no bitmap bit
to clear. Reserved bump memory remains an unmarked hole until whole-slab
reclamation recovers it.

Do not update the young-object slab list on every allocation. The allocation
fast path should not do per-object list bookkeeping.

## Active-Slab Epoch Lists

`ThreadLocalHeap` owns the list of slabs to scan for young zero-refcount
objects:

```cpp
std::vector<SlabAllocator *> epoch_slabs_since_reclamation;
uint32_t ordinary_inactive_slabs_since_reclamation = 0;
size_t dedicated_large_bytes_since_reclamation = 0;
```

This is thread-local allocator epoch state, not VM-global state.

Presence in an epoch list is distinct from being installed as an active
allocation slab. Each role owns its own pin. A slab can be active only,
epoch-listed only, both, or neither. In practice, newly installed ordinary active
slabs should also be remembered for the current epoch so allocations made from
them can be discovered later.

The epoch slab list is updated when the thread-local heap installs an active
ordinary slab:

```cpp
remember_epoch_slab(new_slab);
```

Each call appends exactly one epoch-list membership and adds exactly one epoch
discovery pin. It happens on construction, `switch_to_new_slabs()`, ordinary
slab exhaustion, and future size-class active slab switches. These paths are
responsible for not adding the same slab twice in one epoch. Remembering does
not happen on every allocation.

When an ordinary active slab is switched out, increment
`ordinary_inactive_slabs_since_reclamation`. This counter is seeded to zero
after reclamation and is the first ordinary-slab policy trigger: it counts how
many ordinary allocation slabs have gone inactive during the current epoch.
Switching out also drops the old slab's active allocator pin. The caller then
does not need to run a release check: ordinary active slabs are already
epoch-listed, and that epoch discovery pin keeps the slab alive until
reclamation scans the list and performs batched release checks.

Dedicated large-object slabs do not count as ordinary inactive slabs. They claim
a slab on their own, are never installed as ordinary active allocation slabs, and
would distort the ordinary slab switch policy. Track them through the shared
epoch slab list and the dedicated byte counter. The epoch entry owns the epoch
discovery pin whether construction later succeeds or fails; failed construction
leaves no valid-object bit, so the normal epoch finish can drop the discovery
pin and ask `GlobalHeap` whether the slab is empty.

After each reclamation, reset the list to the slabs that are active right now:

```cpp
drop epoch discovery pins for scanned epoch slabs
release empty scanned epoch slabs
epoch_slabs_since_reclamation = current_active_slabs();
add epoch discovery pins for those current active slabs
ordinary_inactive_slabs_since_reclamation = 0;
dedicated_large_bytes_since_reclamation = 0;
```

This is required because an active slab may receive more allocations before the
next slab switch. Today there is one ordinary active slab. With size classes,
`current_active_slabs()` becomes one active slab per size class.

One epoch-list membership owns one epoch discovery pin. The allocation/switch
paths should make duplicate membership impossible by design; debug assertions
can check that invariant.

## Candidate Discovery

Reclamation has two candidate sources:

- **ZCT entries**: older zero-refcount objects and objects that reached zero
  through heap `DECREF`.
- **Epoch slabs**: ordinary and dedicated young objects that may never have
  entered the ZCT because their only references were managed stack values.

VM-global reclamation iterates registered `ThreadState`s. For each thread, it
processes that thread's ZCT and scans the valid-object bitmap for the slabs in
that thread's `ThreadLocalHeap::epoch_slabs_since_reclamation`.

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
native descriptor teardown, and slab release batching.

## Root Filtering

Stack roots remain a conservative identity filter.

Root collection inserts refcounted-pointer-shaped values into the root set. It
must not dereference pointer-shaped stack values and must not consult allocator
metadata to validate them. The root set is then used to filter both ZCT entries
and bitmap-discovered young candidates.

## Teardown

Native-layout descriptors release owned values and run any native teardown. They
are not required for slab walking when the valid-object bitmap enumerates object
starts.

When reclaiming an object:

```text
look up the object's release descriptor by NativeLayoutId
if descriptor is StaticSpan/DynamicSmiSpan/DynamicAuxSpan:
    for each described owned Value cell:
        copy value
        clear cell to not_present
        release copied value through reclamation path
else if descriptor is CustomDealloc:
    call the custom deallocator with the reclaimed thread installed as active
clear valid-object bit
remember owning slab in reclaimed_slabs
Reclaiming -> Dead
```

Child releases that reach zero append to the ZCT currently being processed.

After all candidate processing is complete:

```cpp
for(SlabAllocator *slab: scanned_epoch_slabs)
    slab->drop_epoch_discovery_pin();

for(SlabAllocator *slab: touched_slabs)
    global_heap.release_slab_if_empty(slab);
```

The release-check set includes slabs whose valid-object bits were cleared and
epoch slabs whose discovery pins were dropped. Reclamation must not unmap slabs
while candidate sources are still being scanned.

Object extent is descriptor-driven through
`object_size_in_bytes(const HeapObject *)`, but bitmap discovery does not need
to query object extent. The bitmap enumerates candidate object starts directly.

## Policy

Reclamation request policy can be added after the mechanism is stable. Useful
triggers include:

- ZCT length
- ordinary inactive slab count since the previous reclamation
- dedicated large-object bytes since the previous reclamation
- total committed slab bytes

Every-safepoint reclamation remains a testing mode, not a production policy.

Policy tests should prove that each trigger requests safepoint reclamation
without reclaiming at arbitrary program points, multiple pending requests
coalesce into one safepoint, repeated allocate/drop/safepoint cycles remain
stable under the policy triggers, and debug counters stay internally consistent.
