# Native Subtype Storage

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Partial |
| Scope | Native builtin subclass storage, class-creation validation, and constructor specialization; explicit Python slot declarations remain deferred |
| Owning layers | Runtime object model, shapes, native layouts, allocation and reclamation; builtin families own construction and payload semantics |
| Validated against | `78f7490a` (2026-10-11), for current-code constraints and local CPython 3.14.5 probes; storage and constructor changes remain unimplemented |
| Supersedes | The previous investigation in this document |

The accepted direction is a common overflow-storage pointer in every
Python-visible heap `Object`, native heap integer storage for `int` subclasses,
and overflow-only additional attributes for builtin families that currently
lack `SlotObject` storage. Existing `SlotObject` families retain their regular
storage mechanism. This decision was agreed on 2026-10-10. Class-creation
validation, constructor specialization, and shared pure-slot layouts were
refined on 2026-10-11.

The staged checklist is in the
[implementation plan](native-subtype-implementation-plan.md).

## Common Storage

Move the overflow-storage pointer into `Object` as
`MemberHeapPtr<OverflowSlots>`, preserving its ownership and one-word storage.
`SlotObject` must reuse that
pointer rather than contain a second one. Rename `SlotObject` to an
inline-capable base, provisionally `InlineSlotObject`, to make its remaining
responsibility explicit: it marks the physical presence of inline `Value`
slots. Overflow access must no longer require casting to that base. Internal
heap records that do not have Python object identity, such as Shape records,
do not acquire the common pointer. The pointer is initially null; allocate
overflow storage only when needed.

A layout without the inline-capable base must declare zero inline attribute
capacity in its instance Shape. Root-shape construction, transitions, and clones
must preserve that invariant. A positive capacity would let attribute access
overwrite native fields. Existing inline-capable layouts must likewise keep
Shape capacity within the actual physical slot region.

Current user-class creation gives every class the same capacity of ten inline
slots, without inheriting or adding the base class's capacity. Replace that
unconditional choice with native-lineage validation at class creation, before
installing the instance root Shape. Walk the class MRO and inspect each base's
instance native layout, not the C++ layout of the `ClassObject` record.
Resolve compatible native payload ancestry and reject conflicting requirements
with `TypeError` during class creation, rather than at instance allocation.
For example, `dict` and `list` cannot both supply the payload. Ordinary Python
mixins and sibling subclasses sharing a compatible native lineage need not
conflict. Subclass eligibility is validated separately.

The resulting physical layout determines the Shape's inline capacity:

- Native layouts without inline attribute storage, including `Dict`, declare
  zero inline capacity. Additional attributes use overflow storage.
- Pure-slot objects share `NativeLayoutId::SlotObject` and dynamically allocated
  inline storage. Their predefined attributes establish the minimum inline
  slot count; additional attributes may use spare inline capacity or overflow.
- Inline-capable layouts with additional native metadata retain distinct native
  IDs and fixed physical inline capacity.
- Ordinary user instances use the shared pure-slot layout with automatic inline
  slots. The universal `object` base maps to this layout and does not force
  native subclasses with other payloads to adopt generic slot-only storage.

Root-shape construction, transitions, and clones preserve the selected storage
interpretation. Allocators assert that the requested class's root Shape is
compatible with the allocation's native layout and physical capacity. The
metadata API for expressing native compatibility still needs an implementation
sketch; the validation phase and capacity rules are settled here.

Conceptually:

```text
Object:
    heap header
    shape pointer
    overflow-storage pointer

native builtin:
    Object
    native fields and payload
```

Native layout identity describes physical payload storage. For pure-slot
objects it does not distinguish Python types; the Shape supplies Python class
identity and attribute locations. Subclassing must not replace a non-slot native
payload with generic slot-only storage, or turn a subclass instance into an
exact builtin value where class identity remains observable.

### Shared Pure-Slot Layout

The following six current concrete types become the shared
`NativeLayoutId::SlotObject` layout. Their predefined attributes remain at their
existing fixed inline locations in the root Shape.

| Current concrete type | Minimum inline slots | Predefined attributes |
|---|---|---|
| `Instance` | 0 | None; ordinary user classes retain automatic inline capacity |
| `ModuleLoaderObject` | 3 | `kind`, `name`, `path` |
| `ModuleSpecObject` | 6 | `name`, `loader`, `origin`, `submodule_search_locations`, `has_location`, `parent` |
| `Slice` | 3 | `start`, `stop`, `step` |
| `ExceptionObject` | 1 | `args` |
| `StopIterationObject` | 2 | `args`, `value` |

These objects contain only inline slot values beyond the common header and
overflow pointer. Allocation, object-size recovery, and tracing use their
actual physical inline slot count, recorded on the object. The class's
`instance_inline_slot_count`, exposed by `get_instance_inline_slot_capacity()`,
must remain consistent with the root Shape's capacity. The count
must accommodate every predefined inline location; predefined values must be
initialized according to the owning builtin's semantics.

Each concrete `Object` subclass declares
`static constexpr uint32_t kPredefinedInlineSlotCount` for allocation validation.
It is zero when there are no predefined inline fields and includes inherited
physical locations otherwise. Allocation must provide at least this minimum;
fixed native layouts also constrain the maximum physical inline capacity.

`Function`, `ClassObject`, and `ModuleObject` retain separate native layout
IDs. They contain native metadata in addition to their attribute slots;
`ModuleObject`, for example, also contains validity cells and dependent-cell
storage. They continue using common slot access with their existing fixed
physical capacities. Attribute extensibility is separate from this distinction.

Sharing the native ID does not make unrelated builtin classes interchangeable.
Typed access and trusted handlers must validate the relevant semantic class and
predefined-slot contract rather than treating the shared ID as proof of a
particular builtin type. Native ancestry validation must likewise retain the
predefined-slot compatibility requirements; matching IDs alone are insufficient.

Future slack tracking may remove only the unused trailing inline capacity
common to the entire root Shape transition tree. Predefined attributes already
occupy their root locations, so they set a lower bound; attributes allocated by
descendant Shapes can raise that bound. It must never remove an occupied inline
location. The profiling trigger and coordination of allocation counts, existing
objects, and Shape/cache metadata remain a later implementation design.

For native families currently lacking slot storage, such as `String`, `Dict`,
and `List`, additional subclass attributes all use `OverflowSlots`. Do not
append an inline attribute region after the native payload. String length,
tuple length, or integer digit count must not become attribute-shape
polymorphism: otherwise-equivalent instances of the same subclass should share
a Shape regardless of payload length.

This rule does not remove native fields or existing predefined inline slots.
Exceptions retain `.args`, and StopIteration retains `.value`, in their native
inline locations. These existing `SlotObject` families continue using their
regular shape/slot mechanism. Ordinary user classes based on `object` likewise
keep their current automatic inline slots; all classes ultimately inherit from
`object`, so native payload ancestry is the relevant distinction.

Exact builtins do not automatically become attribute-mutable merely because
space for the overflow pointer exists. Shape permissions continue to determine
whether an instance accepts user attributes. A subclass of an immutable native
builtin may have mutable attribute Shapes while the native payload remains
immutable; do not copy the exact builtin's attribute-immutability restrictions
onto that subclass. Dictionary payload entries and subclass attributes remain
separate: `d["x"]` and `d.x` are different storage.

## Integer Subclasses And Immediate Values

An `int` subclass must always be a heap object, including for values that fit in
an SMI. Use native BigInt storage with the requested subclass identity. Its
constructor must not pass through a result-normalization path that returns an
SMI and discards that identity. A generic wrapper containing an exact builtin
payload is not required by this design.

Ordinary exact integers retain the existing SMI representation and result
normalization. `finalize_bigint` checks whether a result fits an SMI before
allocating, so disabling normalization would introduce heap allocations where
none are currently needed. Any change to that policy needs separate performance
evidence.

The complete current immediate inventory and local CPython 3.14.5 subclass
probes are:

| Clover immediate value | Python type | CPython permits subclasses? |
|---|---|---|
| SMI | `int` | Yes |
| `False`, `True` | `bool` | No |
| `None` | `NoneType` | No |
| `NotImplemented` | `NotImplementedType` | No |
| `Ellipsis` | `ellipsis` | No |
| Not-present marker | Internal VM sentinel | Not applicable |
| Exception marker | Internal VM sentinel | Not applicable |

Interned pointers and refcounted pointers are not immediate objects. Floats and
large integers already have heap representations. Among the Python-visible
immediate types, only `int` requires a subclass representation.

Native layout checks and exact-type checks must remain distinct. For example,
`is_exact_int_value` currently accepts any BigInt layout; supporting native
integer subclasses requires auditing that assumption. Payload-compatible
operations must preserve normal Python method resolution and subclass overrides.
Result identity is operation-specific: constructing the requested subclass
preserves its identity, while an explicit `int(x)` conversion or ordinary
inherited integer arithmetic can produce an exact builtin integer. Existing
reuse paths must not return a subclass object where the operation requires an
exact result.

## Constant-Offset Attribute Access

An attribute cache guards the receiver Shape and relevant lookup validity,
then applies a resolved physical location. The target access forms are:

```text
inline:   load Value at object + constant attribute offset
overflow: load OverflowSlots pointer at object + constant anchor offset,
          then access a constant element offset in that storage
```

The common pointer makes the overflow anchor independent of native payload
length. Cached access must not execute a payload-size recipe, inspect length to
find attribute storage, or cache a raw field address. Shape metadata may supply
resolved offsets when populating a cache; cache hits and future AArch64 lowering
should use constants after their guards.

Current cache plans store a storage kind and index relative to the existing
fixed slot base. Their final offset encoding is an implementation detail still
to be sketched. Shape transitions and clones must preserve physical storage
interpretation, and tracing must use the same interpretation as attribute
reads, writes, and deletion.

## Allocation, Reclamation, And Construction

The common pointer adds one eight-byte field to layouts that currently inherit
only `Object`. Existing `SlotObject` layouts must not pay for a duplicate field.
Native member offsets, size descriptors, release descriptors, and offset
assertions must be updated together. The allocator rounds extents to a
32-byte granularity, so the physical allocation increase depends on the size
boundary crossed; it is not necessarily eight additional allocated bytes for
every instance.

Reclamation must observe and release the common overflow pointer for all
Python-visible heap layouts. Native payload extent remains recoverable from
native layout descriptors and object-local counts. Overflow storage is a
separate allocation, avoiding a second variable-sized tail on a native object.
The planned copying collector must likewise recover extent without following
Shape or ClassObject metadata awaiting relocation.

## Constructor Thunk Specialization

Every builtin class must supply or inherit an allocation-correct `__new__`.
For an instantiable native builtin family, this method allocates a valid native
object for the requested class, including user subclasses; it must not hard-code
exact builtin identity. Non-instantiable builtin classes must reject construction
rather than fall through to generic `Instance` allocation. A fresh native
allocation uses the requested class's instance root Shape: `dict.__new__`
called for a dict subclass produces `Dict` native storage with that subclass's
Shape. Builtin payload construction remains owned by the builtin family.

Subclass eligibility is separate from storage and construction capability.
Having an overflow pointer does not make `bool`, `NoneType`, or the other
non-subclassable singleton types legal bases. Native payload compatibility must
also be validated independently during class creation, as described above.

When building a constructor thunk, resolve `__new__` through the existing
lookup machinery. Recognize supported builtin implementations through the
trusted-handler system and its semantics metadata, then emit equivalent
specialized allocation bytecode. Specialization depends on the resolved
trusted method and its applicability contract, not merely its name, the
absence of `__new__`, or the class's native layout. Allocator-specific semantics
tags and bytecode lowering still need a concrete implementation sketch; the
current `TrustedHandlerSemantics` enum does not yet describe these allocators.

The specialization must preserve argument validation, payload construction,
requested subclass identity, exception behavior, and coordinated default
`object.__new__`/`object.__init__` argument rules. A trusted allocator whose
contract proves that it returns the requested class can omit the post-allocation
type check. Methods without a supported trusted allocation contract use the
general call path. Keep family-specific construction policy in the owning
builtin implementation; the thunk builder selects known lowering rather than
inventing a native-layout-based fallback allocator.

Constructor thunks and their call caches retain the existing MRO-and-contents
validity guard. Replacing, adding, or deleting `__new__` or `__init__`, including
inherited methods, invalidates the affected constructor lookup. Invalidation
protects subsequent calls; it does not cancel a thunk already executing.

The existing constructor machinery supports `__new__`-only or `__init__`-only
class calls, but not the combination. The `__new__`-only thunk currently returns
its result directly; this also misses initialization when the requested class
has no initializer but `__new__` returns a subclass instance whose class does.
Supporting post-allocation initialization is a prerequisite for the builtin
constructor work.

For general `__new__` calls, use the existing `is_instance_of_known_class`
bytecode operation on the returned value. Its class-identity comparison handles
the common exact-class result immediately; only a different actual class
requires an MRO subtype check. Do not add another inline cache for this test.
Subclass-returning allocators are supported through that fallback without
optimizing them as a common case. A failed check is not an error: return the
allocation result and skip initialization.

When the check passes, resolve `__init__` by special-method lookup using the
returned object's actual class and MRO. Ignore instance attributes and custom
instance attribute-access hooks. Preserve descriptor binding and require the
initializer to return `None`. Lookup or cache validation occurs after `__new__`
returns, so it observes a different returned subclass or an initializer replaced
during allocation. Existing special-method and call caches can avoid repeated
lookup without pinning the initializer selected at constructor entry.

### Argument Binding

Current thunks copy the selected method's signature, defaults, and keyword
remapping into the thunk, then forward normalized parameters. That is suitable
for the current single-method paths but cannot generally serve two independent
calls: normalization loses which arguments were omitted and may apply defaults
that belong only to `__new__`.

The combined path must preserve the original constructor arguments and bind
them independently for allocation and initialization. Do not forward
`__new__`'s normalized frame as `__init__`'s input. In particular, each method
must apply its own defaults and argument-validation rules, and initialization
must not be validated or called when the returned object fails the type check.

Preserving the call does not inherently require an unconditional `*args`/
`**kwargs` tuple-and-dict wrapper. Existing register-based calls, keyword-name
metadata, and cached argument-binding plans are inputs to an implementation
sketch for keeping original arguments live and rooted across allocation.
Actual variadic parameters may still require their usual collection objects.
The original-argument representation, thunk signature, and forwarding mechanism
remain to be designed; this document does not claim that the current thunk ABI
already preserves them. This is constructor-protocol work, not builtin payload
policy.

Verification must cover class-definition-time layout conflicts and compatible
mixins, zero/fixed/dynamic inline capacities, native allocation with subclass
Shapes, replacement of specialized methods, unrelated and subclass `__new__`
results, initializer replacement during allocation, independent defaults and
keyword binding, and non-`None` initializer results. Performance verification
must include ordinary exact-class construction and known builtin allocation
without forcing every call through variadic collection objects.

## Explicit Python Slot Declarations

Clover already uses Shapes and ordinary slot storage without requiring a Python
`__slots__` declaration. Keep that automatic mechanism as the storage model.
For now, `__slots__` remains an ordinary class attribute; do not implement
restricted names, slot descriptors, or CPython's physical layout machinery in
this work. This is an intentional compatibility limitation.

Future semantic enforcement can build on predefined stable locations,
uninitialized values, latent descriptors, and shape permissions. Declared names
would need assignment, deletion, and reassignment at the same location while
rejecting undeclared attributes where required. Inheritance and class-level
descriptor behavior require their own review when that work is requested.

Local CPython 3.14.5 probes confirmed that ordinary subclasses of `int`, `tuple`,
`bytes`, `str`, `list`, and `dict` accept attributes. Nonempty explicit
`__slots__` are rejected for `int`, `tuple`, and `bytes`, but accepted for `str`,
`list`, and `dict`. These Python-visible rules do not dictate Clover's physical
attribute storage.

## Alternatives And Revisit Conditions

A subtype-only tail after a variable native payload gives instance-dependent
attribute offsets. Encoding actual payload length into Shapes introduces
unwanted polymorphism; calculating the tail location on each cache hit violates
the constant-offset access requirement. It is not the selected representation,
even though a fixed-size native prefix could support constant tail offsets.

A subtype-only preheader at a fixed negative offset could preserve exact builtin
sizes and constant-offset access. CPython's managed dictionary machinery uses a
related approach. Clover's tagged pointer positions and object-header bitmap
share a 32-byte grid, and construction currently returns the allocation start.
Simply adding one word and shifting the returned pointer would violate those
invariants. Supporting a hidden prefix requires deliberate allocation,
discovery, size, reclamation, and future relocation changes. Revisit this only
if measured exact-builtin memory cost justifies that machinery.

Separate slot-capable native variants or generic wrappers could avoid the
common-pointer cost, but add payload-access boundaries, layout variants, and
identity risks across builtin operations and trusted handlers. They are not the
initial direction. A future extension-type API may require a separate storage
and tracing contract; this decision does not settle that API.

References:

- [Value representation](value-representation.md)
- [Builtin object model](builtin-object-model.md)
- [Native layout descriptors](native-layout-descriptors.md)
- [Refcounting and reclamation](refcounting-and-reclamation.md)
- [CPython managed dictionary preheader](https://github.com/python/cpython/blob/v3.14.5/Include/internal/pycore_object.h#L851-L883)
