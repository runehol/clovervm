# GC Models and Proofs

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Implemented |
| Scope | Abstract strong closure and full-selection evacuation with early/late weak units; not C++ runtime verification |
| Owning layers | This Lean project owns the executable model and proofs; `src/gc/` owns the C++ collector |
| Validated against | Lean 4.35.0; C++ `StrongClosure` and `Evacuation` at `33cb9cf6` (2026-11-01) |
| Supersedes | N/A |

This is a separate Lean project for the proof slices described in the
[GC implementation plan](../../doc/generational-copying-gc-implementation-plan.md#lean-model-and-proof-obligations).
It does not change the VM or participate in its CMake build.

## Build and Read

From this directory, run:

```sh
lake build
```

`lean-toolchain` pins Lean to 4.35.0. Lake is Lean's build tool. The project uses
only the libraries bundled with that toolchain; there are no external package
dependencies. Generated output lives in ignored `.lake/` storage.

Read these files in order:

1. [Mark.lean](CloverGC/Mark.lean): the graph, independent reachability
   specification, and executable collector.
2. [Examples.lean](CloverGC/Examples.lean): small computations, including cycles,
   sharing, disconnected objects, and an insufficient scan budget.
3. [MarkProofs.lean](CloverGC/MarkProofs.lean): discovery lemmas, the invariant,
   preservation/termination, and the final correctness theorems.
4. [Audit.lean](CloverGC/Audit.lean): checked axiom dependencies of the public
   claims. It is imported by the default build, along with proofs and examples.

These first four files cover the mark phase. The next slice is described in
[Copying Discovery and Forwarding](copying-discovery.md), with separate model,
proof, example and audit modules. Everything is checked by the same build.

The final strong-copying slice is [Complete Strong Evacuation](strong-evacuation.md).
It adds live field rewriting, graph preservation, completion and abstract source-
disposal safety. [Weak Repair Units](weak-repair.md) then extends it with early
resolution, exact deferral and coordinated late repair. Minor filtering and
runtime integration remain outside this model.

### Proof Module Layout

The final correctness results remain in `MarkProofs`, `CopyDiscoveryProofs`,
`EvacuationProofs` and `WeakRepairProofs`. Their supporting facts are grouped by
responsibility, rather than by the implementation slice that first needed them:

| Module | Responsibility |
|---|---|
| [MarkedQueue](CloverGC/MarkedQueue.lean) | Shared marked/pending membership, duplicate-freedom and inclusion facts |
| [CopyProgress](CloverGC/CopyProgress.lean) | Monotonic strong marks and preserved forwarding across discovery/scan/drain |
| [EvacuationSchedule](CloverGC/EvacuationSchedule.lean) | Correspondence between physical and logical discovery/queues |
| [EvacuationFields](CloverGC/EvacuationFields.lean) | Field plans, metadata-preserving writes and repair invariants |
| [WeakResolutionProofs](CloverGC/WeakResolutionProofs.lean) | Resolver, field and unit repair facts, including final liveness |
| [WeakScheduling](CloverGC/WeakScheduling.lean) | Strong-state erasure, unit enumeration, registration and visit histories |

`MarkedQueue` is a generic proof view of two lists, not a replacement executable
collector or an additional runtime state. The mark/copy models retain their
existing definitions; their lemmas reuse the same list-level argument.

## What Lean Is Checking

A `def` defines a function we can execute. A `theorem` states a proposition and
supplies a proof that Lean checks. A proof beginning `by` uses tactics to build
that proof: `simp` simplifies expressions using known equalities, `grind`
combines logical facts, `omega` proves natural-number arithmetic, and
`induction` breaks a claim into base and recursive cases. These tactics must
produce proof terms accepted by Lean; they do not grant themselves permission
to assume the desired result.

For example, the main statement is:

```lean
theorem collect_correct (heap : Heap n) (roots : List (Fin n)) (x : Fin n) :
    x ∈ (collect heap roots).1.marked ↔ Reachable heap roots x
```

`Fin n` is an object ID smaller than `n`. `∈` means membership and `↔` means
"if and only if." The pair returned by `collect` contains the final state
(`.1`) and the scan trace (`.2`). The statement says that, for any heap, roots,
and object ID, the collector marks that object exactly when a strong path from
a root reaches it.

The definitions of `Reachable` do not mention the collector: roots are reachable,
and strong children of reachable objects are reachable. This avoids defining
correctness circularly in terms of the algorithm's output.

## One Run

```text
roots: A, A             D → B
       A → B → C
       ↑       │
       └───────┘
```

The first root marks A and queues it. The duplicate root does nothing. Scanning
A discovers B; scanning B discovers C; scanning C finds that A is already marked.
The queue is now empty. D is neither marked nor scanned: pointing into the live
graph does not make D reachable from the roots.

The model maps A, B, C, D to IDs 0, 1, 2, 3. The FIFO trace is `[0, 1, 2]`.
Marks are prepended to their list, so the final marked list is `[2, 1, 0]`;
its order has no semantic significance. Tests using `by decide` check specific
computations; the general theorems cover all finite heaps, not only those tests.

## Invariant and Results

The invariant ties the algorithm's state to its already-scanned history:

- Marked, pending, and scanned lists each contain no duplicates.
- Pending and scanned objects are disjoint.
- An object is marked exactly when it is pending or already scanned.
- Every marked object is reachable.
- Every already-scanned object's strong children are marked.

The scanned history is not consulted by the collector. `drain` records its
actual queue pops as audit output; the proof uses this to establish exactly-once
scanning. This is not an additional C++ mark set or a requested runtime feature.

Discovery and scanning preserve the invariant. Because the heap has `n` objects,
the disjoint pending/scanned lists together have length at most `n`. Each scan
adds a new object to the history, so `n` scan attempts suffice. `drain` is
structurally recursive on a budget, and `collect_complete` proves the budget
does not run out with work pending. Termination is not inferred from merely
returning when a counter reaches zero.

| Theorem | Claim |
|---|---|
| `collect_complete` | The final pending queue is empty |
| `collect_correct` | Marking equals independent strong reachability |
| `collect_trace_nodup` | No object appears twice in the scan trace |
| `collect_scans_once` | Every reachable object appears in the trace exactly once |
| `collect_skips_unreachable` | Every unreachable object appears zero times |
| `collect_scan_bound` | The scan trace contains at most `n` objects |

The project treats warnings as errors, so unfinished proofs are not accepted.
The audit additionally guards the transitive axiom output for the public
theorems: only Lean's standard `propext`, `Classical.choice`, and `Quot.sound`
appear. No custom GC axiom or unfinished-proof axiom is accepted. Standard
logical axioms are not assumptions about the collector's behavior.

## Correspondence and Limits

| Lean model | C++ counterpart in `src/gc/strong_closure.h` |
|---|---|
| `discover` membership check and mark insertion | Adapter `try_mark` |
| Append a newly marked object to `pending` | `pending_.push_back` |
| `seed` | Calls to `add_root` with a fresh adapter |
| `scanOne` | Pop front, enumerate strong slots, discover decoded children |
| `drain` | The C++ draining loop; Lean additionally records a trace and uses a proved budget |

Lists are a simple model of membership and FIFO order, not a proposal to replace
the C++ hash set or deque. The model does not assume a specific marking or queue
representation. It allows duplicate roots, duplicate slots, cycles and sharing.

The theorem covers arbitrary `n`, including zero, with a fixed graph during the
phase and fresh marks. Object IDs ensure all modeled references are valid.
The supplied roots are the starting specification; this does not prove they
cover every live interpreter/native value. The model starts after slot decoding:
nulls and immediate values do not contribute graph edges. It assumes the adapter
faithfully supplies all strong references, exact test-and-set marking, and a
FIFO queue. It models successful operations only, not resource exhaustion.

There is no copying, forwarding, weak repair, generation filtering, barrier,
reclamation, raw-pointer arithmetic, RC ownership, or C++ memory-model proof in
the mark-phase slice. In particular it does not establish production root completeness,
layout safety, or a formal refinement from C++ to Lean. The C++ tests remain
necessary. The separate copying-discovery slice establishes local forwarding
properties; the strong-evacuation slice proves the complete abstract strong
traversal. Executable differential comparison against the C++ test heap,
minor-collection filtering and runtime ownership proofs remain later work.
