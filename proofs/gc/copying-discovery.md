# Copying Discovery and Forwarding

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Implemented |
| Scope | Local copying discovery and forwarding with all sources selected; no scan/repair traversal |
| Owning layers | Lean owns the model/proofs; the C++ adapter owns physical copying/placement and `Evacuation` owns traversal |
| Validated against | Lean 4.35.0; C++ `Evacuation::discover_slot` at `33cb9cf6` (2026-11-01) |
| Supersedes | N/A |

This is the second [GC proof slice](README.md). Run `lake build` from this
directory to check both slices. It introduces no runtime changes.

Read [CopyDiscovery.lean](CloverGC/CopyDiscovery.lean) for the executable model,
[CopyDiscoveryProofs.lean](CloverGC/CopyDiscoveryProofs.lean) for the proofs,
[CopyExamples.lean](CloverGC/CopyExamples.lean) for concrete runs, and
[CopyAudit.lean](CloverGC/CopyAudit.lean) for the guarded theorem dependencies.

## The Additional Problem

Marking only records that an object has been seen. Copying must also remember
where it went. Two encounters with A must return the same A-prime, rather than
creating two distinct objects and losing sharing or identity.

`Reference.source object` names an original logical object (`Fin n`).
`Reference.copied index` names a separately allocated destination (`Nat`). The
first allocation has index 0 regardless of which source was discovered first.
Both ordinary and NonMoving destination records use this one index namespace;
their placement is recorded separately. This distinguishes source identity
from location without modeling byte addresses or separate allocators.

`CopyInput` fixes the original strong-reference graph, source movability, and
destination placement for the phase. All original objects participate in this
slice's collection; there is no minor-collection selection predicate yet.
Source movability and destination kind are independent: a movable original may
be copied into NonMoving storage. An already-NonMoving original stays in place.

`CopyState` contains a forwarding function, allocated copy records, marks, and
the pending queue. Records contain origin identity, placement, and strong fields.
The model does not represent arbitrary payload bytes or prove memcpy safety.

## Discovery

`discoverReference` returns an updated state and a repaired reference:

1. For an unforwarded movable source, append a fresh copy record, install its
   forwarding index, then mark/enqueue the copy before returning it.
2. For a forwarded source, reuse its index and mark/enqueue only if necessary.
3. For a NonMoving original, mark/enqueue that original without allocating.
4. For an allocated copy reference, return it unchanged. The invariant already
   guarantees allocated copies are marked. This matches the observable no-op
   in C++ for both fresh ordinary destinations and already-marked stable copies.

The helper `installCopy` exposes the intermediate allocation/forwarding update
as a definition so it can be reasoned about. It is not a standalone operation
that preserves the full invariant: only completed discovery guarantees the new
copy is marked. The caller sees the returned reference after that marking.

No children are discovered here. A fresh record preserves the original fields
as source references. For A pointing to itself, A-prime initially points to the
old A, not A-prime. The later scan must repair this; no source-disposal theorem
is claimed by this slice.

Discovery itself is not recursive over the graph, so this slice needs no fuel
parameter. Copying an edge list is different from following those edges.

## What the Invariant Requires

The `Ledger` proof bundle says:

- Every forwarding entry names an allocated record with the correct origin.
- Every allocated record is the forwarding destination of its origin.
- Every record came from a movable source and retains its selected destination
  kind. Child fields are deliberately not frozen by the ledger: the fresh-copy
  theorem proves their initial contents; traversal has a separate field invariant.

`CopyInvariant` adds canonical marks, marked allocated copies, duplicate-free
mark/queue lists, and the requirement that pending references are marked.
Pending objects may include copies in either destination kind and unmoved
NonMoving originals, all in the same FIFO queue. The invariant does not require
every mark to remain pending, so rediscovery also applies to already-scanned
objects whose marks remain but which are no longer queued.

`empty_invariant` proves the empty state satisfies these properties.
`discovery_preserves` proves a discovery preserves them. The traversal slice
refined the original `copies_faithful` ledger field into `copies_metadata`, so
repairing children can change reference representations without changing
origin or placement. Discovery itself still preserves existing records exactly.
This is an executable
state transition with proved preservation, not an axiom asserting that a copy
or forwarding operation is correct.

`ValidReference` is an input precondition. Sources are in range by construction;
a copied reference must name an existing record. Invalid copied indices are
expressible, and the executable function simply echoes them. The theorems about
marked canonical outputs deliberately do not apply to those inputs. No runtime
validation is claimed or proposed here.

## Public Results

| Theorem | Claim |
|---|---|
| `empty_invariant` / `discovery_preserves` | The invariant starts true and survives each valid discovery |
| `discovery_source_represents` | The returned source-discovery result denotes that logical original |
| `discovery_returned_marked` / `discovery_returned_canonical` | A valid returned reference is already marked and canonical |
| `discovery_fresh_copy` | A new copy uses the old allocation count as a fresh index and installs forwarding there |
| `discovery_nonmoving` | A NonMoving original is returned unchanged without altering allocations or forwarding |
| `discovery_preserves_copy` / `discovery_preserves_forwarding` | Existing records and forwarding destinations are never overwritten |
| `no_source_merging` | One canonical physical reference cannot represent distinct sources |
| `unique_representation` | One source has at most one canonical representation in a valid state |
| `source_rediscovery` | Repeating the source encounter changes neither state nor returned reference |
| `returned_rediscovery` | Discovering the repaired reference is also a complete no-op |

The faithful-copy, queue uniqueness and marked-before-return properties are
established by the invariant-preservation and output proofs. No fresh-copy
assumption is smuggled into the allocator: it explicitly appends a record at
the old list length, and the proofs show old slots survive and that index was
previously unallocated. Destination placement cannot bypass queue membership.

The default build guards public theorem dependencies against unfinished proofs
and custom GC axioms, allowing only the standard Lean logical axioms. The
concrete examples deliberately allocate source 1 before source 0, cover both
destination kinds and a NonMoving original, preserve self/duplicate child edges,
and repeat discovery through both original and repaired references, including
with an empty pending queue and persistent marks. They also derive successive
state invariants from the empty-state theorem.

## Next Boundary

The [strong-evacuation slice](strong-evacuation.md) now proves scanning/repair,
graph preservation, termination and abstract source-disposal safety. Those
claims use this slice's discovery mechanism but are not results of discovery
alone. [Weak repair](weak-repair.md) now has a separate protocol proof; minor
filtering and C++ refinement remain unproved. The
immutable input graph, valid roots/slots, successful resource acquisition and
exclusive collection remain model boundaries, not verified CloverVM runtime
properties.
