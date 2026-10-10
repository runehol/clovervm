# Native Subtype Storage

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Not started |
| Scope | Physical storage for Python subclasses of native builtin types; explicit Python slot declarations remain deferred |
| Owning layers | Runtime object model, shapes, native layouts, allocation and reclamation; builtin families own construction and payload semantics |
| Validated against | `e0c3248e` (2026-10-10), for current-code constraints; the accepted storage changes are not implemented |
| Supersedes | The previous investigation in this document |

The accepted direction is a common overflow-storage pointer in every
Python-visible heap `Object`, native heap integer storage for `int` subclasses,
and overflow-only additional attributes for builtin families that currently
lack `SlotObject` storage. Existing `SlotObject` families retain their regular
storage mechanism. This decision was agreed on 2026-10-10.

## Common Storage

Move the overflow-storage pointer into `Object`. `SlotObject` must reuse that
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

Current user-class creation gives every class the same default of ten inline
slots, without inheriting or adding the base class's capacity. Instance allocation
uses that class's root Shape capacity. When the selected native layout cannot
hold inline attribute slots, subclass creation must override this generic default
with zero. The inheritance-aware capacity-selection details are deferred to a
later implementation sketch.

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

Native layout identity still describes the builtin payload. The Shape supplies
Python class identity and attribute locations. Subclassing must not replace a
native payload with a generic `Instance`, or turn a subclass instance into an
exact builtin value where class identity remains observable.

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

Every builtin class must supply or inherit an allocation-correct `__new__`.
For an instantiable native builtin family, this method allocates a valid native
object for the requested class, including user subclasses; it must not hard-code
exact builtin identity. Non-instantiable builtin classes must reject construction
rather than fall through to generic `Instance` allocation. Class construction
must validate compatible native ancestry and prepare matching metadata, but
builtin payload initialization remains owned by the builtin family. This note
does not choose a new allocation-plan API or a constructor-thunk implementation.
It does not reinstate the reverted exception-specific layout selection in
`class_object.cpp`.

Subclass eligibility is separate from storage and construction capability.
Having an overflow pointer does not make `bool`, `NoneType`, or the other
non-subclassable singleton types legal bases. Native payload compatibility must
also be validated independently: ordinary Python mixins can coexist with a
compatible native storage lineage, but conflicting native payload requirements
must be rejected before an instance is allocated. The generic metadata and
validation API still need a concrete implementation sketch.

The existing constructor machinery supports `__new__`-only or `__init__`-only
class calls, but not the combination. Supporting both is an explicit prerequisite
for the builtin constructor work. The combined path calls `__new__` first and
calls initialization only when the returned object is an instance of the
requested class, preserving the original arguments and requiring `__init__` to
return `None`. Constructor caches must remain guarded against replacement of
either method. This is constructor-protocol work, not builtin payload policy.

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
