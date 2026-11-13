# GC Models and Proofs

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Implemented |
| Scope | Abstract strong closure, full weak evacuation, and generational strong evacuation; not C++ runtime verification |
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

The file-based differential harness uses
[heap JSON format v2](differential-format.md), unchanged before and after
collection, with string object identities, integer allocation IDs and tagged
object states. Algorithm and collection selection are separate run configuration.
The document defines the projection to existing C++ and Lean representations;
the C++ typed format reader, validator and writer are implemented in
`tests/gc_differential/heap_format.{h,cpp}` and tested against the documented snapshots.
The C++ native execution library in `tests/gc_differential/heap_runner.{h,cpp}`
constructs fresh synthetic heaps, runs full mark/full evacuation/young strong evacuation, and captures
checked snapshots before source reclamation. Directed tests cover the documented
results, mixed destinations and invalid native states. The C++ file runner is
implemented. The Lean format, collection and file CLI are also implemented;
the host-CPython comparator and bounded Hypothesis campaigns run both file
interfaces. Shrinking remains pending.

### C++ File Runner

From the repository root:

```sh
ninja -C build-debug gc_cpp
build-debug/tests/gc_differential/gc_cpp --algorithm evacuate --collection full \
  tests/gc_differential/fixtures/before.json > cpp-after.json
```

Use `--algorithm mark` for marking without movement or weak repair, and `--help`
for usage. Both algorithms retain all original allocations in the output.
Young collection supports evacuation with strong and weak storage. Full
collection supports both algorithms and weak storage. The runner reads one file; stdout carries the
result heap and stderr carries errors. No VM initialization is required.

Run the CLI process checks directly with either command:

```sh
ninja -C build-debug check_gc_cpp
ctest --test-dir build-debug -R '^GCHeapCLI$' --output-on-failure
```

The same checks run under `check`, `check-all`, and CTest's `regular`/`full`
labels. They compare the reviewed mark/evacuation fixtures as JSON, preserve
array ordering, and exercise argument, file and validation errors.

### Lean Heap Format

`CloverGC/Differential/HeapFormat.lean` uses bundled `Lean.Json` parsing and
printing for the generated JSON v2 files. It checks supported tags, required
values, integer bounds and heap relationships before collection. Validation
orders allocation records once and checks that each allocation ID equals its
index. The wire types are separate from the collector model's logical/physical
references.

`HeapFormat.WeakRememberedCovered` expresses owned weak coverage by allocation ID,
independently of object-array order. Validation uses a proof-producing decidable
check covering every stored guard and payload, including inactive entries.
`validateHeap_weak_covered` and `prepareFreshHeap_weak_covered` certify the public
validation boundaries; `HeapAudit` guards their axiom dependencies.

`CloverGC/Differential/HeapRunner.lean` converts fresh snapshots into the existing
mark, full weak-evacuation and generational weak-evacuation models, and captures the
result in the same heap schema. It retains immediate/null slot positions and
original physical storage, maps logical identities through the explicit
allocation pairing, and assigns copy IDs from the model's allocation ledger.
Capture checks completion, reference cardinality, copy marks, weak kinds and
the resulting heap relationships. It emits observed references without following
forwarding. `HeapRunnerProofs` proves complete snapshot projection for successful captures;
`HeapCollectionCompleteness` composes these contracts with actual collection and capture success for valid inputs.

The adapter follows the same implementation/proof split as the collector:
`HeapFormat.lean` and `HeapRunner.lean` contain executable definitions;
`HeapFormatProofs.lean` and `HeapRunnerProofs.lean` contain their correctness
claims and projection specifications. `HeapAudit.lean` imports the complete adapter
proof chain and audits the public boundaries plus independent capture claims.
Axiom checks follow proof dependencies transitively; helper lemmas do not need
separate audit modules. The collector's core audits remain separate.
The default `lake build` checks both layers. For adapter mutation checks, build
`CloverGC.Differential.HeapFormatProofs` and
`CloverGC.Differential.HeapRunnerProofs` explicitly; an implementation-only
build does not check those theorems.

The pure `encodeReference` and `encodeCell` specifications describe physical
reference and tagged-cell projection independently of the fallible capture code.
`allocationOfReference_correct` and `captureCell_correct` prove success exactly
when referenced copies are allocated, with the specified output. Source references
retain their original allocation ID; immediates retain their value and tag, and
null reference cells remain distinct from immediate zero. `captureCell_succeeds`
provides the local success theorem for valid cells. These claims are axiom-audited.

`CellsProjection` specifies ordered root and strong-slot projection: immediate
and null cells remain in place, and each non-null reference position consumes
exactly one valid model reference. `mergeReferences_correct` proves this relation
is equivalent to successful merging. The capture theorems connect it to emitted
root arrays and the strong fields of originals and copies, including exact cell
counts and duplicate references. Mark capture preserves root and strong cells.

`encodeUnit`, `UnitProjection`, and `UnitsProjection` specify weak-unit capture
from the supplied model state. `captureUnit_correct` and `captureUnits_correct`
prove success exactly for matching unit kinds, in-range logical unit IDs and
valid referenced copies, with exact output contents. Unit order, IDs, guard
positions and tagged payload values are preserved; capture also preserves
unvisited inactive entries. These claims remain in the separate proof module
and are axiom-audited.

`ReferenceValid` and `FreshStates` independently specify reference bounds and
fresh object states. `checkReference_correct` and `checkFreshHeap_correct` prove
acceptance exactly when these predicates hold, including null references, empty
heaps, and both placement policies. The state predicate admits only unmarked
originals without forwarding. These leaf validation proofs are axiom-audited;
they impose no restriction on immediate payloads.

`FreshIds` specifies dense physical allocation IDs in array order and canonical
logical `obj-N` identities whose ordinals are a permutation of the same range.
It allows any pairing between logical and physical IDs. The canonical syntax
predicate is independent of the executable parser. `validateStructure_fresh_ids`
proves these properties for successfully checked fresh structures;
`prepareFreshHeap_ids` establishes them on the ordered preparation result.
These soundness claims do not assert that every well-formed input is accepted.

`HeapCellReferencesValid` covers all stored cell and guard targets in roots,
strong slots, owner weak units and external weak units. It includes both guards
and payloads of inactive entries, and applies to fresh and completed snapshots.
Immediates impose no condition in these predicates. `checkCell_references` and
`checkUnits_references` establish the local contracts;
`validateStructure_cell_references` and `prepareFreshHeap_cell_references`
connect them to accepted snapshots. These soundness theorems are axiom-audited;
forwarding headers and remembered-owner IDs have separate validation contracts.

`RememberedValid` certifies that the single remembered owner list has no duplicate
IDs, names only old movable or nonmoving originals, and covers old-to-young
strong and weak edges. Owners need not be reachable. The independent coverage
predicates inspect stored targets, including inactive weak entries.
`prepareFreshHeap_remembered` proves these properties for the returned ordered
heap, using the existing weak-coverage theorem. Its axiom dependencies are guarded.

`FieldsBeforeEntries` specifies owner weak layout independently as a pairwise
ordering relation. `checkUnits_fields_first` applies to both original and
destination storage, while external units remain free to interleave fields and
entries. `validateStructure_fields_first` and `prepareFreshHeap_fields_first`
lift this property to accepted heaps. Existing reference-validation contracts
are preserved through the extracted per-unit checker, and all claims are audited.

`FreshUnitIds` specifies a permutation of dense global logical weak-unit IDs
across fresh originals and external units. `validateStructure_fresh_unit_ids`
proves uniqueness and density from the actual insertion path and set-size check;
`prepareFreshHeap_unit_ids` applies it to preparation output. Destination copies
are outside this fresh-input claim because they share their originals' logical
unit IDs. The preparation state certificate is shared by these and the object-ID
proofs. All public claims are axiom-audited.

`FreshWellFormed` composes independent state, identity, cell-reference,
remembered-owner, owner-layout and unit-ID predicates. It contains no validator
success premise. `prepareFreshHeap_wellFormed` certifies this full specification
for returned heaps; `prepareFreshHeap_ordered` also certifies the exact normalized
result. Canonical object IDs include their existing unsigned-64-bit wire bound.
These are soundness claims; validator acceptance is a subsequent theorem.

Placeholder immediates use `Int64` in the wire heap and collector model, matching
the existing C++ payload and JSON encoding. `decodeImmediate_correct` proves that
JSON integer conversion accepts exactly the signed value of the result, without
wrapping. This representability check is confined to decoding; `checkCell_correct`
accepts every immediate and checks only reference targets. GC well-formedness has
no immediate-range predicate or additional success-theorem assumption.

`MarkSnapshotProjection` specifies the complete marking result: exact roots,
ordered original records, observed marks, unchanged strong and weak cells,
external weak units and remembered owners. `captureMark_projects` proves this
for successful marking capture, complementing the full evacuation projection.

`HeapValidationCompleteness` supplies shared checker-acceptance lemmas from
semantic predicates: canonical object IDs, reference-valid cells, and
original/external units with valid contents, fresh IDs and admissible owner
layout pass their checks with exact updated state. `HeapAudit` guards the
transitive axiom dependencies of the public completeness and soundness claims.

`checkUnits_success` extends leaf acceptance to the stateful original/external
unit scan. It constructs successful execution from reference safety, valid owner
layout and globally fresh unit IDs, preserving the global ID ledger. The proof
handles arbitrary prior IDs and external entry/field interleaving; no validator
acceptance premise is used.

`validateOriginalAllocation_success` composes the leaf and unit-scan contracts
for an original record. Its semantic premises supply the physical ordinal,
canonical logical identity, an empty in-range origin slot, reference safety,
owner layout and fresh unit IDs. It constructs the actual successful result,
preserving copy-table state and extending precisely the origin slot and ID ledger.

`checkRememberedOwners_success` accepts every unique list of valid old owners,
including nonmoving owners, and constructs its physical-ID ledger.
`checkStrongOwners_success` uses that ledger and independent edge coverage to
accept the complete strong-edge scan. Remembered membership imposes no reachability
or strong/weak classification on the owner itself.

`HeapFormatCompleteness` proves `FreshWellFormed.completed`: a fresh heap
satisfies the independent completed-snapshot constraints with every record in
the original prefix and no destination copies. Fresh structural acceptance then
reuses `HeapCompletedFormatCompleteness`; it needs no separate allocation-fold
invariant or final pairing traversal. Freshness remains independently checked.

`prepareFreshHeap_correct` closes fresh-input validation in both directions:
preparation accepts and returns exactly `orderedHeap input` if and only if that
heap satisfies `FreshWellFormed`. Completeness covers the entire structural
validator, including root/reference scans, dense unit IDs, remembered owner and
edge checks, and final origin/destination consistency. There is no scalar-range
or executable-validation premise.

`HeapPreparationProofs.lean` gives independent correspondence and acceptance
contracts for wire-to-model references, cells and weak templates. Conversion uses
the retained physical-to-logical pairing, preserves immediate values and cell
tags, and processes both guards and the payload of every entry. Every
reference-valid input converts successfully. Its axiom audit is in the root build.

`strongReferences_correct` and `strongReferences_succeeds` certify ordered
strong-reference extraction: immediates and nulls are skipped; every non-null
reference is converted in slot order, including duplicates. `originalKind_correct`
and `originalKind_succeeds` certify generation, movability and destination policy
for every original state. Both contracts have independent semantic predicates.

`modelObjectIds_succeeds` proves that every valid object-identity permutation
builds the exact physical-to-logical vector used by preparation. `PhysicalObjectMap`
specifies every vector entry in input allocation order. The shared `modelObjectId`
helper is the original inline decoder, with unchanged parsing and error behavior.

`indexOriginals_succeeds` certifies the next preparation phase: each physical
record is retained at its declared logical object index, and the completed table
converts successfully to the original-record vector. The private table invariant
records exact stored values and prevents overwrites through ID uniqueness; full
ID coverage proves all slots are populated. The executable loop is unchanged.

`indexTemplates_succeeds` certifies complete weak-template table construction:
each unit's logical ID selects its corresponding converted field or entry.
`indexHeapTemplates_succeeds` derives its preconditions directly from
`FreshWellFormed`, accounting for the owner/external concatenation permutation.
Both include empty tables and all stored cells of inactive entries. The extracted
`indexTemplates` loop preserves the original conversion and error order.

`modelUnitIds_correct` and `modelUnitIds_succeeds` specify ordered conversion of
owner and external location lists, retaining duplicates and exact unit IDs.
`modelRememberedOwners_succeeds` converts existing physical owner IDs through
the retained object pairing, preserving the whole input list. Uniqueness and
coverage of the resulting owner set remain separate generational invariants.

`HeapObjectMapping.lean` connects the two identity schemes: a valid physical-to-
logical map is bijective, indexed originals agree with every physical input slot,
and a converted reference selects exactly the wire allocation at its physical ID.
These independent lemmas carry object provenance into the generational invariants;
they do not assume that model validation or collection succeeds.

`modelGenerational_wellFormed` derives the model's first-class strong-generation
constraint from wire validity and independent conversion correspondence. It proves
remembered-owner uniqueness, old-owner membership and every old-to-young strong
edge's coverage. `validateGenerational_complete` proves that a well-formed raw
model is accepted by the actual model validator; neither theorem assumes success.

`modelWeakLocations_ledger` derives the dense global repair-location ledger
from the converted owner/external lists and wire validity. It uses
`OriginalsIndexed.perm`, which proves that logical indexing retains every input
record exactly once. `weakLocations_wellFormed` derives disjoint storage and
location coverage from that ledger, including empty heaps and external-only units.

`ModelTemplate.targets` certifies exact target enumeration between wire units
and model templates. After removing null/immediate positions, every guard and
payload reference is retained in callback order, including duplicates and stored
references of inactive entries. This is a correspondence in both directions,
so a missing target cannot make the coverage constraint silently weaker.

`modelWeak_wellFormed` derives the complete first-class weak-generation
constraint: unique located storage and remembered coverage of every old owner's
stored young target. Exact template-target correspondence carries guards and
payloads into this proof, including inactive entries. `validateWeak_complete`
proves actual validator acceptance from that independent constraint.

`modelChildren_succeeds`, `modelKinds_succeeds` and `modelOwnerUnits_succeeds`
construct the model's vectors with correspondence at every logical object index.
They derive acceptance from independent cell/state/unit-bound predicates and
preserve row positions through `ListProjection.getElem`. No validator or earlier
conversion-success premise is needed.

`prepareModel_complete` and `prepareHeap_complete` construct actual successful
preparation from `FreshWellFormed`. `PreparedHeapProjection` relates the exact
ordered snapshot, both identity schemes, all graph/kind/location vectors, roots,
remembered owners and every weak template, and carries the complete weak invariant.
There is no executable-success or scalar-range premise. `HeapCollectionCompleteness`
composes this input-to-model construction with collection and capture success.

`runCollection_mark_complete` proves successful full marking for every valid
fresh wire heap, with both prepared-input correspondence and the complete marking
snapshot projection. `captureMark_complete` derives capture success from fresh
source records and the core marking completion theorem. These claims cover the
mark branch; `HeapCollectionCompleteness` covers evacuation capture and output validation.

`HeapCaptureCompleteness` supplies successful strong-slot and weak-unit capture
from slot cardinality, allocated references, bounded unit IDs and matching unit
kinds. It connects prepared wire templates to full and young weak collectors and
proves successful ordered source/copy capture with exact snapshot projection.
Private helpers handle individual units, headers and objects. Unselected sources
remain unmarked and unforwarded; copied origins must be selected. The core proofs
supply completion and state validity; the prepared model supplies physical/logical
correspondence and weak-unit kinds. Immediate/null positions are retained, with
no capture-success premise. Its axiom audit is part of the root build.

`HeapObjectPairCompleteness` specifies reciprocal source/copy links independently
of validation and proves their acceptance, including placement and ordered weak
layout. The executable pair check is factored out of the existing validator loop.

`HeapCompletedFormatCompleteness` gives independent semantic constraints for
completed snapshots, covering global physical IDs, logical source/copy identities,
references, weak-unit density/layout and remembered-owner coverage. It proves
structural validation, public validation and serialization succeed from those
constraints, with no executable-success premise. Dense physical IDs make sorting
the identity. Private helpers in the same module handle destination records and
source/copy scans with exact sparse lookup by logical object ID. Sources may be
marked or forwarded; destinations preserve the source table and weak-unit ledger.
`HeapSnapshotCoverage` establishes the completed constraints for collected states.

`HeapStrongCaptureValidity` derives strong capture conditions from the actual
full and generational core collectors: allocated references, slot cardinality,
completion, selected copy origins and unchanged unselected objects.

`HeapWeakCaptureValidity` derives weak capture conditions from both weak
collectors. Every unit preserves its field/entry kind and contains only allocated
references; unregistered units retain source-only templates. Reference validity
includes stored references in inactive entries.

`HeapWireCaptureValidity` connects prepared source identities and allocated
model references to physical bounds in the emitted source/copy heap. Captured
weak units preserve unit IDs, kinds and field-before-entry order; encoded cells
and units satisfy the wire reference bounds.

`HeapSnapshotStructure` derives complete reference bounds and ordered owner
layouts from snapshot projection and prepared-input validity, including diagnostic
source units. It also proves source identity/generation preservation and copied
identity, physical ordinal and destination classification. The same module proves
dense global physical IDs, canonical retained names, unique copied logical IDs and
bounded destination identities. The original logical-ID permutation and
source/external weak-ID sequence survive capture; copy layouts reuse source weak
IDs without contributing new IDs.

`HeapSnapshotReferences` connects observed forwarding to the matching captured
destination by logical identity. Every source/copy pair has reciprocal physical
links, the requested placement and matching ordered weak layout; unforwarded
objects have no destination. The same module connects emitted strong/weak
references back to their actual model positions and proves that allocated
references known to be old cannot resolve to diagnostic young sources. Physical
source lookup and remembered-owner validity survive capture independently of
reachability. Internal forwarding-search and copy-index helpers are private.

`HeapStrongCaptureCoverage` derives old-to-young strong-edge coverage for
captured storage from the core collectors. Unremembered old sources have only
old original children; copied strong storage uses the proved disposal-safety
result. Both modes use the same remembered-owner set.

`HeapWeakCaptureCoverage` derives corresponding weak-edge coverage using the
same remembered-owner set. Unregistered old-owner templates use first-class
coverage of all stored targets, including inactive payloads. Registered units and
copied-owner units use the existing repair-safety proofs in both modes.

`HeapSnapshotCoverage` transfers both reference categories to the emitted heap
and proves complete remembered-owner validity. Diagnostic weak storage reuses
input coverage; repaired storage uses the core coverage results. Its
`SnapshotProjection.completed_wellFormed` theorem assembles all independent
completed-format constraints without an output-checker success premise.

`HeapCollectionCompleteness` proves actual `captureStrong`, `collectPrepared`
and `runCollection` success from independent valid-input and core-state facts.
Full and young evacuation produce the exact selected core state, satisfy the
completed heap format and serialize successfully. `runCollection_supported_complete`
covers full marking and both evacuation modes; young marking has an explicit
rejection theorem. No executable-acceptance assumption or extra scalar predicate
is used. These claims concern the Lean runner and do not assert formal C++ refinement.
The [mutation campaign](mutation-campaign.md) records all 30 proof-only Lean catches
and their C++ counterparts at the completed proof head.

Copy capture reuses the source record's stable `object_id` directly;
`allocation_id` remains the copy allocation ordinal. `captureCopy_object_id`
certifies this independently of decimal rendering, and the aggregate copy
projection uses the same source-identity contract.

`SnapshotProjection` composes the cell and weak-unit relations into the complete
physical evacuation snapshot. `captureStrong_projects` covers both full and young
capture: ordered originals followed by copies, exact identities and headers,
strong slots, weak units, external weak order, and the unchanged remembered set.
Selected movable originals retain their diagnostic weak templates; other
originals and destinations expose the observed unit state. The full weak wrapper
is covered by `captureEvacuated_projects`. These are successful-capture claims;
`HeapCollectionCompleteness` composes them with actual core completion and output-format acceptance.

Evacuation identity, root and strong-slot corollaries derive from these complete
projections. Each capture operation is unfolded once to establish its projection;
the narrower facts reuse it without adding input-validity assumptions. Marking's
identity, root and strong-slot facts likewise derive from its complete projections.

Capture preservation is also proved at the executable boundary. Fresh preparation
preserves the allocation records up to sorting; capture reads originals directly
from that retained snapshot and appends destination copies in allocation order.
`runCollection_original_prefix` and `runCollection_preserves_original` establish
that successful output retains every input original's object ID, allocation ID,
storage kind, generation and placement policy, including unreachable old originals
in a young collection. Headers and repaired fields may change. The corresponding
`HeapAudit` guards the transitive axiom dependencies.

The `gc_lean` executable exposes the same file interface as `gc_cpp`:

```sh
lake exe gc_lean --algorithm evacuate --collection full \
  ../../tests/gc_differential/fixtures/before.json > lean-after.json
```

It accepts `mark` or `evacuate` for full collection; young collection supports
evacuation with strong and weak fields. It also supports
`--flag=value`, `--` before the path, and `--help`/`-h`. The complete result is
serialized before writing stdout. Argument, file, format and collection errors
produce a `gc_lean:` diagnostic on stderr and exit status 1.

From `proofs/gc`, run:

```sh
lake build
lake exe gc_format_tests
lake exe gc_execution_tests
lake exe gc_cli_tests
```

The default build checks the existing proofs and builds the runner and test executables.
The format tests cover shared fixtures, round trips and heap validity. Execution
tests cover both collectors, permuted identities, mixed destinations, weak
repair, long cycles and deliberately inconsistent model results. These runtime
tests are separate from the collector proofs and their axiom audits. CLI tests
launch the built runner as a process and cover reviewed outputs, argument/file
errors, exact paths, empty heaps and rejection of completed snapshots. Their
Lake dependency builds `gc_lean` automatically; CMake remains independent of Lean.

### Host Python Comparison

Use standard host CPython 3.10 or newer for this harness. Build `gc_cpp` and `gc_lean` using
the commands above, then run from the repository root:

```sh
python3 tests/gc_differential/compare.py \
  --cpp build-debug/tests/gc_differential/gc_cpp \
  --lean proofs/gc/.lake/build/bin/gc_lean \
  --algorithm evacuate --collection full \
  tests/gc_differential/fixtures/before.json
python3 -m unittest discover -s tests/gc_differential
```

The comparator copies the input once, supplies the same file to both runners,
and compares complete JSON output. It ignores object member order and whitespace,
preserves array order, and distinguishes booleans, integers and other JSON value
types. It does not renumber IDs or follow forwarding. Runners own heap validation;
the Python harness does not implement another heap parser or collector.

Exit codes are 0 for agreement, 1 for a heap mismatch, and 2 for runner/input
failure. Both runners are attempted even if one fails. Rejection (including
identical rejection by both), crash, timeout, launch errors, invalid JSON and
stderr on a successful exit are runner failures. `--timeout` is a finite positive
per-runner limit in seconds, defaulting to 15.

Every comparison failure creates a unique directory under
`gc-differential-failures/` (or `--failure-dir`). It contains the original
`input.json` bytes, raw `cpp.stdout`/`cpp.stderr` and `lean.stdout`/`lean.stderr`,
and `run.json` with outcome, run configuration, statuses, exit codes and replayable
commands pointing to the preserved input. Mismatches also include
`difference.diff`. Existing failures are never overwritten. Keep the referenced
executables to replay a result; the harness does not build or archive binaries.

`tests/gc_differential/compare.py` is also importable by property tests in that
directory (or with `PYTHONPATH=tests/gc_differential`):

```python
from compare import assert_equivalent

assert_equivalent(heap, cpp=cpp_path, lean=lean_path, algorithm="evacuate")
```

The callable takes an ordinary heap dictionary, returns `None` on agreement,
and raises `HeapMismatch` or `RunnerFailure`, each carrying `artifact_dir`.
It accepts `collection="full"|"young"`, `timeout` and `failure_dir` as keyword
arguments. Young collection requires `algorithm="evacuate"`. The Hypothesis
campaign below uses this same interface.
The comparator and its tests use only Python's standard library and run outside
the VM. Controlled-runner tests exercise mismatch/failure handling; the real
runner integration test is skipped when either binary has not been built.

### Hypothesis Campaigns

Campaigns require host CPython 3.11 or newer. Install the pinned test dependencies in an isolated environment,
then run from the repository root with both file runners already built:

```sh
python3 -m venv build-gc-python
build-gc-python/bin/python -m pip install -r tests/gc_differential/requirements.txt
build-gc-python/bin/python tests/gc_differential/campaign.py \
  --cpp build-debug/tests/gc_differential/gc_cpp \
  --lean proofs/gc/.lake/build/bin/gc_lean \
  --collection full --examples 100 --max-objects 20 --seed 132
build-gc-python/bin/python -m unittest discover -s tests/gc_differential
```

`heap_strategies.valid_heaps(max_objects=20)` constructs ordinary heap
dictionaries with dense object, allocation and weak-unit IDs, references bounded
by the current object count, and fresh original states. Logical identities and
the input record order are independently permuted. Strong/root cells include
signed 64-bit immediates and nullable references; owner fields precede entries,
while external units can mix both kinds. References can form cycles and aliases,
and guards can be inactive. Each owner has at most four strong slots, two weak
fields and two entries; roots and external units have at most four elements.
Construction needs no rejection filters or additional heap validator. Both modes
construct complete remembered-owner coverage for both strong and weak references,
including conservative entries and permuted remembered-owner order.

`campaign.run_campaign(...)` runs separate mark and evacuation properties through
`assert_equivalent`. `--algorithm mark|evacuate|both` defaults to both for full
collection. Young collection requires `--algorithm evacuate --collection young`.
Full and young evacuation have separate Hypothesis identities and example
databases, so running a young campaign cannot retire a full-only failure. The default
budget is 100 examples per algorithm, with at most 20 objects; the summary reports
actual comparison calls. Hypothesis may also confirm failing cases, each of which
retains its own artifact. The test deadline is disabled; `--timeout` remains the
per-runner timeout. Exit codes are 0 for a successful campaign, 1 for a heap
mismatch, and 2 for runner, configuration or Hypothesis framework failures.

Only Hypothesis's reuse and generation phases are enabled. Without an explicit
seed, its configured example database can replay earlier failures; its default
`.hypothesis/` directory is ignored by Git. `--seed` makes generation reproducible
for the same strategy, settings, Python and Hypothesis versions and disables the
example database. Campaign failures add the example budget, object bound, seed,
attempt count, enabled phases and Python/Hypothesis versions to `run.json`.
Preserved heap files can always be replayed directly with the comparator.
Shrinking remains a separate slice so it can retain the specific failure class.

The campaign tests exercise seed and persisted database replay, algorithm selection, injected mismatch
and rejection capture, framework error classification, and generated heaps with
both real runners (including zero-object heaps). Without the optional dependency,
the campaign test module is skipped and the standard-library comparator tests
remain runnable.

### Generational Strong Model

[Generational weak resolution and repair](young-weak.md) uses one remembered
owner list for strong and weak fields. C++ `RememberedSet`, the JSON `remembered`
array and Lean `rememberedOwners` describe the same owner membership. Both
runners support young weak evacuation, and host-CPython campaigns generate
combined strong/weak coverage. Proofs establish exact strong projection,
registration and queue accounting, repaired unit values, and safety of external
storage and retained owners. Physical storage lifetimes and retirement remain
adapter obligations.

[Young-only strong evacuation](young-strong.md) describes the proof-carrying
heap representation and collection invariants. `RawGenerationalHeap` represents
decoded data; `GenerationalHeap` embeds `WellFormed`, enforcing unique old
remembered owners and complete old-to-young strong coverage. Generation,
movability and destination derive from `OriginalKind`. Conversion establishes
the proof with a decidable check; malformed raw heaps cannot become collector
inputs without proving these constraints.

The driver reuses the proved copy-discovery primitive with selection around it.
Full selection is proved equal to the existing strong driver; the full weak
model and its theorems remain intact. Young liveness includes selected roots and
remembered-owner children, even for owners unreachable from ordinary roots.
The final results cover termination, selected reachability, repaired retained
fields, unselected objects remaining unmarked/unforwarded, unchanged selected
movable source payloads, identity, scan uniqueness and retained-reference disposal
safety. The reset theorem concerns the logical retained graph after promotion;
it does not reset physical remembered sets or reclaim source storage.

The shared `young_before.json`, `young_after.json` and `young_full_after.json`
fixtures exercise this boundary, including permuted identities, ordinary and
nonmoving destinations, aliases, cycles and conservative remembered owners.

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
| [WeakRepairLemmas](CloverGC/WeakRepairLemmas.lean) | Shared field, cell and unit repair proofs from explicit resolver laws |
| [WeakResolutionProofs](CloverGC/WeakResolutionProofs.lean) | Full resolver laws, shared repair specializations and final liveness |
| [WeakScheduling](CloverGC/WeakScheduling.lean) | Strong-state erasure, unit enumeration, registration and visit histories |

`MarkedQueue` is a generic proof view of two lists, not a replacement executable
collector or an additional runtime state. The mark/copy models retain their
existing definitions; their lemmas reuse the same list-level argument.

`WeakRepairLemmas` proves phase compatibility, settled-result correctness and
late completion for arbitrary early/late resolvers. Its explicit premises say
that early resolution cannot report dead, late resolution cannot remain pending,
and early resolved targets remain resolved and stable under late resolution.
Full and generational repair proofs instantiate those laws while retaining their
existing public theorem statements. Resolver implementations and collection
drivers remain separate.

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
traversal. Executable differential comparison against the C++ test heap is
implemented separately. The generational weak model establishes owner-based
remembered coverage and repair safety; barrier maintenance and runtime ownership
proofs remain later work.
