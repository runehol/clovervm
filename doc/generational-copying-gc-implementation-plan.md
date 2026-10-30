# Generational Copying GC Implementation Plan

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Accepted |
| Implementation | Partial |
| Scope | Incremental transition from deferred refcounting to tracing and moving collection, including scanning, rewriting, barriers, and a collector model for Lean proofs |
| Owning layers | Memory manager owns collection; runtime layouts and root publication supply precise mutable references; mutator store paths own barriers |
| Validated against | Slice 4b.1 working tree based on `f46b5600` (2026-10-30) |
| Supersedes | N/A |

The target architecture is described in
[Generational Copying GC Design Notes](generational-copying-gc.md). This plan
stages that work through usable runtime states. Deferred refcounting remains
authoritative while we introduce layout queries, scanners, mutable roots, and
write barriers. Lifetime authority transfers to tracing at an explicit cutover
before enabling nursery evacuation and bulk discard.

The first groundwork is already present: `Instance` reports its full extent
from its current shape's inline capacity while retaining lazy initialization
and an auxiliary initialized-slot count. Concrete construction helpers use
`allocation_size_for(...)`; existing-object queries use
`object_size_in_bytes(...)`. Those changes do not implement tracing or movement.

## Collector Boundary

Keep the collection algorithm separate from Python semantics, interpreter frame
formats, tagged-pointer encoding, and slab mechanics. The runtime adapter
supplies the operations the algorithm requires:

```text
CloverVM adapter
    enumerate mutable roots
    enumerate strong and weak object slots
    read layout and generation
    reserve destination storage
    copy initialized state
    read and write forwarding state

Collector algorithm
    evacuate roots and remembered references
    drain ordinary and stable-promotion work queues
    process weak references after strong closure
    complete collection

Mutator integration
    allocation policy
    ownership operations
    write barriers
    safepoints and root publication
```

This is a responsibility boundary, not a commitment to virtual dispatch or a
large heap framework. Agree the concrete interface when its first consumer is
implemented, following existing low-overhead runtime patterns. The algorithm
must also run against a small test heap without constructing a VM.

The collector model and the production algorithm should expose corresponding
state transitions. Lean proofs establish properties under explicit adapter
assumptions; they do not by themselves verify the C++ adapter.

## Working Runtime Stages

| Stage | Deliverable | Lifetime authority |
|---|---|---|
| 1 | Ordinary reference-span resolution consumed by reclamation | Refcounting |
| 2 | Strong-slot enumeration and collector-only rewriting | Refcounting |
| 3 | Custom-layout scanning and separate weak-reference processing | Refcounting |
| 4 | Mutable roots and relocation-safe native references | Refcounting |
| 5 | Generation state, remembered sets, and write barriers | Refcounting |
| 6 | Tracing verifier over the existing non-moving heap | Refcounting |
| 7 | Isolated evacuation algorithm, test heap, and Lean model | Refcounting in the VM |
| 8 | Non-moving tracing reclamation and ownership cutover | Tracing |
| 9 | Nursery allocation and evacuation for audited layouts | Tracing |
| 10 | Wider coverage, refcount/ZCT retirement, and optimization | Tracing |

Stages 1–3's layout/weak-processing primitives and audit are implemented.
Production collector integration and stages 4–10 remain pending.

Each stage is a review boundary, not necessarily one commit. Split root sources,
layout families, and store families into bounded slices. Stages 1–7 can land
without moving live VM objects. The isolated collector and its model can be
developed once their interface is agreed, alongside the remaining integration
work.

### 1. Resolve Ordinary Reference Spans

Extract the static, SMI-counted, and auxiliary-counted span interpretation from
`ReclamationContext::reclaim_object()`. The query resolves the span's starting
offset and count. Reclamation consumes that result and retains its current
clear-and-release behavior.

Keep custom deallocation separate: it cannot be used to enumerate references.
An unsupported custom layout must not be represented as an empty ordinary
span. This first slice does not require introducing the entire unified
descriptor table or all future callbacks.

This extracts ordinary span resolution from the current release machinery;
`ReleaseKind` does not become the future scanning classification. A custom
deallocator says how current reclamation handles the layout, not whether it
has references or can support scanning and rewriting.

Implemented by `OrdinaryReferenceSpan` and
`resolve_ordinary_reference_span(object, descriptor)` in
`src/memory/native_layout_descriptor.h`, consumed by reclamation. A supported
empty span has a zero count; `std::nullopt` explicitly means custom handling is
required. The result describes one contiguous in-object span and does not
enumerate external storage or disjoint spans.

Completion: existing reclamation behavior passes unchanged, with focused
coverage of static spans, both dynamic count sources, empty spans, and lazy
`Instance` storage. The query must not read uninitialized capacity.

### 2. Enumerate and Rewrite Strong Slots

Expose actual mutable reference locations using the resolved ordinary spans.
Account explicitly for tagged `Value` cells and raw heap-pointer fields rather
than assuming all C++ storage can safely be accessed as `Value &`.

A collector rewrite changes the address of an existing logical reference. It
preserves the required encoding and performs no incref, decref, mutator write
barrier, allocation, or safepoint. Ordinary mutator assignments retain their
ownership behavior.

Implemented by the borrowed `HeapReferenceSlot` view in
`src/memory/heap_reference_slot.h` and the templated
`visit_ordinary_reference_slots(object, span, visitor)` walker alongside the
span resolver. The walker visits every initialized cell in the supplied span.
`load_pointer()` returns null for immediates and null pointers;
`rewrite_pointer()` requires an existing reference and a non-null replacement
in the same pointer storage class.

Tagged references and raw heap pointers currently share their pointer bits,
including the address bits identifying refcounted or interned storage. The
view uses byte copies to access a cell, including storage inside `Member`
wrappers, without treating every field as a `Value &` or invoking assignment
operators. Slot storage must remain valid throughout the visit. Custom scanners
can reuse the view for individual external or disjoint cells in stage 3.

Tests perform simulated replacements and restore owned references before
ordinary refcount teardown. This stage does not relocate VM objects or connect
the primitives to a production collector.

Completion: tests rewrite references between controlled test objects, preserve
immediates and nulls, leave refcounts and ZCT membership unchanged, and never
visit unused inline storage. No VM objects move yet.

### 3. Cover Custom Layouts and Weak References

Implement this stage in bounded slices:

- **3a: separate managed-reference release from native destruction.** Replace
  the combined custom deallocator with independent `custom_release` and
  `destroy` callbacks. Reclamation releases ordinary or custom references,
  invokes optional native destruction, then retires the allocation. Empty
  owning C++ containers during release so destruction cannot release managed
  references a second time. Direct `Member` fields retain their existing
  explicit-release behavior. No scanners, weak processing, movement, or
  ownership-wrapper conversion are part of this slice. This slice is
  implemented for `CodeObject`, `Shape`, and `JitCodeObject`; direct phase tests
  check that native destruction does not release managed ownership again.
- **3b: enumerate and rewrite strong references**, including external owned
  storage in the three custom layouts. Add a non-owning `StrongSlotVisitor`
  (`absl::FunctionRef<void(HeapReferenceSlot)>`) and a common
  `visit_strong_reference_slots(object, visitor)` entry point. Extend the
  existing descriptor with an optional `custom_trace_strong` callback,
  independent of release and destruction. Visit ordinary spans and then
  supplemental custom slots. CodeObject's native constant table and owned
  expanded-call keyword names are visited directly; its VM array wrappers
  expose their backing handles, whose elements are scanned separately through
  the backing objects. Shape exposes class, all descriptor names (including
  latent descriptors), and owned transition names and successors. JitCodeObject
  exposes only its tagged pool prefix. Leave raw weak-cache references and
  `Shape::previous_shape` for 3c. Visits do not recurse, allocate managed
  objects, safepoint, execute Python, mutate container structure, or alter
  refcounts/ZCT state. No production collector or reclamation changes belong
  to this slice. This slice is implemented; regression tests exercise ordinary
  lazy spans, custom enumeration, repeated aliases, external backing-handle
  rewriting, latent descriptor names, and tagged-only JIT pool rewriting.
- **3c: audit remaining layouts and implement weak processing**, keeping
  coherent cache repair separate from strong tracing. Use a non-owning
  `WeakTargetResolver` (`absl::FunctionRef<HeapObject *(HeapObject *)>`) and
  an optional descriptor `process_weak` callback behind
  `process_weak_references(object, resolver)`. Run only after strong closure;
  the resolver returns a surviving address (including identity for uncollected
  storage) or null for a dead target, without marking or dereferencing dead
  inputs. Shape repairs/clears its back-link. CodeObject repairs all raw cache
  references, respecting shape-key sentinels and active union arms, or resets
  an entire entry if a target dies. Include keyword-call caches in full cache
  clearing. Successful repairs are RC-neutral; resetting an expanded-call
  entry releases its strongly owned keyword-name plan through normal ownership
  rules. Those names have already participated in strong closure. Use fake
  liveness/forwarding maps with physically live objects in tests, restoring
  successful substitutions before RC teardown. Audit each registered layout's
  strong, weak and native-only storage without changing production reclamation
  or adding Python weakref objects. The callbacks and fake-liveness tests are
  implemented. The exhaustive
  [Native Layout Reference Audit](native-layout-reference-audit.md) records
  ownership, weak fields, native-only state, and unresolved relocation
  prerequisites (including transferred array-backing extent metadata).

Audit every native layout, including references in C++ containers, backing
records, compiled code, and caches. Separate strong-slot enumeration, weak
processing, managed-reference release, and native destruction. Existing custom
deallocators are evidence to inspect, not a complete inventory of tracing needs.

The current custom layouts are `CodeObject`, `Shape`, and `JitCodeObject`.
All three must support complete strong-reference scanning and rewriting,
separate weak processing where applicable, managed-reference release while
refcounting remains, and independent native destruction. `JitCodeObject`'s
tagged constant-pool slots are included even though they live outside its own
allocation.

Ordinary strong spans, additional custom scanning, and custom destruction may
coexist in one descriptor. Custom destruction neither implies an empty strong
span nor prevents scanning. These layouts eventually live in non-moving
storage, but their outgoing references to movable objects must still be traced
and rewritten.

Use the unified layout design in
[Native Layout Descriptors](native-layout-descriptors.md) as the target. Migrate
its queries and consumers in bounded slices. Report object extent, initialized
extent, and strong slots separately; retain the shape-derived `Instance`
capacity and auxiliary initialized prefix.

Weak processing happens after strong closure. Repair or clear coherent cache
entries as a unit. Objects in stable storage still expose outgoing references
to movable objects. Resource-owning layouts need a deliberate destruction and
placement contract before they can enter a moving space.

Completion: a per-layout audit identifies every reference category, and tests
exercise custom enumeration and weak repair/clearing without performing a real
collection. No layout is considered fully traceable merely because it has a
release descriptor.

### 4. Publish Mutable Roots

Replace address-only root discovery with enumeration of the storage that owns
each live reference. The current `ReclamationRootSet` loses those locations,
and its conservative stack candidates cannot simply become writable roots.

Split the audit and implementation by source:

- managed frame slots, excluding frame metadata and non-reference words;
- published accumulator and active code, including restoration after collection;
- thread and VM fields, caches, and immortal/stable objects owning references;
- CloverVM native handles and C++ references live across safepoints;
- JIT mainline, transition, side-exit, and compiled-constant state.

`Owned<T>` currently retains a value; it does not register an updateable root.
Define how native code roots and reloads live references across safepoints.
Interior addresses and borrowed references require explicit lifetime rules.
For the same logical root stored in multiple places, update every place that
execution can subsequently read.

Completion: root visitors can perform controlled rewrite tests, and resumed
execution reads the rewritten locations. No potentially movable reference
survives solely through conservative discovery or a positive refcount.

#### 4a. Scoped Native Root Cells

Introduce `ScopedRoot(ThreadState &, HeapReferenceSlot)` as an allocation-free,
non-copyable, non-movable LIFO registration of one initialized native cell.
`ThreadState::visit_native_root_slots(StrongSlotVisitor)` visits the actual
writable cells, including distinct cells aliasing the same object. Registration
borrows storage and performs no retain/release operations. The cell and thread
must outlive registration; the cell must remain writable and address-stable.
Enumeration cannot mutate the chain, allocate managed objects, or reenter.

Callers must reload the registered cell after safepoints rather than retaining
an extracted raw pointer. Existing `Owned<T>` ownership remains independent;
registration does not automatically apply to all owners. Integrate the explicit
cells read-only into current reclamation, leaving the conservative stack scan
and refcounting operational. Test scope cleanup, aliases, per-thread isolation,
controlled RC-neutral rewrites, and zero-count liveness across reclamation.

#### Remaining Root Slices

- **4b.1: Thread persistent roots.**
  `ThreadState::visit_persistent_root_slots(StrongSlotVisitor)` enumerates the
  native-layout class array, five exact-shape caches, and both initialized
  pending-exception cells. Visit all physical aliases without RC operations.
  The method is non-const because it exposes writable member cells; current
  reclamation takes a mutable thread reference and consumes these cells through
  the same read-only adapter as scoped native roots. Do not treat ZCT entries,
  native pointers, mixed JIT storage, or saved interpreter state as persistent
  roots. Verify coverage, getter-visible rewrites with restored substitutions,
  unchanged ownership, and reclamation visibility.
- **4b.2: VM persistent fields.** Enumerate builtin identities, helper
  functions, modules, shapes, and entry adapters.
- **4b.3: Native containers and immortal owners.** Cover intern tables,
  pointer-keyed maps, and outgoing references from immortal objects. Settle key
  repair without invalidating hash-table placement; distinguish roots from
  owned heap edges.
- **4c: Published interpreter state.** Expose writable accumulator publication,
  protect saved publication state across native reentry, and reload before
  dispatch resumes. Keeping code objects non-moving does not remove their
  tracing requirement.
- **4d: Precise frame roots.** Distinguish parameters, locals, temporaries,
  saved code objects, and continuation operands from native PCs, frame pointers,
  padding, and scalar metadata.
- **4e: Native handles and callers.** Migrate safepoint-crossing native locals
  and settle handle-cell address stability, overflow chunks, and interior
  pointers.
- **4f: JIT boundaries and closure audit.** Cover supported JIT safepoints and
  test complete root coverage. Preserve transition execution's no-safepoint
  contract; do not introduce loop-backedge safepoints in this work.

Each remaining slice requires its own readiness review. Slice 4a does not enable
movement or remove conservative root discovery.

### 5. Introduce Barriers Alongside Refcounting

Add generation and remembered state alongside the current header fields. Accept
the temporary larger header; do not remove refcount, lifecycle, or native-layout
state to make room. Settle the concrete transitional header in its own review.

Introduce object-level remembered sets and barrier entry points following the
GC design's hot-path lessons. Cover ordinary members, raw pointer stores,
backing arrays, bulk copies, runtime caches, and relevant generated stores.
Distinguish construction into young storage from construction into old or
stable storage. Retain current incref/decref behavior throughout.

Provide a non-moving test configuration with young and old classifications so
the barrier is exercised before a nursery exists. Compare remembered coverage
with an exhaustive scan. Remembered membership must never retain an unsafe
address after refcount reclamation or storage reuse; define cleanup or
invalidation before enabling these sets in the running VM.

Completion: all old-to-young edges are covered after mutation, including bulk
and construction paths. Immediate stores avoid irrelevant work. Measure the
cost of changed hot store paths while checking the existing runtime still works.

### 6. Exercise Tracing Without Reclaiming Through It

Run a tracing verifier over the real non-moving heap using the new roots and
scanners. Refcounting continues to reclaim objects; verification runs at a
coordinated point where its inspected storage remains valid.

Check reachable-slot validity, initialized extents, root publication, and
remembered-set coverage. Do not treat every positive refcount as a tracing root:
that would hide missing root declarations. Tracing may find unreachable cycles
that refcounting retains; that difference is expected.

Completion: normal execution and forced-safepoint tests exercise this verifier
without changing addresses or lifetime authority. Scanning completeness still
requires the explicit layout/root audits; a successful traversal alone cannot
prove it.

### 7. Implement Evacuation Against a Test Heap

Implement forwarding, destination reservation, initialized-state copying, root
repair, ordinary queue draining, stable-promotion queue draining, and weak
processing through the agreed collector interface. Preserve readable layout
metadata when a source object or its shape has already been forwarded.

Test cycles, shared children, duplicate roots, old-to-young references, and work
that crosses between both destination queues. Define capacity preconditions or
failure behavior before publishing any forwarding state; a partially rewritten
heap cannot simply resume the mutator after an allocation failure.

Completion: the isolated algorithm preserves the test heap's reachable graph,
and the Lean model and executable comparisons described below cover its state
transitions. Production lifetime authority remains refcounting.

### 8. Transfer Lifetime Authority to Tracing

Establish tracing reclamation with stable addresses first, then make tracing
authoritative at an explicit mode boundary. Review the non-moving reclamation
policy, weak processing, destruction order, and treatment of cycles before this
stage is implemented. This supplies the reclamation path needed by stable
allocations; it does not settle whether the eventual ordinary old generation
will move.

Do not combine nursery bulk discard with active deferred-refcount reclamation
without a separate ownership design. A dead nursery object may own an old
object: skipping its release leaks a count, while using existing release paths
can enqueue nursery addresses into ZCT machinery. The selected staging avoids
that hybrid by transferring lifetime authority before production evacuation.

Refcount fields may remain temporarily, but refcount operations and ZCT actions
must have defined behavior in tracing mode. Root registration and tracing must
replace their lifetime guarantees before those guarantees are disabled.

Completion: tracing reclaims the non-moving heap, including cycles, with correct
weak handling and native teardown. Exactly one mechanism controls reclamation
in each runtime mode. Existing deferred-refcount mode remains usable until the
cutover is verified.

### 9. Enable Restricted Production Movement

Connect nursery allocation and the isolated evacuation algorithm to the runtime
adapter. Begin with explicitly audited layouts and keep other allocations in
non-moving storage. Stable owners still need scanning and barriers for nursery
children. Use immediate promotion of nursery survivors as the initial policy.

Before a layout can move, establish its slot completeness, initialized extent,
copy/relocation safety, identity and hash behavior, and raw-address exposure
rules. Resolve the existing `id()` and identity-derived hash questions before
moving affected objects. Safepoint publication and restoration must cover all
execution paths that can hold their references.

Completion: forced collections preserve identity, sharing, cycles, native
handles, and interpreter/JIT recovery. Verify that surviving roots and strong
slots contain no references into discarded nursery storage. Test this property
before permitting source-space reuse.

### 10. Expand Coverage and Retire Transitional Machinery

Expand nursery admission by audited layout family. Remove refcount operations,
ZCT processing, and obsolete lifecycle state after tracing has replaced their
contracts. Repack the header in a later slice rather than coupling that cleanup
to first movement.

Capacity shrinking, more precise remembered entries, barrier specialization,
and further allocation optimizations follow measured needs. The documented
`Instance` shrinking policy preserves existing inline locations; it remains a
separate evacuation optimization.

## Lean Model and Proof Obligations

Model a finite heap graph with roots, strong and weak slots, generation/space
classification, forwarding, and pending scan work. Represent logical object
identity separately from physical addresses. State adapter assumptions
explicitly: complete roots and slots, valid layouts, stable source metadata,
exclusive collection access, and sufficient destination capacity for the
successful-collection theorem.

The initial proof targets are:

- reachable collected objects obtain unique destinations;
- sharing and cycles preserve their logical graph structure;
- strong closure leaves no surviving root or strong slot pointing into space
  that will be discarded;
- the write barrier preserves coverage of old-to-young references;
- evacuation terminates for a finite heap with sufficient destination capacity;
- weak slots are repaired or cleared only after strong closure;
- both scan queues reach closure before nursery reuse.

Build the model alongside the isolated collector rather than reconstructing it
after runtime integration. Compare executable model results with the C++ test
heap over generated small graphs and mutation sequences. State which C++
operations correspond to each model transition. A proof of the abstract model
is not a proof of the handwritten C++ implementation; layout tests, root tests,
runtime verifiers, and executable comparison address that remaining boundary.

## Implementation Discipline

Each slice starts from a concrete readiness sketch and finishes with a usable
runtime state. Preserve the existing collector until its replacement contract
has a tested consumer. Check C++ changes with the required debug build and
tests, and run release checks at the exact HEAD before pushing. Documentation
and Lean-only changes use their relevant content or proof checks instead of
unrelated VM rebuilds.

The first code slice implements stage 1: ordinary reference-span resolution
consumed by existing reclamation. Subsequent API details, header representation,
native rooting, failure behavior, and the lifetime cutover receive their own
bounded reviews. The staged direction is accepted; those material implementation
decisions are not silently settled by this plan.
