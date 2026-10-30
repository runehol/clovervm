# Native Layout Reference Audit

| Field | Value |
|---|---|
| Document type | Architecture audit |
| Status | Accepted scope; findings recorded |
| Implementation | Strong scanning and weak-processing primitives implemented; collector integration pending |
| Scope | Managed-reference ownership, weak fields, and native storage for every registered native layout |
| Owning layers | Runtime layouts supply cells; memory management supplies liveness and forwarding decisions |
| Validated against | Slice 3c working tree based on `aaf57767` (2026-10-30) |
| Supersedes | N/A |

This inventory follows `CL_NATIVE_LAYOUT_REGISTRY` in
[`native_layout_registry.h`](../src/memory/native_layout_registry.h). It is a
field/ownership audit, not permission to move every layout. Mutable roots,
relocation safety, identity/hash policy, barriers, and collector integration
remain separate stages in the
[implementation plan](generational-copying-gc-implementation-plan.md).

Every `Object`/`InlineSlotObject` subtype inherits strong shape and overflow
storage cells. The table lists its additional cells. Typed SMI/bool count cells
may be included in ordinary spans but are immediates, not heap edges. An array
wrapper owns its backing handle; the backing exposes its own elements, so the
owner scanner must not recursively enumerate them.

## Registered Layout Inventory

| Registered C++ layout | Additional strong cells / reference storage | Weak or native-only state |
|---|---|---|
| `BigInt` | No additional edges | Digit count, sign and trailing digits are scalar storage, not pointers; temporary BigInt views/scratch are not this heap layout |
| `ByteArray` | Raw-array backing handle | Byte contents and borrow spans are not heap edges |
| `ByteArrayIterator` | Optional source bytearray | Index is SMI |
| `Bytes` | No additional edges | Count is SMI; byte payload is native data |
| `List` | Value-array backing handle | Sort mutation flag is scalar |
| `Tuple` | Trailing initialized values, SMI-counted span | Size is SMI |
| `EnumerateIterator` | Source iterator and next index (which may be BigInt) | None |
| `ZipIterator` | Sources tuple | Strict flag is inline bool |
| `RangeIterator` | No additional heap edges | Current/stop/step are SMI cells |
| `StringIterator` | Source string | Index is SMI |
| `TupleIterator` | Source tuple | Index and length are SMI |
| `ListIterator` | Source list | Index is SMI |
| `DictKeysView` | Dict | Stack iterator helpers are not persistent heap fields |
| `DictValuesView` | Dict | As above |
| `DictItemsView` | Dict | As above |
| `DictKeyIterator` | Optional dict | Index, expected size and remaining count are SMI |
| `DictValueIterator` | Optional dict | As above |
| `DictItemIterator` | Optional dict | As above |
| `ModuleObject` | All inline attribute cells, current globals/builtins validity cells, dependent-cell array backing | Attached validity-cell elements are owned strongly, not weak; transient `ModuleBuiltinsLookup` is not a heap field |
| `ModuleLoaderObject` | Kind, name, path | None |
| `ModuleSpecObject` | Name, loader, origin, search locations, has-location, parent | Inline values remain in the span |
| `ExceptionObject` | Args tuple | No Python finalizer is introduced by this audit |
| `StopIterationObject` | Inherited args plus value | None |
| `OSErrorObject` | Inherited args plus errno, strerror, filename, filename2, characters-written | Inline/sentinel values remain in the span |
| `Float` | No additional edges | Double payload is not a reference; NaN hash currently depends on its address |
| `Function` | Code object, optional defaults tuple and docstring | Copied call-signature metadata is scalar |
| `Dict` | Hash-table and entry-array backing handles | Hash-table indices are raw; entry backing contains key/value and SMI hash cells. Relocation must preserve hash/identity invariants |
| `SlotDict` | Target object | Iterator helpers are transient, not heap fields |
| `IOBaseObject` | No additional heap edges | Base-closed flag is bool |
| `BufferedIOBaseObject` | Inherited IOBase cells only | No additional native resource state |
| `RawIOBaseObject` | Inherited IOBase cells only | As above |
| `FileIOObject` | Name and mode, inherited base cells | Closed/closefd are bool; fd/access/seekability are native scalars. Existing explicit-close-only policy is unchanged; no resource finalizer is added |
| `Slice` | Start, stop, step | None |
| `String` | No additional heap edges | Count is SMI; characters are not pointers |
| `Instance` | Initialized inline prefix, aux-counted | Shape supplies capacity for sizing, not the initialized reference bound; uninitialized tail is not scanned |
| `CodeObject` | Shape/overflow and direct members, keyword-remap backing handles, native constant-table entries, owned expanded-call plan names | Raw inline-cache fields are weak (see below). CompilationUnit pointer, bytecode, source offsets, exception tables, native targets and trusted resolver are native metadata |
| `jit::JitCodeObject` | Tagged constant-pool prefix, even though externally stored | Untagged pool suffix, code slice, executable addresses and frame metadata are not managed edges |
| `ValidityCell` | No references | Valid bit and dependency-mutability enum only; immutable dependency does not turn a borrowed cache pointer into strong ownership |
| `Scope` | Parent scope, raw name-table/index backing handles, value-array name backing handle | Raw index-table elements are not references |
| `Shape` | Class, all descriptor names including latent ones, transition names and successor shapes in native vector storage | `previous_shape` is weak; descriptor location/flag records are scalar |
| `OverflowSlots` | Aux-counted full capacity of initialized cells | Logical size is not the scan bound; unused capacity is initialized to `not_present` |
| `RawArrayBacking` | No references | Opaque byte payload; `storage_bytes` records its storage extent |
| `ValueArrayBacking` | Recorded Value-cell extent | Cells include compound entry records; transferred old backing can have its count zeroed (see below) |
| `HeapPtrArrayBacking` | Recorded raw-pointer cell extent | Same ownership-transfer count caveat; null cells are valid |
| `HandleChunk` | All initialized handle cells | Native extension context/frontier pointers are roots/borrow state outside the chunk, not chunk fields |
| `ClassObject` | Name, bases, MRO, inline attrs, current validity cells, attached-cell backing handles, instance root shape, constructor thunk | Attached cells and cached constructor thunk are explicitly owned and remain strong, despite their cache-like role |

`Invalid` and `TestOnly` IDs are deliberately not production registry entries.
No Python-visible weakref layout currently exists.

## CodeObject Weak Fields

| Cache family | Active weak components |
|---|---|
| Attribute read | Receiver shape, validity cell, and either constant value or storage owner according to plan kind |
| Attribute mutation | Receiver shape, validity cell, and storage owner for StoreExisting or next shape for Add/Delete; ChangeClass has no active pointer arm |
| Module-global read | Validity cell and slot storage owner |
| Module-global mutation | Validity cell and existing-slot storage owner |
| Ordinary function call | Tagged guard, function, code object, optional validity cell |
| Keyword call | As above; destination-register array is native data |
| Expanded call | Tagged guard, function, optional validity cell; keyword-name plan is strong, not weak |
| Operator | Valid pointer-valued shape keys, both optional lookup cells, function and code object; trusted handler is a native function pointer |

Weak processing runs after strong closure and before source storage is reused.
The resolver identifies live unchanged/moved targets and dead targets without
tracing or dereferencing dead inputs. Null optional fields, inline tagged guards,
and invalid/inline shape keys do not reach the resolver. Inactive plan fields
are not consulted or repaired; verifiers must likewise respect discriminants.
Any dead active component resets the whole cache entry, discarding partial
repairs. No observer or mutator runs between component repairs.

Resetting an expanded-call entry destroys its strongly owned keyword-name plan.
While RC remains authoritative, this decrements those names and can enqueue
zero-count children, but does not itself run reclamation. Strong closure has
already conservatively included those names. Successful pointer repair itself
is RC-neutral. Whole-cache clearing also includes keyword-call entries.

## Remaining Relocation Prerequisites

- Native runtime roots, `CompilationUnit` storage, intern tables and compiler
  state are not made precise or mutable by this heap-field audit. Root auditing
  remains stage 4. Transient lookup results, views, and native borrows need
  explicit lifetime/safepoint contracts rather than blanket weak treatment.
- `ValueArray`/`HeapPtrArray` growth transfers element ownership and zeroes the
  old backing's `value_cell_count`. That field currently also drives
  `object_size_in_bytes()`: it is not immutable allocation-reservation metadata
  for retired backing storage. Future extent queries/collector policy must
  account for this; this slice does not change backing layouts or ownership
  transfer. Cleared/transferred cells must not become duplicate strong owners.
- `Instance` shape-derived sizing and lazy initialization remain unchanged.
  Future capacity shrinking is collector policy, not permission to scan or copy
  uninitialized tails.
- Identity and hashing, including existing float NaN address-derived hashes and
  cached dict hashes, require a relocation-stable policy before movement.
  Reference repair alone is insufficient.
- Native vectors, unique_ptr plans and external code-cache storage in custom
  layouts require their documented stable placement or a separately approved
  relocation strategy. No bitwise-move permission follows from scan coverage.
- File descriptors and any future finalization/native resource policy remain
  separate from tracing. Python finalizers, resurrection, Python weakrefs, and
  extension-owned opaque fields are not implemented here.

The primitives are tested with fake liveness maps while all test objects remain
physically alive. Surviving substitutions are restored before ordinary RC
teardown. They are not called by production reclamation in slice 3c.
