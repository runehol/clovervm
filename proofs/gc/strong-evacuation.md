# Complete Strong Evacuation

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Implemented |
| Scope | Finite-graph strong evacuation with all original objects selected, including NonMoving originals and destinations |
| Owning layers | Lean owns the executable model/proofs; C++ traversal, adapter, layouts and allocator retain their existing responsibilities |
| Validated against | Lean 4.35.0; C++ `Evacuation` at `a10a0b21` (2026-11-01) |
| Supersedes | N/A; extends the mark and copying-discovery proof slices |

This completes the abstract strong-copying argument. It does not complete the
GC proof program: [weak repair](weak-repair.md) is a separate implemented slice;
minor selection/barriers and C++ refinement remain work. Build all proof slices
with `lake build` in this directory.

Read [Evacuation.lean](CloverGC/Evacuation.lean), then
[EvacuationProofs.lean](CloverGC/EvacuationProofs.lean). Concrete cases are in
[EvacuationExamples.lean](CloverGC/EvacuationExamples.lean); public theorem
dependencies are guarded by [EvacuationAudit.lean](CloverGC/EvacuationAudit.lean).

## The Executable Algorithm

`EvacuationState` wraps `CopyState`, adds current strong fields in original
objects, and stores the repaired root list. The input heap is immutable and
remains the independent logical specification. Copy records contain their
current fields, not just their initial values.

1. `seedRoots` discovers every root and retains its repaired reference. Root
   ordering and duplicates are preserved.
2. `scanCopy` pops one canonical reference from the queue and reads its current
   modeled fields with `liveFields`. It does not substitute an oracle's graph
   traversal for the actual field reads.
3. `repairReferences` threads the latest copying state through the fields,
   discovering their targets and retaining returned canonical references.
4. `writeFields` updates that object's fields in the post-discovery state.
   Newly appended copies survive writeback. NonMoving originals are repaired
   in place; movable originals are never written.
5. `drainCopies` repeats the scan with a budget. `evacuate` supplies `n`, and
   the proof establishes that the queue is empty within that budget.

The executable function is total even on arbitrary malformed states: an invalid
copy lookup has an empty fallback and an out-of-range list modification has no
effect. The invariant proves those cases do not occur in executions seeded from
the source graph. This is not a runtime error-handling policy or permission for
the C++ adapter to accept invalid references.

## The Invariant Refinement

The discovery-only ledger originally required each whole copy record to equal
its initial copy. Traversal must change fields, so that field became
`copies_metadata`: origin and destination policy remain fixed, while field
correctness moves to `FieldsInvariant`. The previous forwarding/identity
theorems and fresh-copy theorem still hold; discovery still leaves all existing
records unchanged.

`fieldPlan` describes what an object's fields should currently contain:

- Before scanning, they equal its original fields as source references.
- After scanning, they equal its original logical targets mapped to canonical
  references, preserving slot order and multiplicity.

Movable originals always retain their raw fields. Only the copy is scanned and
repaired. This distinction permits destructive source-header forwarding in the
C++ architecture without pretending source payload references were repaired.

`Schedule` relates physical marks/queue entries to logical object IDs in the
already-proved mark algorithm. `EvacuationInvariant` bundles:

- The copying invariant and physical/logical schedule relationship.
- The mark algorithm's reachability and scanned-history invariant.
- Raw-versus-repaired field correctness for originals and copies.
- Root membership in logical marks and root references equal to canonical
  versions of the original root list.

`scan_evacuation` proves that one physical scan corresponds to one logical scan
and preserves the bundle. `drain_evacuation` lifts that relationship to arbitrary
fuel. Thus we reuse the first slice's proved closure and termination; we do not
assume that the copying queue drains or merely stop at zero and call it complete.

## A Mixed Cycle

```text
roots: A, A
A → A, C, B, B
B → C
C → A
D → A                 (D is unreachable)
```

A moves into NonMoving destination storage, B moves into ordinary destination
storage, and C is an already-NonMoving original. The result is:

```text
roots: A′, A′
A′ → A′, C, B′, B′
B′ → C
C  → A′
```

The shared queue scans A-prime, C and B-prime once each. D isn't copied or
scanned. Initial copy fields still contain old A/B references, so this example
would fail if we mistook discovery alone for complete evacuation. It also
catches writeback that accidentally drops B-prime's newly allocated record.

## The Final Results

| Theorem | Claim |
|---|---|
| `evacuate_complete` | The final pending queue is empty |
| `evacuate_reachable` | Marked canonical objects correspond exactly to strong reachability |
| `evacuate_roots` | Root order, duplicates and logical identities are preserved |
| `evacuate_children` | Every reachable object's fields are the original target list mapped to canonical references |
| `evacuate_forwarded_iff` | A source is forwarded exactly when it is reachable and movable |
| `evacuate_nonmoving` | NonMoving originals retain their original references |
| `evacuate_source_unchanged` | Movable originals retain their raw payload references |
| `evacuate_disposal_safe` | Surviving root/strong-field references name allocated copies or NonMoving originals |
| `evacuate_trace_nodup` / `evacuate_trace_correct` | Exactly the reachable canonical objects are scanned, with no duplicates |
| `evacuate_scans_once` / `evacuate_scan_bound` | Each reachable object is scanned once, with at most `n` scans |

The existing `canonical_injective` result together with root/field preservation
gives the logical graph embedding: distinct identities do not merge, repeated
references remain shared, and cycles remain cycles. Destination kind does not
change scan membership, so ordinary/stable placement cannot lose a child graph.

## What Source Disposal Means Here

`Canonical` means a reference names either an allocated destination record or a
NonMoving original. The disposal theorem establishes this for every repaired
root and every strong field of every reachable canonical object. Therefore
discarding movable original storage cannot invalidate those references.

It does not require stale fields in garbage or discarded source objects to be
repaired. The example's unreachable D can still point to old A. That data is not
part of the surviving graph. No sweep, native destruction or reclamation code
is modeled by the theorem.

The model still assumes a fixed finite graph, supplied complete logical edges,
exclusive collection access and successful allocations. It has no tagged values,
byte extents, native aliasing, RC traffic, weak units, minor-collection selection
or remembered-set/barrier proof. Complete roots and the C++ implementation's
refinement to these transitions remain explicit external obligations.
