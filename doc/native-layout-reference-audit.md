# Native Layout Reference Audit

| Field | Value |
|---|---|
| Document type | Investigation (architecture audit) |
| Status | Accepted |
| Implementation | Partial (strong scanning and weak repair implemented, production collector integration pending) |
| Scope | Managed-reference ownership, weak fields, and native storage for every registered native layout |
| Owning layers | Runtime layouts supply cells. Memory management supplies liveness and forwarding decisions. |
| Validated against | `fc815dc5` (2026-11-27) |
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
| `BytesIterator` | Optional source bytes | Index is SMI |
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
| `Float` | No additional edges | Double payload is native data. NaNs use a canonical address-independent hash. |
| `Cell` | Current value | An empty cell holds the `not_present` sentinel |
| `Function` | Code object and optional defaults tuple, docstring and closure tuple | The closure tuple owns its cells. Copied call-signature metadata is scalar. |
| `Dict` | Hash-table and entry-array backing handles | Hash-table indices are raw; entry backing contains key/value and SMI hash cells. Relocation must preserve hash/identity invariants |
| `SlotDict` | Target object | Iterator helpers are transient, not heap fields |
| `IOBaseObject` | No additional heap edges | Base-closed flag is bool |
| `BufferedIOBaseObject` | Inherited IOBase cells only | No additional native resource state |
| `RawIOBaseObject` | Inherited IOBase cells only | As above |
| `FileIOObject` | Name and mode, inherited base cells | Closed/closefd are bool; fd/access/seekability are native scalars. Existing explicit-close-only policy is unchanged; no resource finalizer is added |
| `Slice` | Start, stop, step | None |
| `Property` | Getter, setter, deleter and docstring value | The `doc_from_getter` flag is an immediate bool in the span |
| `BoundMethod` | Callable and receiver | None |
| `StaticMethod` | Wrapped value | None |
| `ClassMethod` | Wrapped value | None |
| `String` | No additional heap edges | Count is SMI; characters are not pointers |
| `Instance` | Initialized inline prefix, aux-counted | Shape supplies capacity for sizing, not the initialized reference bound; uninitialized tail is not scanned |
| `CodeObject` | Defining module, local scope, name, docstring, JIT code, keyword-remap backing handles, native constant-table entries and owned expanded-call plan names, in addition to shape and overflow | Cache identities and targets are weak (see below). The compilation-unit pointer, bytecode, source offsets, exception tables, native targets and trusted resolver are native metadata. |
| `jit::JitCodeObject` | Tagged constant-pool prefix, even though externally stored | Untagged pool suffix, code slice, executable addresses and frame metadata are not managed edges |
| `ValidityCell` | No references | Valid bit and dependency-mutability enum only; immutable dependency does not turn a borrowed cache pointer into strong ownership |
| `Scope` | Parent scope, raw name-table/index backing handles, value-array name backing handle | Raw index-table elements are not references |
| `Shape` | Class, all descriptor names including latent ones, transition names and explicitly stored successor shapes in native vector storage | `previous_shape` is weak, including when a transition uses it as a fallback. Descriptor location and flag records are scalar. |
| `OverflowSlots` | Aux-counted full capacity of initialized cells | Logical size is not the scan bound; unused capacity is initialized to `not_present` |
| `RawArrayBacking` | No references | Opaque byte payload; `storage_bytes` records its storage extent |
| `ValueArrayBacking` | Recorded Value-cell extent | Cells include compound entry records; transferred old backing can have its count zeroed (see below) |
| `HeapPtrArrayBacking` | Recorded raw-pointer cell extent | Same ownership-transfer count caveat; null cells are valid |
| `HandleChunk` | All initialized handle cells | Native extension context/frontier pointers are roots/borrow state outside the chunk, not chunk fields |
| `RandomEngine` | No additional edges | MT19937 state words and index are native data |
| `ClassObject` | Name, bases, MRO, inline attrs, two current validity cells, attached-cell array backing, instance root shape and constructor thunk | The attached-cell backing owns its validity cells. The class owns the cached constructor thunk. |

`Invalid` and `TestOnly` IDs are deliberately not production registry entries.
No Python-visible weakref layout currently exists.

## CodeObject Weak Fields

[`CodeObject::visit_weak_reference_units`](../src/bytecode/code_object.cpp)
exposes one repair unit per inline-cache entry.
[`Shape::visit_weak_reference_units`](../src/object_model/shape.cpp) exposes its
`previous_shape` back-link as a separate unit.

| Cache family | Active weak components |
|---|---|
| Attribute read | Receiver shape and lookup validity cell. `ConstantValue` adds the cached value, and `ReceiverSlot` adds the storage owner. `BindFunctionReceiver` and the descriptor reads (`DataDescriptorGet`, `NonDataDescriptorGet`, `StaticMethodGet`, `ExactPropertyGet`) add both the storage owner and the cached value or getter in `constant_or_getter`. |
| Attribute mutation | Receiver shape and lookup validity cell. `StoreExisting` adds the storage owner. `AddOwnProperty` and `DeleteOwnProperty` add the next shape. `DescriptorSet`, `DescriptorDelete` and `ExactPropertySet` add the descriptor and its hook. `ChangeClass` uses only the common fields. |
| Module-global read | Validity cell and slot storage owner |
| Module-global mutation | Validity cell and existing-slot storage owner |
| Ordinary function call | Guard identity (exact callable or receiver shape), optional guard validity cell, function and code object |
| Keyword call | Same weak fields as an ordinary function call. The destination-register array is native data. |
| Expanded call | Guard identity, optional guard validity cell and function. The binding plan owns its keyword names strongly. |
| Operator | Pointer-valued shape keys, optional operand lookup cells, function and code object. The trusted handler is a native function pointer. |

The [evacuation driver](../src/gc/evacuation.h) attempts repair while draining
strong work. Targets outside the collection, forwarded movable targets and
marked stationary targets resolve immediately. An unresolved target remains
unchanged, and the driver queues its exact repair unit for a late pass. Other
active fields in the same entry can still be repaired on the early pass. After
strong closure, the late pass treats still-unresolved targets as dead. Weak
resolution neither traces targets nor dereferences dead inputs.

Repair selects fields by the active plan kind and skips null pointers, nonpointer
tagged values and invalid or inline shape keys. A dead active target resets the
whole cache entry even if another field is pending. A dead shape back-link is
cleared. Unit storage stays valid through late repair, and the mutator cannot
observe intermediate repairs. The
[descriptor contract](native-layout-descriptors.md) describes the visitor and
resolver lifetime rules.

Resetting an expanded-call entry destroys its strongly owned keyword-name plan.
While RC remains authoritative, this decrements those names and can enqueue
zero-count children, but does not itself run reclamation. The owner's strong
scan visits those names before repair. Successful pointer repair itself
is RC-neutral. Whole-cache clearing also includes keyword-call entries.

## Remaining Relocation Prerequisites

- Native runtime roots, `CompilationUnit` storage, intern tables and compiler
  state are not made precise or mutable by this heap-field audit. Root auditing
  remains stage 4. Transient lookup results, views, and native borrows need
  explicit lifetime/safepoint contracts rather than blanket weak treatment.
- `ValueArray`/`HeapPtrArray` old-backing count zeroing is RC-only retirement
  bookkeeping, not a moving-GC prerequisite. The front array exclusively owns
  its backing; after replacement the old backing is unreachable garbage and is
  not traced or copied. Keep zeroing while RC needs it, then remove it as part
  of the [post-cutover cleanup](generational-copying-gc-implementation-plan.md#10-expand-coverage-and-retire-transitional-machinery).
  No new historical-capacity field or retired-backing shrink audit is required.
  Live backing extents and initialized storage still require correct queries.
- `Instance` shape-derived sizing and lazy initialization remain unchanged.
  Future capacity shrinking is collector policy, not permission to scan or copy
  uninitialized tails.
- Movement must preserve logical identity and the validity of hashes cached in
  dict entries. Float NaN hashing is already address-independent.
- `CodeObject`, `Shape` and `jit::JitCodeObject` all declare native destruction
  hooks. [Heap construction](../src/memory/heap_construction.h) marks such
  layouts NonMoving. Their native vectors, owned plans and external code storage
  require stable placement until there is a relocation strategy for them.
- File descriptors and any future finalization/native resource policy remain
  separate from tracing. Python finalizers, resurrection, Python weakrefs, and
  extension-owned opaque fields are not implemented here.

[Weak-reference unit tests](../tests/test_weak_reference_processing.cpp) use
fake liveness maps with physically live targets and restore surviving
substitutions before ordinary RC teardown.
[Isolated real-layout evacuation tests](../tests/test_gc_real_layout_evacuation.cpp)
exercise forwarding and marks with external weak roots. Production reclamation
still uses deferred refcounting.
