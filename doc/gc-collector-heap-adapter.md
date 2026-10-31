# Isolated Collector and Heap Adapter

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Partial; isolated mark phase implemented, evacuation not implemented |
| Scope | One collector algorithm instantiated against a test heap and CloverVM |
| Owning layers | `src/gc/` owns traversal and the CloverVM adapter; memory layouts own slot enumeration; tests own the synthetic heap |
| Validated against | Mark-phase working tree based on `7ce87175` (2026-10-31) |
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
| Reserve/copy one object | Reserve destination storage and relocate one admitted layout | Choose ordering; do not hide child traversal in copying |
| Visit strong slots | Enumerate initialized cells, including header references and custom storage | Discover children and repair references |
| Process weak references | Apply a supplied survivor resolver using layout-specific rules | Run only after strong closure; never discover through a weak edge |

These are capability sketches, not existing allocator/header methods. Generation
and pinning must not be reduced to address-only inference. Production relocation
also needs layout admission, external-storage ownership, identity, header-state,
and allocation-failure contracts; the test heap cannot settle those implicitly.

The test adapter could copy one `TestObject` into separately owned destination
storage, preserving its logical identity and allocating distinct cell storage.
The CloverVM adapter would use reviewed destination allocation and per-layout
copy rules; blindly `memcpy`ing every object or invoking constructors again is
not such a rule. Both expose the same one-object operation to the algorithm.

All discovered destinations, including direct stable promotions, must join scan
closure. Non-moving reachable objects must not have their children silently
skipped. Start with an explicit object worklist rather than encoding the
algorithm around contiguous to-space or a Cheney allocation frontier.

**Enumeration/allocation boundary:** the current strong-slot visitor forbids
managed allocation or container mutation during enumeration. The initial
closure sketch satisfies that: it only changes native scratch state. A copying
implementation must not simply reserve managed destinations inside that callback.
Its readiness review must specify a staged schedule (inspection before
destination allocation, with later repair) or explicitly establish a safe,
collector-only reservation contract. Do not retain borrowed slot views across
operations that can invalidate their storage. This scheduling decision is open;
the table is not a claim that evacuation is implementation-ready.

Likewise, a moving pass cannot use the first slice's abandon-on-scratch-failure
rule after forwarding or root rewriting has begun. Reserve sufficient resources
before publication, or agree another safe failure policy first. Root repair,
weak processing, source-space reuse, and the lifetime cutover remain separate
review gates.

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
