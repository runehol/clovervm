# Generational Weak Resolution and Repair

| Field | Value |
|---|---|
| Document type | Design reference |
| Status | Accepted |
| Implementation | Owner-based remembered scanning, model proofs and both differential runners |
| Scope | One full or young collection with strong and weak fields |
| Owning layers | C++ collector owns owner scanning; Lean owns the model and proofs; runners own conversion and capture |
| Validated against | `src/gc/evacuation.h`, `src/gc/remembered_set.h`, Lean definitions and differential checks |
| Supersedes | Separate weak-unit remembered-set proposal |

## One remembered owner set

`RememberedSet` stores borrowed old owner addresses. Entries neither confer
liveness nor describe a strong or weak field. The store barrier records an old
owner when a stored pointer targets a young object. Header membership deduplicates
owners; conservative entries are allowed. Runtime store integration and physical
retirement remain separate from the collector template.

The JSON heap has one ordered `remembered` array of original allocation IDs.
There is no `remembered_weak` array. Version 2 is updated in place because all
consumers and fixtures belong to this internal testing format.

The Lean strong projection retains its `rememberedOwners` list. A proof-carrying
`WeakHeap` adds unit ownership and weak coverage against that same owner list;
it has no independent weak remembered state. Every old-to-young strong or weak
reference requires its owner remembered. Weak coverage includes both guards and
referenced payloads, even in inactive entries. Nulls and immediates add no edge.

## Collection schedule

External weak units are visited first, followed by strong roots. Young collection
then processes each remembered owner in list order: strong slots first, then all
weak units in that owner's storage. The owner itself is not marked, enqueued or
moved. Strong slots discover young targets; weak units only relocate or clear
references. Full collection ignores remembered membership as additional roots
and visits owned units when their objects are scanned.

Pending objects are drained in FIFO order, again visiting strong fields before
weak units. Deferred weak repair runs after strong closure. The owner list stays
in diagnostic output before any remembered-set retirement.

Unselected old references resolve unchanged before forwarding or marks are
consulted. Selected sources follow the existing full resolver. Early repair can
partially rewrite an entry before deferring it. Late dead guards or referenced
payloads reset the coordinated entry to null/null/immediate zero. Inactive
entries reset when visited; unvisited storage stays unchanged.

## Proof and capture boundaries

Erasing weak state preserves the exact strong result and scan trace. Registration
is external units, units of remembered owners in owner/unit order, then units of
logically scanned owners. These locations are disjoint and registered once.
Late visits equal the deferred queue, in order and without duplicates.

Local repair compatibility is lifted through a three-field semantic invariant:
unregistered units retain templates, registered units remain compatible with
final repair, and settled units already equal direct late repair. Owner scanning
and draining preserve it. Every registered result equals direct late repair of
its original template; unregistered units remain unchanged. Strong closure
precedes late repair and the final weak queue is empty. Exact axiom guards are
part of the default Lean build.

External units and units of retained owners satisfy the strong model's `Safe`
predicate. In the unregistered retained-owner case, coverage excludes young
targets from the unremembered old owner. Discarded owners' diagnostic contents
are outside this claim. Native storage separation and lifetimes remain adapter
obligations.

Both runners support young weak evacuation. Capture keeps stale weak templates
only in selected movable originals; unselected old originals emit their repaired
weak state. Full mark remains marking-only, retaining all cells and units. Host
CPython campaigns generate both strong and weak fields with combined coverage.

Repeated collections, runtime write-barrier integration, physical remembered-set
retirement/reclamation and deliberate mutation testing remain separate work.
