# Isolated Collector and Heap Adapter

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Partial; isolated collector and CloverVM weak-unit enumeration implemented; production copying and weak remembered-set integration not implemented |
| Scope | One collector algorithm instantiated against a test heap and CloverVM |
| Owning layers | `src/gc/` owns traversal and the CloverVM adapter; memory layouts own slot enumeration; tests own the synthetic heap |
| Validated against | Groundwork through `19cc5f8e` (2026-11-01); historical verification recorded below |
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
source headers are authoritative for the implemented APIs. The mark phase,
isolated evacuation traversal, and CloverVM weak-unit enumeration are
implemented. Production copying-adapter wiring and weak remembered integration
remain separate review gates; the sketches do not imply runtime activation.

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
| Classify an object | Read generation, per-object NonMoving state, and collection-space membership | Decide whether the current collection must evacuate or scan it |
| Inspect copy layout | Preserve forwarding-safe allocated/copy extents and ordinary strong-span information | Obtain it before destructive forwarding changes |
| Query/install forwarding | Read/write a single source-to-destination association | Ensure one destination and enqueue it exactly once |
| Copy one object | Choose its destination and copy its initialized state, without tracing children or installing forwarding | Publish forwarding and enqueue the destination exactly once |
| Visit strong slots | Enumerate initialized cells, including header references and custom storage | Discover children and repair references |
| Enumerate/repair weak units | Expose embedded units and their coordinated repair/reset actions | Resolve settled targets early; defer only unresolved units until strong closure |

These are capability sketches, not existing allocator/header methods. Generation
and NonMoving are independent header facts, not address-only inference or merely
layout traits. Full pinning is deferred until reference mapping is in place.
Production relocation
also needs layout admission, external-storage ownership, identity, header-state,
and allocation-failure contracts; the test heap cannot settle those implicitly.

The test adapter could copy one `TestObject` into separately owned destination
storage, preserving its logical identity and allocating distinct cell storage.
The CloverVM adapter uses reviewed destination allocation and one generic
memcpy path driven by allocated/copy extents, not per-layout relocation
constructors. Layout admission and trace/repair coverage remain explicit. The
accepted temporary C++ lifetime exception for RC-backed member wrappers and
test-first sequence are recorded in the
[byte-copy protocol](gc-copying-adapter-readiness.md#accepted-byte-copy-protocol).
Both expose the same single-object copy operation to the algorithm.

### One Work Queue Across Destination Heaps

Evacuation is a worklist-based copying collector, not a Cheney scan. A single
`std::deque<Object *>` holds canonical marked but unscanned objects: copied
ordinary/stable destinations and existing collected NonMoving objects. Pop the
front before scanning, and push newly discovered objects at the back. Processed
objects do not remain in the queue. A wide graph can still require a large
frontier; this is not a constant-space guarantee.

Source forwarding prevents duplicate copies. Adapter marks deduplicate scanning
of canonical destinations and existing NonMoving objects; the isolated adapter
uses per-phase scratch marks, not a second traversal before copying. Strong
closure means the single queue is empty after all roots/remembered edges have been supplied.
Ordinary/stable destinations follow identical scanning rules, including edges
that alternate repeatedly between those heaps.

There is no slab scan cursor, allocation-frontier traversal, object-start bitmap
walk, or `for_each_valid_object()` dependency in the copying algorithm. Layout
queries are still required to copy an individual object; they are not a means
of walking an arbitrary destination heap.

The success-path sketch below is implemented by `src/gc/evacuation.h`.
`copy_object(source)` chooses a destination internally, allocates one object,
and initializes its copied state before returning its address. It does not
scan children or install forwarding. The concrete adapter is plumbing: a
separately testable CloverVM placement decision selects the destination, then
`EvacuationDestinations` performs allocation, byte copying, and destination
header preparation using borrowed persistent spaces. Neither a destination enum
nor a placement aggregate is part of the generic collector interface. Real
destination tests exercise this implementation independently of the synthetic
heap; placement-policy and adapter integration tests remain necessary when
those pieces are introduced. No production copying is wired up by the
destination component alone.

```cpp
void enqueue_once(Object *object)
{
    if(heap.try_mark(object))
        pending.push_back(object);
}

void discover_slot(Slot slot)
{
    Object *object = slot.load_pointer();
    if(object == nullptr || !heap.is_collected(object))
        return;

    if(heap.is_movable(object))
    {
        Object *destination = heap.forwarded_to(object);
        if(destination == nullptr)
        {
            destination = heap.copy_object(object);
            if(destination == nullptr)
                heap.allocation_failure();
            heap.install_forwarding(object, destination);
        }
        enqueue_once(destination);
        slot.rewrite_pointer(destination);
        return;
    }
    enqueue_once(object);
}

void scan_object(Object *object)
{
    heap.visit_strong_slots(object, [&](Slot slot) { discover_slot(slot); });
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
slots, applying `discover_slot()` before draining. An uncollected object is not
thereby dead: old/stable owners' relevant edges still need remembered-set/root
coverage. This sketch does not supply that runtime integration.

The adapter answers two independent questions: `is_collected()` selects this
collection's inputs, while `is_movable()` describes allocation capability and
does not inspect collection mode or forwarding state. Both remain true for a
selected forwarded source, so references still follow forwarding. A fresh
ordinary destination is movable but uncollected. Full collections include all
NonMoving objects, including fresh stable copies; young collections exclude
these old allocations. No per-collection allocation boundary or epoch field is
needed to distinguish existing and fresh NonMoving records. Test membership
before choosing copy versus trace in place. Source-space indexing is a
lower-level allocator concern, not the meaning of the movability query.

After copying, the collector publishes source forwarding, marks/enqueues the
canonical copy, and only then rewrites the referencing strong slot. Copying and
forwarding publication cannot invoke callbacks that reenter tracing. Thus a
fresh NonMoving copy is already marked when encountered through a repaired
reference: repeated strong discovery does not rescan it, and weak references
directly to the copy resolve through its mark. A raw destination reservation or
copy alone grants no weak liveness. Unmarked NonMoving targets remain pending
early and become dead after strong closure, regardless of allocation age.

Layout queries must remain valid when source metadata has been forwarded; do
not dereference an overwritten source shape as though it were ordinary metadata.
Every strongly discovered canonical object contributes one queue entry,
irrespective of heap or whether it was copied.

### Growable Slab-Based To-Space

Destination storage grows by acquiring additional slabs, not by moving or
reallocating existing destination storage. The adapter owns allocation and slab
lifetime; the explicit object queue owns scan scheduling. It does not matter
whether consecutive queued objects occupy consecutive slabs or different heaps.

Each object allocation fits in one slab. If the current slab lacks room, leave
its unused tail and acquire a sufficiently large slab. No scan cursor needs to
interpret that tail. Oversized reservations do not by themselves authorize
large-object movement; layout admission and destination bookkeeping still need
their own review.

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

### Overflow Storage Is an Ordinary Strong Edge

An object's overflow pointer is an ordinary strong slot. Copy the owner,
enqueue it, and discover its backing storage while scanning that slot. Copy
each selected object independently; an existing forwarding association prevents
duplicate copying regardless of root order. Old/stable owners' relevant slots
must still be supplied through remembered-set/root coverage.

There is no owner backlink, paired placement, or adjacency guarantee in the
collector contract. Owner/backing co-placement is a deferred locality
optimization, not a reason to add a multi-object evacuation primitive.

The adapter's source selection is independent of destination placement: a full
collection can select old movable sources as well as young ones. Fresh
destinations remain outside that selection even though promotion makes their
generation Old. Non-moving full-collection marking/sweeping and weak liveness
remain separate integration work.

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

The generic collector has no C++ exception handlers. Construction and public
collection operations are `noexcept`, so an escaping native exception terminates
rather than unwinding to a caller. Standard-container allocation failure need
not pass through the adapter's diagnostic. Explicit adapter allocation failures
use its non-returning allocation-failure primitive; a null `copy_object()` result
is rejected through that primitive before publishing forwarding. Destination
allocation follows the same fatal contract. The test adapter terminates too;
test injected exhaustion with subprocess/death tests, not a recoverable
partially evacuated heap. This keeps failure handling independent of VM logging
and permits runtime instantiation with `-fno-exceptions`. Do not assume that
this compiler flag makes ordinary `new` or standard allocators return null.

Each object must be copied and initialized before publishing its forwarding
association, then scheduled. Fatal failure can occur during that sequence:
correctness is required for successful completion, not for resuming
after failure. Settled weak targets can be repaired early; unresolved units
finish only after the unified strong queue reaches closure. Source-space reuse
and lifetime cutover remain separate review gates.

## Weak Repair Units

The accepted [weak repair unit design](gc-weak-repair-units.md) specifies the
tri-state resolver, borrowed address/callback API, per-IC enumeration, early
repair, exact-unit deferral, coordinated payload reset, and lifetime/registration
contracts. The isolated collector implements this protocol; CloverVM layout
hooks enumerate individual IC entries and Shape backlinks through the same
compact unit representation. Runtime copying and weak remembered-set coverage
remain separate integration work.

## Evacuation Implementation Stages

These record the bounded implementation sequence, not permission to enable
CloverVM movement. Slices 1–4 are implemented; slice 5 has preparatory groundwork
but no production copying adapter. The mark phase remains usable.

1. **Single-object copying and forwarding in the test heap.** Extend
   `tests/gc_test_heap.h` with explicit normal/forwarded source state, a
   collector-only slot rewrite, and growable destination chunks for ordinary
   and stable placement. Destination storage keeps object/slot addresses stable
   as it grows. `copy_object(source)` chooses placement internally, preserves
   logical identity and strong/weak cell contents, and neither publishes
   forwarding nor traces children. Test the primitives directly; do not claim
   a complete collection yet.
2. **Shared copying traversal.** Add `src/gc/evacuation.h` containing the
   templated root/slot evacuation and single pending deque described above.
   Copy one object, publish its forwarding, enqueue its destination,
   and repair incoming slots. Exercise ordinary/stable chains, cycles, sharing,
   duplicate roots, self-references, backing references, repeated source
   encounters, chunk growth during scanning, and large frontiers. Compare
   logical graph structure with an independent oracle and check that successful closure
   leaves no strong roots/slots pointing into collected source storage.
   Make collection entry boundaries `noexcept`; reject null copy results before
   forwarding and test terminal failure. No forwarding-state reset may allow a
   source to be copied twice. Instantiate the template with exceptions disabled.
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
   Unit enumeration and tri-state repair are implemented; the owner-wide
   late-only hooks are removed. Real cache tests retain fake relocation maps,
   not actual object movement.
5. **CloverVM copying adapter readiness.** Review real header/forwarding state,
   source classification, collector-only slab allocation, layout copy admission,
   forwarding-safe metadata, source lifetime, and bitmap removal boundaries.
   Roots, barriers, and lifetime authority still require their existing stage
   reviews. Do not add stub production copying methods or move real VM objects
   merely to make the isolated template compile.

   The [copying-adapter investigation](gc-copying-adapter-readiness.md) records
   current code evidence and preparatory slices. Source selection and
   single-object copying are exercised in the isolated heap; live-layout queries,
   the transitional header, and raw HeapSpace allocation do not enable runtime
   movement.

The first slice's test-only source header models normal versus forwarded state;
it does not impose a production header layout. Existing mark tests still use
the same representation/adapter and must continue to pass. The synthetic heap
represents backing references using the same ordinary strong cells as other
edges.

For the first slice, fixed-size C++ test records can be constructed in stable
raw destination chunks and explicitly destroyed with their native vectors at
heap teardown. This demonstrates stable destination addresses without
requiring a bitmap/object-size-based scan. It does not prove variable-sized
CloverVM copy rules or authorize rerunning CloverVM constructors during copying.

Primitive tests cover copied-state preservation, independent forwarding,
alignment, per-object destination choice, stable addresses across chunk growth,
and fatal destination exhaustion. These tests do not enable production copying.

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

## Collection Selection

The isolated test adapter distinguishes collection participation
(`is_collected`) from allocation movability (`is_movable`). YoungOnly
selects young movable sources. Full participation includes young/old movable
sources and existing NonMoving objects. Movability is independent of selection:
an uncollected old moving object is still movable, as is a fresh ordinary
destination. Existing NonMoving objects and fresh stable destinations are not
movable. Fresh ordinary destinations are excluded in either mode; fresh stable
destinations participate in Full just like existing NonMoving objects. Forwarding
does not change source membership or source movability, even when its target
is non-moving.

`TestStorage::NonMoving` and `make_non_moving_object()` represent existing stable
objects; StableDestination records preserve allocation provenance for test
instrumentation, not a different full-collection participation policy.
Evacuation consumes both predicates: strong discovery canonicalizes moving
references, then marks and queues canonical destinations or existing NonMoving
objects once. Only
canonical objects are scanned; movable source payloads remain untouched, while
live NonMoving owners have their slots repaired in place. Use fresh per-phase
adapter marks; do not run a separate mark pass over sources before copying.

Weak resolution leaves uncollected targets alone, follows forwarding for moving
sources, and tests marks for collected NonMoving targets. Unresolved collected
targets are deferred early and cleared after strong closure. Weak references
never mark or copy targets. Both strong and weak behavior change together.
This is isolated tracing/copying, not stable sweep, immortal-object policy, VM
adapter integration, or RC lifetime cutover. Young/full specialization belongs
to a future production adapter; no extent search is imposed by this protocol.

Owner/backing graphs use ordinary strong cells. Selection and forwarding apply
independently to each object, regardless of root order or preferred destination.
An old owner's actual remembered slot must be supplied to repair a young target;
rooting the target alone cannot repair that slot. Source-storage retirement
preserves NonMoving records and unselected old sources; it is not stable-object
sweeping, even when Full participation includes those NonMoving records.

## Tests and Proof Boundary

The [first Lean slice](../proofs/gc/README.md) proves finite-graph strong closure,
exactly-once scanning and the scan bound for an executable model corresponding
to `StrongClosure`. It does not yet model evacuation or weak units. Complete
runtime roots, faithful slot decoding/enumeration, resource success, and the
C++ correspondence remain outside this proof; no production collection is
enabled by it.

The [second Lean slice](../proofs/gc/copying-discovery.md) adds executable copying
discovery/forwarding and proofs of local canonicalization, record preservation
and mark/queue discipline, including NonMoving originals and destinations. It
does not yet prove the copying scan loop, weak handling or safe source disposal.

The tracing/copying checkpoint `1c001cda` and the subsequent orthogonal-protocol
working tree both passed 81 focused Debug GC/storage/copy tests and 80 applicable
Release tests. Full Debug `all check` passed 2,378 tests and full Release passed
2,362, with three existing JIT-disabled skips and one disabled test each. The
revision removes the combined adapter predicate in favor of `is_collected()`
and `is_movable()`, and renames the mapping index to `EvacuationSpaces::contains()`.
Tests include uncollected-but-movable old objects and ordinary destinations,
non-moving stable destinations, and sources forwarded into non-moving storage.
The independent oracle covers mixed moving/NonMoving graphs and in-place weak
repair; source payloads remain unchanged. The no-exceptions instantiation,
formatting, and diff checks pass. No VM collection is activated.

Collection-participation preparation on 2026-11-01, based on `4b212180`, passed
74 focused Debug GC/storage/copy tests and 73 applicable Release tests. Full
Debug `all check` passed 2,371 tests and full Release passed 2,355; each reports
three existing runtime-JIT-disabled skips and one disabled test. Contract tests
cover both selection modes, existing NonMoving versus fresh stable destinations,
factory ownership/identity, and classification after forwarding. Existing stable
weak-repair fixtures now use heap-owned NonMoving objects with explicit strong
roots. Formatting and diff checks pass. No production collector code changed;
the new query still does not implement full NonMoving tracing or weak liveness.

Single-object simplification verification on 2026-11-01, based on `a7c007bf`:
50 focused GC/storage tests passed; full Debug `all check` passed 2,339 tests
and full Release passed 2,328 tests, with three JIT-disabled skips and one
disabled test in each configuration. The owner/backing matrix covers 64
combinations of source selection, generations, root order, and independent
destination choices. A dedicated test checks that backing storage is not copied
before scanning its owner's strong slot. No runtime relocation is enabled.

The isolated strong copying phase is `Evacuation<Heap>` in
`src/gc/evacuation.h`. Its borrowed adapter must outlive the phase. Callers add
roots/remembered slots with `add_root()` and reach strong closure with `drain()`;
uncollected inputs are left alone, not implicitly scanned. Collected NonMoving
objects are marked and scanned in place. The phase is
noncopyable and nonmovable so pending scans cannot accidentally be duplicated.
One deque schedules ordinary/stable destinations and existing NonMoving inputs.
Forwarding is
published for each object before it is scanned. Construction and public entry
points are `noexcept`; native allocation exceptions terminate without requiring
C++ exception support in the collector. A null copy result reaches the adapter's
non-returning allocation-failure handler before forwarding is published.

`tests/test_gc_evacuation.cpp` compares copied graphs with a source-graph oracle
that does not use adapter enumeration or forwarding to discover reachability.
It checks graph identity/connections, source-payload preservation, exactly-once
destination scanning, repaired weak targets, duplicate roots,
cycles, ordinary/stable closure, chunk growth during scanning, and wide
frontiers. A null-copy death test exercises the adapter failure path;
destination-exhaustion death tests also cover the primitive layer. A separate
translation unit explicitly instantiates the complete collector template with
`-fno-exceptions`, and static assertions check the public noexcept contracts.
This does not inject a failure into the standard library's deque allocator.
Source storage remains alive through `finish()`, which follows strong closure
with exact-unit weak repair. Source reclamation remains a caller responsibility;
the test heap provides an explicit source-disposal operation for tests.

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
