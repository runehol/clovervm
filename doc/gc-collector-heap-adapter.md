# Isolated Collector and Heap Adapter

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Partial; isolated mark and strong evacuation implemented; weak evacuation repair and production copying not implemented |
| Scope | One collector algorithm instantiated against a test heap and CloverVM |
| Owning layers | `src/gc/` owns traversal and the CloverVM adapter; memory layouts own slot enumeration; tests own the synthetic heap |
| Validated against | Strong evacuation working tree based on `9e0ab41f` (2026-10-31); verification recorded below |
| Supersedes | N/A |

This expands the collector boundary in the
[GC implementation plan](generational-copying-gc-implementation-plan.md).
The [main GC design](generational-copying-gc.md) still owns generation, movement,
pinning, and lifetime policy. This document does not change those decisions.

## One Algorithm, Two Representations

Template the collector over a concrete heap adapter, not over each Python
object layout. `TestHeapAdapter::Object` is a small test object;
`CloverHeapAdapter::Object` is `HeapObject`. Float, tuple, class, and other
CloverVM layouts continue to use the existing runtime layout dispatch.

The algorithm owns traversal, discovery/enqueue decisions, worklist draining,
and collection phases. The adapter exposes representation-specific primitives.
Neither adapter may hide graph traversal in a `trace_object()` or
`evacuate_reachable_graph()` operation.

No virtual heap hierarchy, policy matrix, configurable collector strategies, or
elaborate concept machinery is needed. The same template body must run in both
instantiations. Sketches below omit includes and ordinary boilerplate; the
source headers are authoritative for the implemented mark-phase API.
The first mark-phase interface below is now implemented in the working tree;
the later evacuation capabilities remain proposals requiring separate review.

## First Slice: Non-Moving Strong Closure

Start with the complete strong graph reachable from explicit roots, not a
nursery-only traversal. Old, stable, and immortal objects can have outgoing
strong references too. This first algorithm neither filters them by generation
nor assumes non-moving means untraced.

The initial adapter contract is:

```cpp
class HeapAdapter
{
public:
    using Object = /* representation-specific object type */;
    using Slot = /* borrowed initialized reference-capable cell */;

    bool try_mark(Object *object); // First mark in this collection returns true.
    bool is_marked(Object *object) const;

    template<typename Visitor>
    void visit_strong_slots(Object *object, Visitor &&visitor);
};

// Slot's existing/proposed common vocabulary:
// Object *load_pointer() const;
// void rewrite_pointer(Object *replacement) const; // Future movement only.
```

`try_mark()` is a primitive mark test-and-set, not a traversal or an allocation
of a managed object. Its representation belongs to the adapter. The first
implementation uses a fresh native side set per collection in both adapters;
it does not reuse RC/lifecycle flags or introduce GC header fields. A future
header-backed implementation would be a separate layout decision.

`is_marked()` provides a read-only result query; strong traversal itself needs
only `try_mark()`. Marks are not owning references and do not affect lifetime.

An algorithm sketch:

```cpp
template<typename Heap>
class StrongClosure
{
    using Object = typename Heap::Object;
    using Slot = typename Heap::Slot;

public:
    explicit StrongClosure(Heap &heap) : heap_(heap) {}

    void add_root(Slot slot) { discover(slot.load_pointer()); }

    void drain()
    {
        while(!pending_.empty())
        {
            Object *object = pending_.front();
            pending_.pop_front();
            heap_.visit_strong_slots(object, [&](Slot slot) {
                discover(slot.load_pointer());
            });
        }
    }

private:
    void discover(Object *object)
    {
        if(object != nullptr && heap_.try_mark(object))
        {
            pending_.push_back(object);
        }
    }

    Heap &heap_;
    std::deque<Object *> pending_;
};
```

Use a fresh adapter and closure instance, seed all roots, and drain once. This
is a single stop-the-world traversal, not an incremental collector interface.
Marking before enqueueing prevents cycles and duplicate roots from repeatedly
scheduling the same object. The deque holds only discovered, unscanned objects;
processed entries are removed instead of retaining the whole live set in a
vector. A wide graph can still require a large frontier. The temporary side
mark set separately retains all discovered addresses until collection ends.
The queue never retains slot handles. No object references are rewritten here.

Root discovery remains outside this template. A caller can feed a span of test
root cells or use an existing root visitor to call `add_root()`. The algorithm
does not decide which interpreter locals or temporaries are roots. Supplying
complete valid roots is an explicit precondition, not a claim that CloverVM's
paused frame-state design is solved.

## Test Heap Sketch

Use real, independently allocated test objects rather than treating vector
indices as addresses or requiring dense IDs. Test-only logical identity labels
let an independent oracle compare graphs after future relocation.

```cpp
struct TestObject;

struct TestCell
{
    std::variant<int64_t, TestObject *> contents;
};

struct TestObject
{
    uint64_t logical_identity;
    std::vector<TestCell> strong_cells;
    std::vector<TestObject *> weak_targets; // Excluded from strong enumeration.
};

class TestSlot
{
public:
    explicit TestSlot(TestCell &cell) : cell_(&cell) {}

    TestObject *load_pointer() const
    {
        auto pointer = std::get_if<TestObject *>(&cell_->contents);
        return pointer == nullptr ? nullptr : *pointer;
    }

    void rewrite_pointer(TestObject *replacement) const
    {
        assert(load_pointer() != nullptr && replacement != nullptr);
        cell_->contents = replacement;
    }

private:
    TestCell *cell_;
};

class TestHeap
{
    // Graph construction helpers own objects here; roots are separate TestCells.
    std::vector<std::unique_ptr<TestObject>> objects_;
};

class TestHeapAdapter
{
public:
    using Object = TestObject;
    using Slot = TestSlot;

    bool try_mark(Object *object) { return marked_.insert(object).second; }
    bool is_marked(Object *object) const { return marked_.contains(object); }

    template<typename Visitor>
    void visit_strong_slots(Object *object, Visitor &&visitor)
    {
        for(TestCell &cell: object->strong_cells)
        {
            std::invoke(visitor, Slot(cell));
        }
    }

private:
    absl::flat_hash_set<Object *> marked_;
};
```

Graph construction finishes before traversal. `TestHeap` keeps object addresses
stable; neither root cells nor any object's cell vectors may be resized or
destroyed during collection. Heap ownership and logical identity are test
concerns, not extra generic collector APIs.

This representation deliberately differs from CloverVM's tagged cells. A
primitive integer and a null reference both load as no edge, without requiring
the generic algorithm to recognize either encoding. Future tests may add
external slot storage through the adapter without changing traversal.

## CloverVM Sketch

The production representation needs no new scanner dispatch:

```cpp
class CloverHeapAdapter
{
public:
    using Object = HeapObject;
    using Slot = HeapReferenceSlot;

    bool try_mark(Object *object) { return marked_.insert(object).second; }
    bool is_marked(Object *object) const { return marked_.contains(object); }

    template<typename Visitor>
    void visit_strong_slots(Object *object, Visitor &&visitor)
    {
        cl::visit_strong_reference_slots(object, [&](HeapReferenceSlot slot) {
            std::invoke(visitor, slot);
        });
    }

private:
    absl::flat_hash_set<Object *> marked_;
};
```

`visit_strong_reference_slots()` already combines initialized ordinary spans
and custom scanning, including supported externally stored references. Weak
cells are excluded. `HeapReferenceSlot::load_pointer()` handles CloverVM's
reference encoding; its collector-only rewrite bypasses RC/barriers and
preserves pointer storage class. The template must not reinterpret tagged bits
or substitute ordinary ownership assignments for that operation.

The first CloverVM instantiation should be an adapter test over explicitly
rooted real objects, not a production full-heap collection. The caller keeps
the entire traversal free of safepoints, reclamation, and graph mutation;
existing ownership must keep the graph alive. Neither the side marks nor the
native worklist provide that lifetime protection. Native scratch allocation
must not reenter the VM. If it fails, abandon the traversal and its temporary
state; no managed graph or lifetime-authority change has occurred.

Existing root visitors can be wired in only after their completeness and
publication contracts are established. Do not scan every allocated object as a
root or use positive refcounts as roots to make an incomplete integration pass.

## Later Evacuation Extension, Not First-Slice Requirements

Keep eventual copying in view without implementing a speculative full adapter
now. The likely additional primitives are:

| Primitive | Adapter responsibility | Algorithm responsibility |
|---|---|---|
| Classify an object | Read generation, placement/pinning, and movement eligibility | Decide whether the current collection must evacuate or scan it |
| Inspect copy layout | Preserve forwarding-safe size and initialized-state information | Obtain it before destructive forwarding changes |
| Query/install forwarding | Read/write a single source-to-destination association | Ensure one destination and enqueue it exactly once |
| Reserve/copy an allocation group | Place and copy one or two admitted objects, without tracing children or installing forwarding | Publish forwarding and ensure all destinations are scanned |
| Visit strong slots | Enumerate initialized cells, including header references and custom storage | Discover children and repair references |
| Enumerate/repair weak units | Expose embedded units and their coordinated repair/reset actions | Resolve settled targets early; defer only unresolved units until strong closure |

These are capability sketches, not existing allocator/header methods. Generation
and pinning must not be reduced to address-only inference. Production relocation
also needs layout admission, external-storage ownership, identity, header-state,
and allocation-failure contracts; the test heap cannot settle those implicitly.

The test adapter could copy one `TestObject` into separately owned destination
storage, preserving its logical identity and allocating distinct cell storage.
The CloverVM adapter would use reviewed destination allocation and per-layout
copy rules; blindly `memcpy`ing every object or invoking constructors again is
not such a rule. Both expose the same bounded group-copy operation to the
algorithm.

### One Work Queue Across Destination Heaps

Evacuation is a worklist-based copying collector, not a Cheney scan. A single
`std::deque<Object *>` holds all copied but unscanned destinations, whether they
are placed in the ordinary old-generation heap or the stable heap. Pop the
front before scanning, and push newly copied objects at the back. Processed
objects do not remain in the queue. A wide graph can still require a large
frontier; this is not a constant-space guarantee.

Source forwarding prevents duplicate copies and scheduling. The copying phase
does not need the standalone mark phase's hash set. Strong closure means the
single queue is empty after all roots/remembered edges have been supplied.
Ordinary/stable destinations follow identical scanning rules, including edges
that alternate repeatedly between those heaps.

There is no slab scan cursor, allocation-frontier traversal, object-start bitmap
walk, or `for_each_valid_object()` dependency in the copying algorithm. Layout
queries are still required to copy an individual object; they are not a means
of walking an arbitrary destination heap.

Allocation groups have a shared fixed bound of two, with inline storage and a
`size_t` count. Member zero is the primary; member one, when present, is its
exclusively owned companion. Named `single()` and `pair()` factories establish
those forms; no dynamic group vector or configurable capacity is needed.

```cpp
template<typename Object>
struct AllocationGroup
{
    std::array<Object *, 2> members;
    size_t count;

    static AllocationGroup single(Object *object);
    static AllocationGroup pair(Object *primary, Object *companion);
};

enum class EvacuationDestination { Ordinary, Stable };
```

The success-path sketch below is implemented by `src/gc/evacuation.h`.
`copy_group()` returns `std::array<Object *, 2>` corresponding to the source
members; only the group's counted prefix is used. It fully copies
the members without child traversal or forwarding publication. The destination
choice must respect the admitted group's placement constraints.

```cpp
void evacuate_slot(Slot slot)
{
    Object *source = slot.load_pointer();
    if(source == nullptr || !heap.is_collected_source(source))
        return;

    if(Object *destination = heap.forwarded_to(source))
    {
        slot.rewrite_pointer(destination);
        return;
    }

    auto group = heap.allocation_group(source);
    auto space = heap.destination_for(group);
    auto destinations = heap.copy_group(group, space);

    // Publish both associations before either destination is scanned.
    for(size_t i = 0; i < group.count; ++i)
        heap.install_forwarding(group.members[i], destinations[i]);

    for(size_t i = 0; i < group.count; ++i)
        pending.push_back(destinations[i]);

    slot.rewrite_pointer(destinations[0]);
}

void scan_object(Object *object)
{
    heap.visit_strong_slots(object, [&](Slot slot) { evacuate_slot(slot); });
}

void drain()
{
    while(!pending.empty())
    {
        Object *object = pending.front();
        pending.pop_front();
        scan_object(object);
    }
}
```

The caller supplies roots and, for a minor collection, the relevant remembered
slots, applying `evacuate_slot()` before draining. An uncollected object is not
thereby dead: old/stable owners' relevant edges still need remembered-set/root
coverage. This sketch does not supply that runtime integration.

`is_collected_source()` must remain true for an already forwarded source;
otherwise later references would skip forwarding lookup and retain old addresses.
It is collection-membership classification, not an unforwarded/liveness test.

Layout queries must remain valid when source metadata has been forwarded; do
not dereference an overwritten source shape as though it were ordinary metadata.
Every copied group member contributes one queue entry, irrespective of heap.

### Growable Slab-Based To-Space

Destination storage grows by acquiring additional slabs, not by moving or
reallocating existing destination storage. The adapter owns allocation and slab
lifetime; the explicit object queue owns scan scheduling. It does not matter
whether consecutive queued objects occupy consecutive slabs or different heaps.

Reserve an entire allocation group in one slab. If the current slab lacks room,
leave its unused tail and acquire a slab that can hold the complete aligned
group. No scan cursor needs to interpret that tail. Oversized
groups require an explicit large-destination policy and metadata capable of
describing every object start in the group.

`SlabAllocator` already supplies stable-addressed mappings, bump allocation,
and valid-object-start metadata. `GlobalHeap` owns slabs, registers address
lookups, and supports acquiring normal and dedicated large slabs. These are
useful building blocks, not an instruction to reuse mutator allocation or
reclamation-epoch policy inside collection unchanged.

Allocation may append storage while scanning, but existing destination objects
and their slot storage must keep stable addresses. Existing bitmaps may remain
where today's allocation/reclamation machinery requires them. They are not a
collector traversal requirement, and removing bitmap work from the young
allocation path is a future allocator goal, not part of this isolated slice.
Stable storage may retain different bookkeeping; no global bitmap removal is
implied.

Exhausting a slab is normal growth, not collection failure. Failure to obtain
another mapping or required native bookkeeping is genuine exhaustion. Current
`SlabAllocator` mapping failure is fatal. The copying collector likewise treats
destination and work-queue allocation failure as fatal, as specified below.
No live-size prepass or whole-heap reservation is implied.

### Exclusively Owned Overflow Storage

CloverVM overflow storage has one owning object and is reached through that
owner, not through another object's strong edge or an independent collection
root. On first evacuating the owner, its overflow storage therefore has not
already been discovered/forwarded independently. This is an ownership and
discovery invariant, not a best-effort placement heuristic.

When relocating the owner and its overflow storage together, reserve their
destination extents together and put the overflow object immediately after
the main object's extent, with the required alignment padding. The purpose is
locality when attribute access follows the owner's overflow pointer.

Keep two distinct heap objects: separate headers, logical identities,
forwarding associations, reference slots, and scan work. After copying and
installing forwarding for both, repair the owner's overflow reference and
schedule both destinations for strong scanning. A later encounter with that
reference uses the installed forwarding association, not another copy.

The generic collector must not know CloverVM's `overflow_storage` field.
The adapter describes an evacuation allocation group; ordinary objects have
a one-object group, while an admitted owner/overflow pair has a two-object
group. The collector must see every source/destination pair so that forwarding
publication and scan scheduling remain algorithm-owned. Do not hide recursive
tracing in a group-copy helper.

This records co-placement for a group being relocated; it does not silently
decide to move an object outside the current collection or override pinning and
stable placement. Mixed-generation/non-moving groups need an explicit policy
before production integration. A path that exposes overflow storage as an
independent root would violate the stated discovery invariant and must be
reviewed rather than quietly downgrading adjacency to best-effort.

Test independent alignment/extent accounting, adjacency, distinct forwarding
records, exactly-once scanning of both objects, shared/cyclic outgoing values,
and failure before either member's forwarding state is published. The synthetic
heap should model an exclusively owned companion without depending on CloverVM
layout details. This remains evacuation design, not implemented mark-phase code.

**Enumeration/allocation boundary:** collector-only destination reservation and
copying are permitted during slot enumeration. They must not trigger another
collection, run Python or mutator callbacks, reclaim objects, or invalidate the
object/slot storage currently being inspected. Rewriting the visited reference
cell is permitted; structurally mutating its containing storage is not. The
synthetic heap must preserve those addresses too, including when it grows.
This supports the normal copying operation: visit a slot, evacuate its target,
and rewrite the slot immediately. No staged-enumeration workaround is required.
The current visitor's broad no-managed-allocation comment must be clarified at
copying integration to distinguish mutator allocation from these collector-only
operations; it is not an inherent restriction of slot enumeration. No slot
handle is retained after its callback returns.

### Fatal Allocation Failure

Failure to acquire a destination slab, allocate queue storage, or obtain native
storage required for copying is fatal: terminate the process. Do not return a
recoverable allocation result, raise Python `MemoryError`, roll back, or resume
the mutator with a partially rewritten heap. This is the agreed initial policy,
not an unresolved requirement for whole-collection reservation.

The generic collector must route `std::bad_alloc` from its queue/collection
entry paths to a non-returning allocation-failure primitive supplied by the
adapter. Destination allocation must follow the same fatal contract. The test
adapter terminates too; test injected exhaustion with subprocess/death tests,
not a recoverable partially evacuated test heap. A non-returning adapter hook
keeps the generic algorithm independent of VM logging/initialization without
making recoverability a configurable collector strategy.

All members must be copied and initialized before publishing their forwarding
associations, then all must be scheduled. Fatal failure can occur during that
sequence: correctness is required for successful completion, not for resuming
after failure. Settled weak targets can be repaired early; unresolved units
finish only after the unified strong queue reaches closure. Source-space reuse
and lifetime cutover remain separate review gates.

## Weak Repair Units

The accepted [weak repair unit design](gc-weak-repair-units.md) specifies the
tri-state resolver, borrowed address/callback API, per-IC enumeration, early
repair, exact-unit deferral, coordinated payload reset, and lifetime/registration
contracts. This is design only; current CloverVM hooks remain late-only and
owner-wide until their own implementation slice.
## Evacuation Implementation Stages

These are bounded slices of isolated copying, not permission to enable CloverVM
movement. The mark phase remains usable and unchanged throughout.

1. **Grouped copying and forwarding in the test heap.** Add
   `src/gc/allocation_group.h` with the fixed two-member group, `size_t` count,
   and named `single()`/`pair()` factories. Extend `tests/gc_test_heap.h` with
   explicit normal/forwarded source state, a collector-only slot rewrite,
   exclusive companion ownership, and growable destination chunks for ordinary
   and stable placement. Destination storage keeps actual object/slot addresses
   stable as it grows. Group copying returns two inline destination pointers,
   preserves logical identities and strong/weak cell contents, and neither
   publishes forwarding nor traces children. Test the primitives directly;
   do not claim a complete collection yet.
2. **Shared copying traversal.** Add `src/gc/evacuation.h` containing the
   templated root/slot evacuation and single pending deque described above.
   Copy groups, publish every member's forwarding, enqueue every destination,
   and repair incoming slots. Exercise ordinary/stable chains, cycles, sharing,
   duplicate roots, self-references, companions, repeated source encounters,
   chunk growth during scanning, and large frontiers. Compare logical graph
   structure with an independent oracle and check that successful closure
   leaves no strong roots/slots pointing into collected source storage.
   Add fatal handling for native allocation exceptions at collection entry
   boundaries; test that failures terminate rather than escaping as recoverable
   exceptions. No forwarding-state reset may allow a source to be copied twice.
3. **Weak units in the isolated collector.** Add the tri-state resolver,
   borrowed unit/callback view, and test-adapter unit enumeration. Resolve old
   and already-forwarded targets early; defer only exact unresolved units.
   Finish them after strong closure, coordinating multi-field invalidation and
   payload reset. Test adjacent units, no deferral for settled units, late
   survival/death, partially resolved units, tagged/primitive fields, exact-once
   callbacks, and reset actions without whole-owner rescanning. Release test
   source storage only after strong and weak repair completes; this is test-heap
   lifecycle, not the production RC/tracing cutover.
4. **CloverVM weak-unit enumeration.** Refactor the existing owner-wide late
   weak hooks into per-unit enumeration/repair. CodeObject cache tables expose
   individual entries; a Shape backlink is a standalone-field unit. Exercise
   fake early/late resolvers, active payload arms, coordinated clearing of guards
   and payload, owned-plan release, and stable deferred addresses. No production
   object movement or new frame-root discovery. This slice does not claim
   remembered-unit/barrier coverage; review that integration separately.
5. **CloverVM copying adapter readiness.** Review real header/forwarding state,
   source classification, collector-only slab allocation, layout copy admission,
   forwarding-safe metadata, source lifetime, and bitmap removal boundaries.
   Roots, barriers, and lifetime authority still require their existing stage
   reviews. Do not add stub production copying methods or move real VM objects
   merely to make the isolated template compile.

The first slice's test-only source header models normal versus forwarded state;
it does not impose a production header layout. Existing mark tests still use
the same representation/adapter and must continue to pass. The synthetic heap
must enumerate its companion reference as a strong slot, not hide it only in
allocation metadata. The oracle must independently model that edge as well.

For the first slice, fixed-size C++ test records can be constructed in stable
raw destination chunks and explicitly destroyed with their native vectors at
heap teardown. This demonstrates real adjacency and stable addresses without
requiring a bitmap/object-size-based scan. It does not prove variable-sized
CloverVM copy rules or authorize rerunning CloverVM constructors during copying.

First-slice tests cover one/two-member groups, primary-first order, adjacency
and alignment of test records, source payload preservation, independent
forwarding records, forwarding queries after publication, growth that leaves
earlier destinations intact, both destination classes, and fatal injected
destination exhaustion. Forwarding is installed explicitly by the test caller
until the shared traversal is added in slice 2. Queue allocation failure belongs
to slice 2; the fatal behavior is fixed, not a new recoverability decision.

Each implementation slice receives a readiness review and focused tests followed
by required Debug/Release checks under the exclusive build/test lock, with
default Ninja parallelism. No VM runtime hot path changes in the first three
slices. Benchmark the copying kernel itself when assessing queue/placement cost,
not unrelated whole-VM workloads that never invoke it.

## Implemented Mark Phase and Verification

The shared algorithm lives in `src/gc/strong_closure.h` and the production
adapter in `src/gc/clover_heap_adapter.h`, under `cl::gc`. Existing memory
primitives remain in `src/memory/`. Test-only representation and instrumentation
live in `tests/gc_test_heap.h`, with coverage in `tests/test_gc_mark_phase.cpp`.
No separate library target or production collector hook is introduced.

Working-tree verification on 2026-10-31, based on `7ce87175`: all seven focused
mark/adapter tests passed. After switching the pending worklist to a deque,
full Debug `all check` passed 2,244 tests and full Release `all check` passed
2,235 tests; each configuration had three existing JIT-disabled skips and one
disabled test. The generic header passed a standalone C++20 syntax check without
VM include paths. Formatting and diff checks passed.

Synthetic tests use an independent fixed-point graph oracle, including 160
fixed-seed generated graphs and a 50,000-object chain. Real-object adapter tests
check known ordinary/custom strong edges, weak-only target exclusion, and
unchanged references, refcounts, lifecycle fields, and ZCT size. These assertions
do not claim an independent oracle for the entire VM metadata graph.

## Tests and Proof Boundary

The isolated strong copying phase is `Evacuation<Heap>` in
`src/gc/evacuation.h`. Its borrowed adapter must outlive the phase. Callers add
roots/remembered slots with `add_root()` and reach strong closure with `drain()`;
outside-source targets are left alone, not implicitly scanned. The phase is
noncopyable and nonmovable so pending scans cannot accidentally be duplicated.
One deque schedules both ordinary and stable destinations. Forwarding is
published for every group member before any is scanned. Native allocation
exceptions during construction, root evacuation, and draining reach the
adapter's non-returning allocation-failure handler.

`tests/test_gc_evacuation.cpp` compares copied graphs with a source-graph oracle
that does not use adapter enumeration or forwarding to discover reachability.
It checks graph identity/connections, source-payload preservation, exactly-once
destination scanning, unchanged weak cells, group adjacency, duplicate roots,
cycles, ordinary/stable closure, chunk growth during scanning, and wide
frontiers. Injected native allocation exceptions exercise the fatal public entry
paths; destination-exhaustion death tests also cover the primitive layer.
This does not inject a failure into the standard library's deque allocator.
Source storage remains alive after this phase: weak repair and reclamation
are not implemented or implied by strong closure.

Strong-evacuation working-tree verification on 2026-10-31, based on
`9e0ab41f`: all 22 focused GC tests passed. Full Debug `all check` passed
2,274 tests and full Release `all check` passed 2,265 tests; each configuration
had three existing JIT-disabled skips and one disabled test. The tests include
a 30,001-object chain, a 4,096-child frontier, and 24 fixed-seed generated
graphs. These checks cover the isolated adapter, not production relocation.

Exercise both instantiations; use synthetic graphs for exact comparison with an
independent oracle: empty roots, null/immediate cells, duplicate roots/edges, self-cycles,
mutual cycles, shared children, disconnected garbage, and weak-only targets.
Check that each reachable object is enumerated once and unreachable objects
are not enumerated. Fresh collection contexts must rediscover the same graph;
marks must not leak between runs. Include real CloverVM custom-layout scanning,
not only ordinary spans, in adapter tests.

For later movement, extend the test heap with bounded destinations, forwarded
sources, non-moving objects, stable promotions, external cells, and multiple
collection cycles. Keep the oracle independent of collector marks/forwarding;
compare logical identity, edges, sharing, and absence of surviving references
into discarded source storage.

The corresponding Lean model starts with a finite strong graph and a worklist:
each first discovery schedules one object; draining reaches strong closure.
An abstract proof assumes complete slot/root enumeration and valid stable
storage. Runtime adapter tests and audits establish those assumptions separately.
Passing the test-heap instantiation is not proof of CloverVM root completeness,
layout correctness, or relocation safety.
