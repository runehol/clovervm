# Generational Copying GC Implementation Plan

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Accepted |
| Implementation | Partial |
| Scope | Incremental transition from deferred refcounting to tracing and moving collection, including scanning, rewriting, barriers, and a collector model for Lean proofs |
| Owning layers | Memory manager owns collection; runtime layouts and root publication supply precise mutable references; mutator store paths own barriers |
| Validated against | Weak-unit migration working tree based on `649dca5b`; further frame-state work paused (2026-10-31) |
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
    drain one copying work queue for ordinary and stable destinations
    resolve settled weak units early; finish deferred units after strong closure
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

The template interface and concrete test-heap/CloverVM sketches are in
[Isolated Collector and Heap Adapter](gc-collector-heap-adapter.md). The initial
slice is non-moving strong closure over explicit roots; it is implemented in
`src/gc/` in the current working tree, with both synthetic and real-object tests.
It does not depend on completing interpreter frame maps and does not enable
production tracing or evacuation.

The collector model and the production algorithm should expose corresponding
state transitions. Lean proofs establish properties under explicit adapter
assumptions; they do not by themselves verify the C++ adapter.

### Byte-Copy Direction and Immediate Sequence

Real-object evacuation uses generic `std::memcpy`, not C++ copy construction or
per-native-layout relocation constructors. Query source layout, reserve its
allocated extent, and copy its copy extent; then prepare destination state,
publish forwarding, and repair traced references. The existing per-layout
checklist covers self/interior pointers and other address-dependent state.

Current RC-backed member wrappers temporarily prevent the C++20 implicit-lifetime
guarantee for some copied records. This limitation is explicitly accepted for
the memcpy implementation, not permission to break RC ownership. Replace those
representations and enforce compile-time trivial-copyability checks for admitted
movable records at the RC-wrapper cutover, rather than building a temporary
relocation-constructor system.

The next preparatory slices are **5a: generic byte copying tested on exact
Float**, **5b: dynamic extents and copied-reference repair**, and **5c: admission
and real-adapter readiness**, detailed in the
[copying-adapter sequence](gc-copying-adapter-readiness.md#follow-up-slices-proposed-for-review).
These labels are local to that investigation, not implementation-plan stage
numbers. Copies stay test-owned and unpublished; sources remain unchanged.
The classification-only part of 5c now has `gc::EvacuationSpaces`: a snapshot
of selected HeapSpace mapping ranges with forwarding-safe generation/NonMoving
checks. It distinguishes fresh destinations from selected sources without
object enumeration or per-allocation bookkeeping. This does not wire evacuation
into CloverHeapAdapter or implement full-collection stable-object liveness.
The isolated test heap separately distinguishes collection participation
(`is_collected`) from mode-independent movability (`is_movable`), and existing
NonMoving objects from fresh destinations.
The isolated Evacuation phase consumes both predicates, marks canonical copies
and existing NonMoving objects in a single work queue, and resolves weak targets
through forwarding or marks. This does not supply runtime immortal policy,
stable sweeping, or ownership transfer.
The bounded routing groundwork is documented in
[collection routing and space lifetime](gc-copying-adapter-readiness.md#collection-routing-and-space-lifetime):
minor collections append into existing old/NonMoving spaces; full moving
collections use a separate ordinary destination while retaining NonMoving
storage. Raw-space tests establish separation and lifetime only, not production
retirement or a HeapSpace ownership-swap API.
Test-only real-layout integration now composes the collector with actual
OverflowSlots headers, reference slots, mapping containment, destination copying,
and forwarding. It covers young/full routing, explicit remembered slots, mixed
cycles, external weak roots, and retained graph validity after raw input storage
is released. These fixtures never register objects with RC or activate runtime
collection; see the
[integration test boundary](gc-copying-adapter-readiness.md#real-layout-evacuation-integration-tests).
Production activation still follows stages 4--9 below: precise roots/native
references, barriers, tracing verification, tracing lifetime authority, then
restricted movement. Byte-copy groundwork can proceed alongside integration;
the paused frame-state design stays paused.

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
  `WeakResolver` returning Resolved/Pending/Dead and an optional descriptor
  `visit_weak_units` callback behind
  `visit_weak_reference_units(object, visitor)`. The initial owner-wide,
  late-only interface has been replaced by per-unit enumeration and callbacks.
  Early repair preserves unresolved references; after strong closure the
  resolver reports an unforwarded source as dead, without marking or
  dereferencing dead inputs. Shape exposes its back-link as one unit.
  CodeObject exposes each IC entry as a unit and repairs all raw cache
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
  prerequisites. Old-backing count zeroing is RC-only bookkeeping scheduled
  for post-cutover removal, not a tracing/copying prerequisite.

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
its queries and consumers in bounded slices. Report object extent, copy
extent, and strong slots separately; retain the shape-derived `Instance`
capacity and auxiliary initialized prefix.

The first unification step is implemented: `native_layout_descriptor_for(id)`
returns one static record containing the existing release and object-size parts.
Reclamation, scanning, and size-query consumers use it; the separate tables and
lookup APIs are removed. `NativeLayoutInfo` adds non-optional
`allocated_size` and `copy_size`, plus an optional ordinary strong span with a
64-bit count. Raw backings copy their full byte extent without pointer scanning;
lazy instances copy their initialized prefix. Empty trailing placeholders remain
reserved but are excluded from the copy prefix. Custom scanners stay separate.
This query does not enable movement, and the compact formula migration remains
subsequent work.

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
  functions, modules, shapes, and entry adapters through the non-const
  `VirtualMachine::visit_persistent_root_slots(StrongSlotVisitor)`. Include the
  actual `OperatorStep::dunder_name` cells, not native table-storage pointers.
  Visit initialized null/inline cells and every alias without RC operations.
  Assemble thread and VM roots once in `VirtualMachine::run_heap_reclamation`;
  the memory-layer `run_heap_reclamation(threads, roots)` consumes that explicit
  root set. Keep intern tables, pointer-keyed maps, and JIT execution internals
  out of this slice. Audit managed VM fields against the visitor for
  completeness. Tests check cache presence/reuse, restored getter-visible
  rewrites, untouched dispatch metadata, and production reclamation liveness.
  Do not reconstruct a complete pointer inventory or assert visitation order,
  counts, backing capacities, or offsets: logical views can share cells, and
  duplicate visits need not affect observable correctness.
- **4b.3: Read-only builtin-class roots.** Add
  `VirtualMachine::for_each_builtin_class(visitor) const`, walking values in
  `builtin_classes_by_name_` without changing entries or keys. Its names and
  classes are immortal and non-moving: return class pointers by value, not
  writable map cells. Full tracing seeds those classes alongside existing
  VM/thread roots, then follows their outgoing cells through
  `visit_strong_reference_slots` with normal visited-object tracking. Reachable
  immortal owners do not move, but their references to movable children can be
  repaired. Do not enumerate all immortal-heap allocations as implicit roots.

  Skip the interned-string table: interned strings have no unique movable
  children, and their non-moving shape/class are accessible from the VM.
  No string/map relocation, rehashing, heap-wide walk, or new lifetime policy.
  Expose the builtin-class root walk and test-only graph traversal; do not add
  a production tracer or change current RC reclamation. Verify canonical roots
  survive public builtin rebinding/deletion, traversal handles cycles and
  reaches child objects, and restored child-cell rewrites leave class/name
  identities and map lookups unchanged. Nursery collections use remembered
  owners; initialization and mutation barriers remain in stage 5.
- **4c: Published interpreter state (implemented).**
  `ThreadState::visit_published_root_slots` exposes the initialized accumulator
  and active-code cells, not the conservative stack range or native frontier.
  Current reclamation consumes this visitor read-only. The live-accumulator
  interpreter safepoint slow path assigns the updated accumulator returned by
  `handle_safepoint` before dispatch resumes; the dead-accumulator path publishes
  `not_present()` and discards the return value. `NativeHandleRootRegion`
  registers its saved accumulator and active-code cells with `ScopedRoot`,
  so nested reentry restores updated values. Code objects remain non-moving
  but still require tracing; no PC rebasing is introduced. Controlled rewrite
  tests cover resumed execution,
  nested restoration, and RC-neutral writes. AArch64 `Uninitialized` emission
  now writes `not_present()` so recovery cannot publish stale register bits.
  No evacuation or new safepoints.
- **4d: Precise frame roots.** Distinguish parameters, locals, temporaries,
  saved code objects, and continuation operands from native PCs, frame pointers,
  padding, and scalar metadata.
  Further exceptional-flow clearing and frame-map implementation are paused
  pending a frame-state design: semantic binding, initialized storage, temporary
  liveness, before/after-instruction and suspended-call positions, handler entry,
  and the separate needs of GC, traceback/frame observation, and recovery.
  The existing normal/loop prologue clears are bounded initialization changes,
  not evidence that precise scanning is designed or complete. The remaining
  slices below are provisional work breakdowns, not implementation authority.
  - **4d.1: Publish exact frame position (implemented).**
    `SafepointScanRecord` carries the frame pointer and resume bytecode PC,
    alongside the existing stack bound and root cells. Active interpreter
    safepoints publish the actual current frame and PC, including the restored
    caller at continuation safepoints. An absent interpreter position is
    represented by null frame/PC fields. Named `idle(lowest)` and
    `interpreter(lowest, fp, pc, code, accumulator_or_not_present)` factories
    distinguish construction of these states; the interpreter factory requires
    a code-object reference and an explicit accumulator value. Publication
    takes a complete record. `NativeHandleRootRegion` saves/restores that record
    while registering its
    saved accumulator and code cells as before. Reserving native handle storage
    changes only the lower bound, not the saved frame position. Position fields
    are native metadata, not root cells; the current conservative scan and
    mutable-root visitors are unchanged. This supplies context for the later
    frame walker, not complete frame-root coverage or a claim that a completed
    interpreter activation remains live after returning to native code.
  - **4d.2: Codegen establishes ordinary-slot safety; analysis describes it.**
    Extend the existing `BindingInfo::needs_entry_clear` decision in scope
    analysis and continue emitting clears through the existing function
    prologue. Do not add blanket frame initialization, runtime initialization
    bitmaps, edge-clear insertion, or a bytecode repair pass. Additional clears
    are selective, once per invocation for affected locals, not per store or
    loop iteration. Parameters and globals must not be cleared.
    - **4d.2a: Reachable normal joins.** Use the existing
      `Presence` and `may_be_entry_value` facts. A non-parameter function local
      that may be bound after a reachable normal join, but may still contain
      untouched entry storage, requests `needs_entry_clear`. Add only the
      internal reachability bookkeeping needed to keep unreachable annotation
      paths from requesting GC-driven clears. Existing source-level unbound
      checks remain independent and continue to request their own clears.
      Normalize normal joins conservatively, without coupling this decision
      to the current interpreter/JIT polling locations. This may clear a local
      that is overwritten again before an actual poll; minimizing such clears
      is not part of the first slice. Do not claim loop/exception coverage or
      enable precise frame scanning yet.
      Implemented with structural codegen checks and interpreter regressions;
      before/after runtime measurements are recorded in
      [the selective-entry-clear performance report](gc-local-entry-clears-performance.md).
    - **4d.2b.1: Loop summaries.** Extend the same codegen decision to loop
      headers/backedges, zero-iteration exits, break/continue, and loop `else`.
      Use a GC-only copy of the entry flow state with the existing conservative
      local-touch summary; include assignments in a repeated `while` condition
      and in a `for` target as well as its body. Do not change source-level
      presence facts. Normalize joins between break exits and the normal/else
      exit with the same entry-clear request as other normal joins. All clears
      remain in the function prologue, never on a backedge or in the loop body.
      Touch summaries can conservatively request redundant clears; this is not
      a minimal-clear analysis. Verify structural placement, semantics, and
      before/after runtime cost before claiming loop-slot coverage.
      Implemented and verified in the working tree; see the
      [loop-entry-clear performance report](gc-loop-entry-clears-performance.md)
      for the full comparison and unresolved performance outlier.
    - **4d.2b.2: Exceptional-flow summaries.** Extend the decision to handlers,
      finally, and suppressed exceptions, accounting for partial execution
      before an exception. Never clear live bindings at handler entry; any
      required normalization remains a safe prologue clear. Separate readiness
      review; not implemented by the loop slice.
    - **4d.2c: Read-only map derivation and verification.** Analyze emitted
      bytecode, including handwritten builders, to derive ordinary-frame root
      selections. Retain potentially bound locals even when bytecode-dead;
      wholly unbound locals can be omitted without inspecting untouched
      storage. Mixed bound/untouched states at GC observation points identify
      an incomplete producer contract, not permission to rewrite bytecode or
      drop a possible reference. Fix the producing codegen/builder instead.
      A deleted local contains a valid sentinel, not an object reference.
      Temporary liveness and instruction/exception write ordering require their
      own audit before these results can drive root enumeration.
  - **4d.3: Suspended calls and continuations.** Identify live caller state and
    binary, ternary, and truthiness prefix operands, excluding dispatch metadata.
  - **4d.4: Assemble the frame walk.** Cover native-entry boundaries and connect
    precise frame enumeration to reclamation without dropping coverage for
    native/JIT state that still awaits conversion.
- **4e: Native handles and callers.** Migrate safepoint-crossing native locals
  and settle handle-cell address stability, overflow chunks, and interior
  pointers.
- **4f: JIT boundaries and closure audit.** Cover supported JIT safepoints and
  test complete root coverage. Preserve transition execution's no-safepoint
  contract; do not introduce loop-backedge safepoints in this work.

Each remaining slice requires its own readiness review. Slice 4a does not enable
movement or remove conservative root discovery.

### 5. Introduce Barriers Alongside Refcounting

Add generation and remembered state without removing refcount, lifecycle, or
native-layout state. Keep the total header eight bytes: state byte, 8-bit native
ID, 16-bit aux count, and 32-bit RC. The accepted next state encoding uses bits
0--1 for generation, bit 2 for per-object NonMoving, bits 3--4 as the pointer-tag
forwarding discriminator (zero normally), bit 5 spare, and bits 6--7 for normal
RC lifecycle. Preserve normal fields through accessor-based updates. The accepted
eight-byte transition and final eight-byte
target are specified in [GC Object Header](gc-object-header.md). Implement the
transition's byte-state/encoded-pointer representation without enabling production
forwarding, since a forwarding store destroys RC too. Migrate to the wider
final native-ID/aux fields only in an explicit later layout cutover. Generation
is retained in the source forwarding word. Audit moved RC/native-ID/aux offsets and
measure performance even though total header size is unchanged.

The representation is implemented with standalone `HeapHeader` storage contained
by `HeapObject`. Pointer-tag detection, target decoding (`~0x03`), one-way
NonMoving accessors/delegates, and encoding tests are implemented. Initial
construction policy is implemented: callers supply an explicit
`InitialMobilityPolicy` template argument through the heap APIs to shared
construction. Debug checks storage agreement; Release has no runtime policy
lookup or branch. Construction sets the bit before valid-object publication for
interned storage and layouts declaring `native_destroy`. Ordinary managed layouts remain
movable; stack construction and raw collector copies are outside that path.
Keep every accessor fully inline and header forwarding
reads/writes within HeapHeader. Do not add per-layout relocation constructors.

NonMoving is independent of generation and can change from clear to set only
through a one-way stable-placement transition. A movable string/backing may be
copied into stable storage before a raw address is exposed; the source remains
a movable forwarding source. Do not pin a nursery source in place by merely
setting its bit. Full pinning bits/counts are deferred until reference mapping
is in place. These header slices do not enable promotion, production forwarding,
nursery routing, or barriers. The detailed slices and verification are in
[the header contract](gc-object-header.md#code-migration-slices).

Introduce object-level remembered sets and barrier entry points following the
GC design's hot-path lessons. Cover ordinary members, raw pointer stores,
backing arrays, bulk copies, runtime caches, and relevant generated stores.
Distinguish construction into young storage from construction into old or
stable storage. Retain current incref/decref behavior throughout.

The standalone strong remembered-set primitive is implemented with reusable
4 KiB linked blocks and header-based deduplication. Its post-store recording and
minor-reset contract are exercised on test-owned headers and through real-layout
evacuation, including conservative overwritten records and repeated collections.
No runtime stores or ThreadState buffers are wired. Owner lifetime/invalidation,
thread coordination/publication, weak remembered units, and full-collection
retirement remain separate integration gates.
The standalone `adopt_from(StrongRememberedSet &&)` handoff splices active
blocks in constant time, preserving entry addresses and header membership.
Sealed counts allow partial intermediate blocks. The receiver keeps its active
prefix and takes the entire source chain; the source becomes empty and receives
the receiver's displaced spare cache. Boundary/cache-exchange/destruction tests, resealing
after refill, and repeated real-layout minor collections exercise the transfer.
ThreadState adoption is not wired, and this does not provide RC invalidation or
full-collection cleanup.

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

Check reachable-slot validity, copy extents, root publication, and
remembered-set coverage. Do not treat every positive refcount as a tracing root:
that would hide missing root declarations. Tracing may find unreachable cycles
that refcounting retains; that difference is expected.

Completion: normal execution and forced-safepoint tests exercise this verifier
without changing addresses or lifetime authority. Scanning completeness still
requires the explicit layout/root audits; a successful traversal alone cannot
prove it.

### 7. Implement Evacuation Against a Test Heap

Implement forwarding, growable slab-backed destinations, initialized-state
copying, root repair, one destination-independent work queue, and weak processing
through the agreed collector interface. Preserve readable layout
metadata when a source object or its shape has already been forwarded.

Test cycles, shared children, duplicate roots, old-to-young references, and work
whose edges alternate between ordinary and stable destinations. Each object is
copied, forwarded, and enqueued independently. Overflow backing
storage is discovered through an ordinary strong slot; owner/backing adjacency
is a deferred optimization, not part of the collector interface. No bitmap
walk or Cheney/slab scan cursor is required. Collector-only allocation during
enumeration must preserve visited storage and must not reenter the mutator/GC.
Destination and work-queue allocation failure are fatal; there is no rollback,
recoverable allocation result, or resumption of a partially rewritten heap.
Removing current allocation bitmaps remains separate allocator integration work.

Implement in bounded slices: test-heap single-object copy/forwarding primitives,
shared copying traversal, isolated weak units/source disposal, then CloverVM
weak-unit enumeration. The [weak repair unit design](gc-weak-repair-units.md)
defines early resolution, exact-unit deferral, and coordinated entry/payload
invalidation; it does not yet implement production weak-remembered coverage.
Concrete boundaries
and the first-slice review are in the
[adapter design](gc-collector-heap-adapter.md#evacuation-implementation-stages).
Production copying-adapter readiness follows separately; these slices do not
change production object-header layouts, frame roots, barriers, or lifetime
authority.

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

Before a layout can move, establish its slot completeness, copy extent,
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

Purge RC-specific array-backing retirement once tracing is authoritative and no
supported RC reclamation path needs it. In particular, remove the
`set_value_cell_count(0)` writes to old backings in `ValueArray::reserve()` and
`HeapPtrArray::reserve()`. The front array exclusively owns its backing: growth
copies live elements into a new backing and replaces that reference; the old
backing then becomes unreachable garbage. Do not clear its stale references or
normalize its element count for tracing, which does not scan unreachable
backings. Retain the current zeroing while RC remains active to prevent double
release. This cleanup does not remove layout/count metadata needed to
scan or copy the live backing safely.

Capacity shrinking, more precise remembered entries, barrier specialization,
and further allocation optimizations follow measured needs. The documented
`Instance` shrinking policy preserves existing inline locations; it remains a
separate evacuation optimization.

## Lean Model and Proof Obligations

Model a finite heap graph with roots, strong and weak slots, generation/space
classification, forwarding, and pending scan work. Represent logical object
identity separately from physical addresses. State adapter assumptions
explicitly: complete roots and slots, valid layouts, stable source metadata,
exclusive collection access, and successful resource acquisition for the
successful-collection theorem. Allocation failure is a separate terminal outcome,
not a resumable partially evacuated state or a whole-heap preallocation promise.

The initial proof targets are:

- reachable collected objects obtain unique destinations;
- sharing and cycles preserve their logical graph structure;
- strong closure leaves no surviving root or strong slot pointing into space
  that will be discarded;
- the write barrier preserves coverage of old-to-young references;
- evacuation terminates for a finite heap with sufficient destination capacity;
- weak slots are repaired or cleared only after strong closure;
- all ordinary and stable destinations reach closure through the unified work
  queue before nursery reuse.

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
