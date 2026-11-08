# Young-Only Strong Evacuation

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Implemented |
| Scope | Proof-carrying generational heaps and full/young strong evacuation |
| Owning layers | Lean owns the executable model and proofs; runners decode and capture heap snapshots |
| Validated against | Lean 4.35.0 and the standalone C++ Evacuation collector |
| Supersedes | N/A |

Stage one extends the executable strong collector with generational selection
and remembered-owner field repair. Raw generational heaps contain a decoded
strong graph, tagged original kinds and an ordered remembered-owner list.
The collector accepts a proof-carrying heap: remembered owners are distinct
and old, and every old-to-young strong edge has its owner remembered.
Conservative remembered entries are allowed, including in full collections.

Full collection selects all originals and adds no remembered roots. Young
collection selects young originals, repairs roots and the actual strong fields
of remembered owners, then drains selected canonical objects. Unselected old
objects remain unmarked and unforwarded. Pending copies may contain raw source
references; processed owner fields and scanned copy fields are repaired.

The generational driver reuses the existing proved copy-discovery primitive.
Its full specialization is proved equal to the existing strong driver, retaining
the full weak protocol and its proofs. Young liveness is selected reachability
from selected roots and remembered-owner edges; it includes children of old
owners that are unreachable from the current roots.

Heap format v2 adds required `remembered_strong` allocation IDs. Both runners
validate the same generational contract; Lean conversion constructs the
well-formedness proof. Input and output retain the supplied list, before reset
or source retirement. Young mode initially requires empty weak storage and
supports evacuation only.

Checked results establish selected reachability and termination, forwarding
only selected movable objects, unselected-object preservation, exact repaired
fields, retained-reference safety and the logical postcondition for minor
remembered-set reset. Physical full-collection remembered-set retirement,
barrier maintenance, young weak repair and repeated collections remain separate.


The implementation is split by responsibility:

| Module | Responsibility |
|---|---|
| `GenerationalHeap` | Tagged object kinds, raw/valid heaps and decidable coverage |
| `Generational` | Collection selection and executable collection |
| `GenerationalProofs` | Selection, discovery invariants, frozen unselected references and full equivalence |
| `GenerationalFields` | Tracking, pending-copy/processed-field plans and writeback |
| `GenerationalSchedule` | Root/remembered seeding, scan simulation and proved termination |
| `GenerationalCorrectness` | Selected reachability, identity, exact fields, preservation and disposal/reset results |
| `GenerationalExamples` / `GenerationalAudit` | Checked positive/negative examples and exact axiom guards |

`CollectionInput` accepts only `GenerationalHeap`, which embeds the complete
`WellFormed` proof. Its `graph` contains decoded nonnull references; wire
immediates and nulls remain in retained slot layouts. No new GC axioms or
unfinished proof terms are introduced. Full weak collection reuses the previous
model and proof graph. The format bridge, C++ bytes/layouts and runtime barrier
integration remain outside these abstract collector proofs.
