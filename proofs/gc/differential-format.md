# GC Heap JSON Format v2

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | C++ and Lean file runners, host-Python comparator and bounded Hypothesis campaigns implemented |
| Scope | One heap snapshot schema with stable object identities, allocation IDs and tagged object states |
| Owning layers | Runners validate and project snapshots; Lean owns the executable model and proofs; C++ uses the synthetic heap and collector templates |
| Validated against | `986deae7` (2026-11-07), including tagged weak cells, native storage classes and the executable Lean definitions |
| Supersedes | N/A |

Both runners read a heap and dump a heap using this same schema. Object IDs
are strings identifying logical objects. Allocation IDs are integers naming
physical storage in one global namespace. Algorithm and collection selection
are separate run configuration. The schemas below specify every v2 field.

## Run Configuration

```sh
gc_cpp  --algorithm evacuate --collection full before.json > cpp-after.json
gc_lean --algorithm evacuate --collection full before.json > lean-after.json
```

Both commands are implemented with matching file interfaces.
`--algorithm` is `mark` or `evacuate`. Full collection supports both. Young
collection supports evacuation with strong and weak fields: `--algorithm evacuate
--collection young`, with owner and external weak storage. Young mark is rejected. Seeds, generator settings,
completion status and diagnostic traces are outside the heap file. A rejected,
crashed or unfinished run must fail rather than emit a successful heap result.

Both runners require exactly one `--algorithm`, one `--collection full|young`, and one input
file. They reject duplicate selections and extra files. Collection success emits
one heap to stdout and exits zero. Argument and validation errors emit no heap,
write diagnostics to stderr and exit one. File/output and execution errors also
fail the process. The complete result is validated and serialized before writing
stdout.
`--help` prints usage to stdout and exits zero. See the
[C++](README.md#c-file-runner) and [Lean](README.md#lean-heap-format) build and test commands.

## IDs and Cells

- `object_id` is a canonical string `"obj-N"`, where `N` is a nonnegative decimal
  integer without leading zeros (except `0`). With `n` logical objects, their
  identities are exactly `"obj-0"` through `"obj-(n-1)"`. Copying preserves this
  string. It encodes the existing numeric logical identity directly.
- `allocation_id` is a nonnegative integer, unique per physical record. With
  `a` allocations, IDs are exactly `0..a-1`. New allocations append IDs in actual
  allocation order across all destination arenas. There are no separate source
  and destination allocation-ID ranges starting at zero.
- Initially there is one allocation per object. The pairing between object IDs
  and allocation IDs is explicit; matching numeric suffixes are not required.
  After copying, multiple allocation records can carry the same `object_id`.
  Original allocations retain IDs `0..n-1`; destinations append allocation IDs
  `n`, `n+1`, and so on without resetting the allocation counter.
- `unit_id` is a nonnegative integer naming a logical weak repair unit. It is a
  separate typed field, never an object reference. The original allocations and
  external storage together declare exactly `0..k-1`, each once. A destination
  retains the same unit IDs and kinds as its original, in separate storage.

Every strong slot, root, standalone weak value and entry payload is a **cell**
with exactly one of these shapes, unchanged before and after collection:

```json
{"imm": -7}
{"ref": null}
{"ref": 3}
```

The block lists alternatives. Immediates are signed 64-bit integers. References
always contain allocation IDs or null; an object-ID string is never a pointer.
Null reference cells and immediate zero remain distinct.

## Heap and Unit Records

| Record | Required fields |
|---|---|
| Heap | `format`, `version`, `roots`, `objects`, `external_weak`, `remembered` |
| Object allocation | `object_id`, `allocation_id`, `state`, `strong`, `weak` |
| Standalone weak field | `unit_id`, `kind: "field"`, `value` |
| Weak entry | `unit_id`, `kind: "entry"`, `guards`, `payload` |

`format` is `"clover-gc-heap"`; `version` is `2`. `roots` and each `strong` array
are ordered cells, retaining duplicates and aliasing. `objects` contains every
original and destination allocation before source reclamation.

A unit nested in an object's `weak` array occupies that physical allocation.
`external_weak` contains external units. A field's `value` and an entry's
`payload` are cells. `guards` is exactly two nullable allocation IDs. Both guards
and the tagged payload are weak; neither contributes strong discovery. Null
guards are valid inactive entries, which reset to null/null/immediate zero.

Owner weak arrays contain all field units before entry units, preserving order
within each kind. External units are visited in array order. A physical unit is
identified by its container allocation and `unit_id`; an external unit by its
`unit_id`. A logical unit can therefore appear in both an original allocation
and its destination without aliasing their storage.

## Generational Heap Invariant

`remembered` is an ordered array of distinct allocation IDs naming old
originals: `old_original` or `nonmoving_original`. Membership represents strong
remembered status; it is independent of the tracing `marked` flag. Nonmoving
owners follow the same edge rule as other old objects.

Every old-to-young strong reference must have its owner remembered. Young
owners may point to any generation. Old owners may always point to old objects,
including remembered old objects. Conservative remembered owners with no young
references are allowed. These constraints hold independently of collection mode.
Every old-to-young weak reference also requires its owner in this same list.
This includes both guards and referenced payloads, even in inactive entries.
The list records owners, not strong or weak references.

Full collection ignores the remembered list as additional roots. Young collection
processes roots, then each remembered owner in list order: its actual strong
slots, followed by all its weak units. It then drains copied objects. Old owners are not marked or enqueued. External weak units are visited before roots. This
includes owners unreachable from the ordinary roots: their storage still survives
minor collection. Both snapshots retain the supplied list, before reset or
retirement. Copy destinations are old and marked; discarded original payloads
remain visible in the diagnostic snapshot.

The Lean collector accepts a proof-carrying `GenerationalHeap` whose `WellFormed`
proof establishes unique old remembered owners and complete old-to-young strong
coverage. Raw wire data is converted and checked before constructing this type.
Collection-phase invariants account for pending copies with raw fields and
processed owners with repaired fields; they do not impose a stable-heap rule on
all intermediate physical storage. See [the model and proof boundaries](young-strong.md).

## Object State Enums

`state` is exactly one variant from this table. No other fields are permitted
inside a variant.

| `kind` | Additional fields | Implied properties |
|---|---|---|
| `young_original` | `copy_to`, `header` | Movable original allocation, young |
| `old_original` | `copy_to`, `header` | Movable original allocation, old |
| `nonmoving_original` | `marked` | Nonmoving original, old, normal header |
| `ordinary_destination` | None | Ordinary destination, old, marked, normal header |
| `nonmoving_destination` | None | Nonmoving destination, old, marked, normal header |

`copy_to` is `"ordinary"` or `"nonmoving"`. It is used only by movable originals.
`marked` is a boolean. `header` is exactly one of:

```json
{"kind": "normal", "marked": false}
{"kind": "forwarded", "forwarded_to": 3}
```

A normal header permits either boolean mark value and implies no forwarding.
A forwarded header implies an unmarked original; `forwarded_to` is a required,
nonnull allocation ID. Nonmoving originals and destinations cannot carry a
forwarded header. Destination variants describe completed phase snapshots;
old generation, marked state and absence of forwarding are checked invariants,
not optional defaults. Young/old generation and movability are not separate
fields that could contradict the selected variant.

For example, allocation `1` with `object_id: "obj-1"` can forward to allocation
`3`, which also has `object_id: "obj-1"`. Roots then contain `{"ref": 3}`. The
forwarding target must exist, be a destination of the configured placement, have
the same object ID, and have a normal header with no further forwarding.

## Complete Heap Before Collection

```json
{
  "format": "clover-gc-heap",
  "version": 2,
  "roots": [
    {
      "ref": 1
    },
    {
      "ref": 1
    }
  ],
  "objects": [
    {
      "object_id": "obj-0",
      "allocation_id": 0,
      "state": {
        "kind": "nonmoving_original",
        "marked": false
      },
      "strong": [
        {
          "ref": 1
        }
      ],
      "weak": []
    },
    {
      "object_id": "obj-1",
      "allocation_id": 1,
      "state": {
        "kind": "young_original",
        "copy_to": "ordinary",
        "header": {
          "kind": "normal",
          "marked": false
        }
      },
      "strong": [
        {
          "ref": 0
        },
        {
          "imm": 7
        }
      ],
      "weak": [
        {
          "unit_id": 0,
          "kind": "field",
          "value": {
            "imm": -7
          }
        }
      ]
    },
    {
      "object_id": "obj-2",
      "allocation_id": 2,
      "state": {
        "kind": "old_original",
        "copy_to": "nonmoving",
        "header": {
          "kind": "normal",
          "marked": false
        }
      },
      "strong": [],
      "weak": []
    }
  ],
  "external_weak": [
    {
      "unit_id": 1,
      "kind": "entry",
      "guards": [
        1,
        0
      ],
      "payload": {
        "ref": 2
      }
    }
  ],
  "remembered": [
    0
  ]
}
```

## Complete Heap After Full Evacuation

```json
{
  "format": "clover-gc-heap",
  "version": 2,
  "roots": [
    {
      "ref": 3
    },
    {
      "ref": 3
    }
  ],
  "objects": [
    {
      "object_id": "obj-0",
      "allocation_id": 0,
      "state": {
        "kind": "nonmoving_original",
        "marked": true
      },
      "strong": [
        {
          "ref": 3
        }
      ],
      "weak": []
    },
    {
      "object_id": "obj-1",
      "allocation_id": 1,
      "state": {
        "kind": "young_original",
        "copy_to": "ordinary",
        "header": {
          "kind": "forwarded",
          "forwarded_to": 3
        }
      },
      "strong": [
        {
          "ref": 0
        },
        {
          "imm": 7
        }
      ],
      "weak": [
        {
          "unit_id": 0,
          "kind": "field",
          "value": {
            "imm": -7
          }
        }
      ]
    },
    {
      "object_id": "obj-2",
      "allocation_id": 2,
      "state": {
        "kind": "old_original",
        "copy_to": "nonmoving",
        "header": {
          "kind": "normal",
          "marked": false
        }
      },
      "strong": [],
      "weak": []
    },
    {
      "object_id": "obj-1",
      "allocation_id": 3,
      "state": {
        "kind": "ordinary_destination"
      },
      "strong": [
        {
          "ref": 0
        },
        {
          "imm": 7
        }
      ],
      "weak": [
        {
          "unit_id": 0,
          "kind": "field",
          "value": {
            "imm": -7
          }
        }
      ]
    }
  ],
  "external_weak": [
    {
      "unit_id": 1,
      "kind": "entry",
      "guards": [
        null,
        null
      ],
      "payload": {
        "imm": 0
      }
    }
  ],
  "remembered": [
    0
  ]
}
```

The original allocation `1` and destination allocation `3` both identify
`obj-1`. Allocation `0` and both roots now reference `3`. Allocation `2` is still
unmarked, and the external weak entry resets because that target is dead.
Original allocation fields remain visible until reclamation by the caller.

For `--algorithm mark`, the schema is identical: reachable originals become
marked, all headers remain normal, cells, remembered membership and weak units remain unchanged, and
no destination allocations are added.

## Validation and Dumping

This is an internal format for generated test files. Producers emit the exact
v2 shapes above: UTF-8 JSON, unique keys, known fields, and decimal integer
literals without fractions or exponents. Readers use their JSON libraries;
lexical rejection behavior for noncanonical or hand-edited files is not part of
the comparison contract. The C++ reader currently performs additional checks.

- Readers check required values, types, supported versions and enum variants.
  Booleans are not integers. `object_id` uses its canonical string syntax;
  allocation and unit IDs are integers. Reject gaps and duplicate IDs in their
  respective domains. Logical object IDs repeat only in their destinations.
- Remembered owners must be unique old originals; every old-to-young strong
  edge must have a remembered owner, including in full-collection inputs.
- Roots, cells, guards and forwarding targets must name existing allocations.
  Each object ID has exactly one original allocation. In this single-collection
  scope it has at most one destination. The destination and its original have
  matching object IDs, matching weak-unit layouts and reciprocal forwarding.
- All original and external logical unit IDs are unique. Destination weak
  layouts repeat only their original's unit IDs and kinds, in the same order.
  Physical object and unit storage must remain disjoint and stable through
  collection and dumping.
- A fresh-run heap has only original variants, all marks clear and normal
  headers. An old original can still be copied during full collection. The
  current runner does not replay a completed snapshot as another collection.
- Dumpers must check the exact native state before selecting a variant. Do not
  derive a C++ object's variant just from `is_movable()`: check `TestStorage`,
  generation, header kind, mark state and actual forwarding target. In
  particular, `Source` at a registered destination address is an error even
  though its movability equals an ordinary destination's movability.
- Verify every property implied by the enum. An unmarked, young or forwarded
  destination is a failed run; do not silently emit a normal destination enum.
  The same applies to wrong identities, invalid references or mismatched
  forwarding. Report observed inconsistencies in diagnostics rather than
  reconstructing a corrected heap from expected values.
- The synthetic object's unused `preferred_destination` metadata on nonmoving
  originals and destinations is outside this JSON contract. Existing native
  fixture tests continue to check its preservation. `copy_to` on movable
  originals remains part of the compared heap.

The Lean format layer uses bundled `Lean.Json.parse` and printing directly.
It shares the snapshot and fresh-run heap validation rules above; format parsing
does not invoke the collector model. See
[Lean format tests](README.md#lean-heap-format).

## C++ and Lean Projection

C++ registers an allocation ID for each initial address and appends IDs at actual
copy allocation calls. It reads `object_id` from the actual stored
`logical_identity`, encoding numeric identity `N` as `"obj-N"`. Pointer cells and
forwarding targets resolve through the address-to-allocation-ID table. Unknown
addresses or identity/storage inconsistencies fail the run; the dumper never
substitutes expected identity or forwarding metadata.

Lean's current definitions do not directly store the entire serialized heap.
The runner retains validated initial slot layouts and metadata alongside the
executable model. This is the required projection:

| Heap observation | Lean source |
|---|---|
| Stable object identity | Original `Fin n` value; destination `CopyRecord.origin` |
| Original allocation ID | The allocation explicitly paired with that object ID in the input |
| Destination allocation ID | `n + j` for destination ledger index `j`, with `n` initial allocations |
| Destination state enum | `CopyRecord.destination`, together with checked copy-mark and canonicality invariants |
| Original source state | Retained young/old classification and `copy_to`, plus actual model marks and forwarding |
| Nonmoving original state | Initial classification plus actual model marks |
| Rewritten reference slots and roots | `EvacuationState` fields/roots and copy records, merged with retained slot layouts |
| Immediate and null strong/root slots | Their original positions and values in those retained layouts |
| Weak values in selected movable originals | Initial templates, since discarded source weak storage is not repaired |
| Weak values in unselected old originals | Repaired owner storage |
| Weak values in nonmoving originals and destinations | The corresponding current `Weak.State.units` values |
| External weak values | Current `Weak.State.units` values for external units |

For each strong/root slot layout, consume exactly one model reference per
original nonnull reference position, in order. Preserve immediate/null positions
and require exact reference cardinality: no truncation, default filling or
sorting. Weak cells already carry their tags in the model. Unvisited owner units
retain their initial values.

The internal destination ledger index is translated to the global allocation
number above; it is not a separately exposed JSON ID namespace. Logical IDs are
never used as allocation IDs without consulting the explicit initial pairing.
The projection must use the existing executable collection definitions. It
must not reconstruct collection by following forwarding or running another
collector. Presentation metadata and C++ raw storage remain outside the Lean
collector proofs; all existing theorem assumptions and conclusions remain
subject to review if implementation requires changing them.

## Ordering, Comparison and Generation

Emit allocation records by ascending allocation ID, with IDs assigned at
allocation rather than sorted from addresses or object IDs afterward. Preserve
root, strong-slot, owner-unit and external-unit order, including duplicates and
aliasing. JSON object member order and whitespace are irrelevant. Compare every
serialized cell and state, both kinds of object ID, unit layouts, and forwarding.
Traces, scan counts and timing are separate diagnostics/tests; heap equality
alone does not establish scheduling properties or collector correctness.

C++ RapidCheck or a Python campaign can generate the same fresh heap and run
both collectors with identical separate run configuration. The format does not
depend on the generator. Directed heap/expected-heap pairs require no generation.
Seeds, campaign settings and runner revisions belong to reproduction metadata
outside the heap. The host-CPython comparator in `tests/gc_differential/compare.py`
provides a file CLI and an importable `assert_equivalent` function for generated
heap dictionaries. It compares both runners' complete JSON results, distinguishes
mismatches from runner failures, and preserves original failing inputs and run
outputs. `heap_strategies.py` constructs valid fresh heaps directly, and
`campaign.py` runs separate full mark/full evacuation/young evacuation properties
with bounded Hypothesis generation and replay. Both modes generate strong and weak fields and construct complete remembered
owner coverage for both kinds of reference. Seeds and Python/Hypothesis versions are recorded with
comparison failures, outside the heap. Shrinking remains future work. See the
[comparison commands and API](README.md#host-python-comparison).

A future shrinker restores the dense object-identity, allocation-ID and unit-ID
domains separately and updates all affected references and associations. It
preserves the original failing heap and checks that the candidate is valid and
retains the specific failure.
