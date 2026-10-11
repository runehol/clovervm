# Native Subtype Storage

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Partial |
| Scope | Native builtin subclass storage, class-creation validation, and constructor specialization; explicit Python slot declarations remain deferred |
| Owning layers | Runtime object model, shapes, native layouts, allocation and reclamation; builtin families own construction and payload semantics |
| Validated against | Parent `49f0b342` and the Stage 8d working tree (2026-10-11); full debug checks: 1,580 passed, one skipped; focused release constructor measurements; combined constructor orchestration remains pending |
| Supersedes | The previous investigation in this document |

The accepted direction is a common overflow-storage pointer in every
Python-visible heap `Object`, native heap integer storage for `int` subclasses,
and overflow-only additional attributes for builtin families that currently
lack inline storage. Existing `InlineSlotObject` families retain their regular
storage mechanism and separate native layout IDs. This decision was agreed on
2026-10-10 and refined on 2026-10-11. The proposed shared native ID was abandoned
in favor of preserving native field and initialization contracts.

The staged checklist is in the
[implementation plan](native-subtype-implementation-plan.md).

## Common Storage

The overflow-storage pointer lives in `Object` as
`MemberHeapPtr<OverflowSlots>`, preserving its ownership and one-word storage.
The C++ `InlineSlotObject` base adds no second pointer; it marks the physical
presence of inline `Value` slots. Overflow access operates through `Object`.
Internal heap records without Python object identity, such as Shape records,
do not acquire the pointer. It is initially null; overflow storage is allocated
only when needed.

A layout without the inline-capable base must declare zero inline attribute
capacity in its instance Shape. Root-shape construction, transitions, and clones
must preserve that invariant. A positive capacity would let attribute access
overwrite native fields. Existing inline-capable layouts must likewise keep
Shape capacity within the actual physical slot region.

## Native Layout Selection And Multiple Inheritance

Keep the existing native layout IDs. In particular, `Instance`, `Slice`,
`Exception`, `StopIteration`, `ModuleLoaderObject`, and `ModuleSpecObject` remain
separate. The C++ base name `InlineSlotObject` does not introduce a shared
`NativeLayoutId::InlineSlotObject`.

User-class creation now selects storage from the completed C3 MRO before
installing the final instance root Shape. Instance-only ancestry retains the
ten-slot default. Inspect the classes' instance layouts, not the native layout
of the `ClassObject` records themselves:

- Ignore `Instance` entries when selecting builtin storage. Ordinary Python
  mixins and the universal `object` base do not override a builtin payload.
- Select at most one builtin layout. Repeated occurrences of that same layout
  through compatible subclasses are allowed.
- Reject mixing distinct builtin storage requirements with `TypeError`, such
  as Slice plus Exception or List plus Dict. Matching inline slot counts do not
  establish compatibility: both families may require their own field at slot 0.
- A builtin's own established ancestry is not a second independently selected
  layout. In particular, StopIteration's inherited Exception ancestry is
  permitted, with StopIteration storage selected. This is an explicit existing
  native extension, not permission to combine unrelated builtin bases.
- Scan the MRO from most-derived to least-derived. Accept equal non-Instance
  IDs, plus the explicit case where the selected layout is StopIteration and
  the later ancestor layout is Exception. Keep the selected provider and
  capacity unchanged. If Exception is selected and StopIteration appears later,
  upgrade the provider, layout, and capacity together to that StopIteration
  entry. Reject every other distinct non-Instance ID.
- Do not impose a separate declared-base restriction. A valid
  `(StopIteration, Exception)` declaration selects StopIteration storage safely;
  the reverse order already fails ordinary C3 MRO construction. Dense native
  IDs and indexed descriptor/class tables remain unchanged; no inheritance bit
  encoding is introduced.
- Use the selected builtin layout's existing inline capacity and predefined
  field contract. It is zero for layouts without inline attribute storage.
  If all entries use `Instance`, use the ordinary-instance capacity policy
  (currently ten slots, not an additive sum of ancestor capacities).

Thus "one builtin layout in the MRO" means one selected storage provider after
accounting for its own established builtin ancestry, not a literal requirement
that all MRO entries have one identical non-Instance ID. Multiple inheritance
cannot compose two native prefixes. Subclass eligibility remains a separate
check, even where physical storage would otherwise fit.

CPython 3.14.5 rejects `(Exception, StopIteration)` because its MRO is
inconsistent, but permits `(StopIteration, Exception)`. Clover accepts the
valid order through the MRO storage rule. A user Exception subclass can precede
StopIteration in a valid MRO, requiring the explicit upgrade described above.
CPython also rejects `__class__` assignment
between exact Exception and StopIteration objects in either direction.

Root Shapes, transitions, and clones must preserve the selected native layout
and capacity. Allocation validates agreement with that class contract and the
concrete allocator; generic `Instance` allocation must not construct a class
that requires builtin storage. Native constructors remain responsible for
initializing their predefined fields.

`ClassObject::make` owns the unpublished candidate during this work. Its initial
generic root is provisional; builtin ancestry gets a fresh root carrying the
selected provider's predefined descriptors, occupied slot extent, and capacity.
Structural flags are preserved, while exact-builtin shape restrictions are
removed to allow subclass attributes. Descriptor permissions remain intact.
Generic `Instance` construction raises `TypeError` for a selected native layout
until the corresponding builtin allocator is supported.

### Class Reassignment

Preserve the current restrictions on `__class__` assignment. Do not broaden
assignment to builtin families merely because they have inline slots or the
common overflow pointer. Layout compatibility is necessary, but not sufficient;
capacity and predefined-field contracts must remain valid. Ordinary compatible
user classes and the existing ModuleObject path retain their supported behavior.
Exception and StopIteration remain distinct layouts and cannot be reassigned to
one another: a one-slot exception cannot safely become a two-slot StopIteration.

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

Native layout identity continues to distinguish the existing native storage
contracts, while the Shape supplies the object's actual Python class identity.
Subclassing must not replace a builtin payload with generic `Instance` storage
or erase subclass identity by returning an exact builtin value.

### Existing Inline Slot Contracts

Retain the following separate native layouts and predefined locations:

| Concrete type | Native layout ID | Minimum inline slots | Predefined attributes |
|---|---|---|---|
| `Instance` | `Instance` | 0 | Ordinary user attributes; current default capacity is ten |
| `ModuleLoaderObject` | `ModuleLoaderObject` | 3 | `kind`, `name`, `path` |
| `ModuleSpecObject` | `ModuleSpecObject` | 6 | `name`, `loader`, `origin`, `submodule_search_locations`, `has_location`, `parent` |
| `Slice` | `Slice` | 3 | `start`, `stop`, `step` |
| `ExceptionObject` | `Exception` | 1 | `args` |
| `StopIterationObject` | `StopIteration` | 2 | `args`, `value` |

These builtin layouts retain their existing physical inline capacities and
size/release descriptors. Added subclass attributes spill to common overflow
storage once that fixed region is full. `Function`, `ClassObject`, and
`ModuleObject` likewise retain their separate IDs, capacities, and native
metadata. The class's `instance_inline_slot_count`, exposed through
`get_instance_inline_slot_capacity()`, must agree with its root Shape and the
selected allocation contract.

Each concrete `Object` subclass declares
`static constexpr uint32_t kPredefinedInlineSlotCount` for allocation validation.
The count includes inherited predefined locations, and is zero where none
exist. Exception `.args` remains a typed tuple field; StopIteration `.value`
remains an additional initialized field. A raw count of available slots alone
does not establish these initialization guarantees.

The proposal to merge these IDs and make all six families dynamically sized is
withdrawn. Ordinary `Instance` size recovery currently uses an initialized-prefix
aux count; its physical-capacity/header correction remains deferred to the
moving-GC header work, rather than being folded into this storage selection.

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
inline locations. These existing `InlineSlotObject` families continue using their
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

Stage 4c exposes the existing live `SlotDict` mapping wherever a heap object's
Shape declares `__dict__`, including exact list, dict, and tuple objects.
Neither inline-slot capability nor exact-builtin identity gates that descriptor.
Existing Shape and descriptor permissions govern mapping updates and deletion;
exposing the mapping does not grant additional mutation rights. No separate
exposure flag or subclass-only descriptor installation is needed. Immediate
values retain their existing Shapes and are outside this heap-object change.

## Integer Subclasses And Immediate Values

An `int` subclass must always be a heap object, including for values that fit in
an SMI. Use native BigInt storage with the requested subclass identity. The int class
records `NativeLayoutId::BigInt` for its heap allocation contract; tagged SMI
values keep their existing immediate representation. Its
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
only `Object`. Existing `InlineSlotObject` layouts must not pay for a duplicate field.
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

Construction continues to support either `__new__` or `__init__`, not both
in one class call. Preserve the existing new-only path: call the resolved
method with the requested class and return its result directly.

When no `__new__` is resolved, the constructor thunk builder inspects the
class's selected native layout and instance root Shape. It emits a dedicated
zero-argument allocation opcode for that layout, carrying a class constant.
Here zero-argument means no Python constructor arguments are consumed by the
allocator; `cls` still supplies subclass identity, root Shape, and inline slot
capacity. The builder selects the opcode once; opcode handlers must not contain
a switch over native layouts.
In stage 5a, unsupported layouts raise TypeError during thunk construction,
before constructor argument binding or initializer execution, using the existing
allocation-error message. Class/root capacity agreement is validated during
emission; the runtime Instance guard remains as a defensive check.

Stage 5b adds `CreateExceptionKnownClass`. It allocates Exception storage with
the requested class and root Shape, validates the fixed one-slot capacity, and
initializes `.args` to an empty tuple. User initializers receive constructor
arguments afterwards; builtin argument initialization remains stage 6.
Stage 5c adds `CreateStopIterationKnownClass` with the distinct two-slot native
layout. Its default allocator initializes `.args` to an empty tuple and `.value`
to None, preserving the requested subclass identity.

The implementation plan separates thunk-time allocator selection with
Instance-only support (5a), Exception default allocation (5b), and StopIteration
default allocation (5c).

Start with `Instance`, `Exception`, and `StopIteration`. Keep the existing
`CreateInstanceKnownClass` name, which identifies the concrete native Instance
layout, and add separate exception allocation operations. Allocate the class's
required inline capacity
within the validated physical bounds, preserving distinct native headers and
predefined field locations. Allocation must establish valid default state:
exceptions have an empty tuple in `.args`, and StopIteration also has `None` in
`.value`. The existing init-only thunk then calls `__init__`, when present, with
its normalized arguments and requires a `None` result. C++ initializers wrapped
as Python `Function` objects own payload initialization.
The existing variadic wrapper packs positional arguments into a tuple; the
initializer assigns native fields directly while their Python descriptors retain
their existing permissions. Keywords are rejected by the normal argument binder.

Stage 6 is limited to C++ Exception-family and StopIteration `__init__`
implementations wrapped as Python functions and argument-bearing instantiation
and subclassing tests. Mutable builtin
allocation and initialization are planned by family in stage 7; immutable
constructors are planned by family in stage 8.

For list, stage 7a uses a dedicated empty native allocator and the fixed Python
initializer signature `__init__(self, iterable=(), /)`. Its body calls builtin
clear/append through a persistent private bootstrap alias of the list class.
This bypasses subclass overrides and public builtin rebinding without closures
or extra signature parameters. The private alias remains while the initializer
needs it; closure capture will replace it when supported. Native list attributes
remain in overflow storage. Argument errors precede mutation; a valid call clears
before iteration and preserves partial contents when iteration raises.

Dict remains on the existing new-only path in stage 7b. The native bootstrap
allocator accepts validated dict subclasses and preserves their root shape. Its
Python `__new__` replacement captures that allocator and the shared update
implementation through private bootstrap bindings, then populates the new native
object. Shared update inserts through builtin dict `__setitem__` so subclass
method overrides do not intercept native population. No dict initializer or new
allocation opcode is introduced; combined constructor orchestration is deferred.

Stage 8a makes native int `__new__` subclass-aware using its existing new-only
thunk. Exact class calls retain the common conversion fast path; non-exact
targets use a slow helper for subtype/layout validation and CPython-style errors.
Int subclass values always use fresh heap BigInt storage, including small values
and zero, with their requested class root shape and zero inline capacity. Exact
int conversion and inherited unary plus/abs discard subclass identity. Trusted
abs remains guarded by the exact int root shape. Existing input conversions are
retained; no new opcode or general constructor orchestration is introduced.

Stage 8b adds numeric float `__new__` on the existing new-only path. It accepts
zero or one float/int/bool input, including heap numeric subclasses. Exact target
calls reuse only exact Float inputs; subclasses receive fresh Float storage with
their requested class root and zero inline capacity. Convert to a double before
allocation and retain checked integer overflow. String parsing and user conversion
protocols are outside this slice. Existing exact-result unary and arithmetic
operations and trusted exact-root guards are preserved.

Stage 8c extends existing string construction through native `str.__new__`.
Exact-str/small-int construction keeps the direct decimal formatter. A separate
subclass helper validates ancestry and layout, converts the input, retains the
converted String across allocation, and copies its characters into storage with
the requested class root shape. String subclass inputs honor `__str__` overrides;
inherited `str.__str__` returns an exact string, while custom conversion results
keep their permitted subclass identity for exact-target construction. No new
encoding/decoding forms or constructor orchestration are introduced.

Stage 8d extends native bytes construction for existing empty, bytes, list, and
tuple inputs. Exact target calls reuse only exact Bytes objects; subclass inputs
are copied into exact storage. A separate validated helper allocates fresh Bytes
storage with the requested subclass root shape and zero inline capacity, using
the existing integer-element validation for list/tuple inputs. Additional
attributes use common overflow storage. Custom conversion protocols, new input
forms, and the existing explicit-None/default distinction remain deferred.

Only native payloads that can be initialized after allocation use this path.
Immutable payloads, including int, float, str, bytes, and tuple, must be
constructed through `__new__`. Mutable subclass attributes do not make an
immutable native payload eligible. Unsupported and non-instantiable layouts
must reject construction rather than fall through to generic `Instance`
allocation. No argument-bearing allocation or general call-unpacking machinery
is introduced by this work.

`ClassObject` stores an explicit `SubclassPolicy`. Ordinary classes permit
subclasses; builtin factories declare `Allowed` or `Disallowed` independently
of layout, visibility, and constructor availability. Direct-base validation
raises `TypeError` for prohibited bases, including `bool` even though it inherits
from `int`. Internal loader/spec classes are prohibited for now.

Subclass eligibility is separate from storage and construction capability.
Having an overflow pointer does not make `bool`, `NoneType`, or the other
non-subclassable singleton types legal bases. Native payload compatibility must
also be validated independently during class creation, as described above.
Native C++ allocation asserts agreement between the allocator's layout, the
requested class, and its Shape capacity. Bootstrap installation and cached
Shapes receive the same checks. The temporary self-labelled class Shape is
allowed only at initial ClassObject construction; metaclass installation
validates its final identity. These assertions enforce internal contracts;
Python-facing construction continues to use explicit exception paths.

For the new-only path, future trusted specialization is selected by the
resolved method and its applicability contract. Native-layout selection applies
only to the no-new allocation path; it must not bypass an explicit or inherited
`__new__`. Builtin payload policy remains owned by each family; the Exception and
StopIteration initializers live in the exception runtime implementation and
write native fields through their existing `Member` owners. The inherited
BaseException initializer updates only `.args`, including when explicitly
called on a StopIteration; the StopIteration initializer also resets `.value`
to the first positional argument or None. Direct calls validate native receiver
layouts before writing fields.

Constructor thunks and their call caches retain the existing MRO-and-contents
validity guard. Replacing, adding, or deleting `__new__` or `__init__`, including
inherited methods, invalidates subsequent constructor lookup without cancelling
a thunk already executing. Inherited methods count when deciding whether the
call is new-only, init-only, or an unsupported combination.

### Argument Binding And Deferred Combined Construction

Single-method thunks continue to copy the selected method's signature, defaults,
and keyword remapping and forward normalized parameters. They do not need to
retain the original call for a second independently bound method invocation.
The zero-argument allocator does not consume or bind the initializer arguments.

Combined `__new__`/`__init__` construction remains deferred until general
`funcall(*args, **kwargs)` forwarding is available. That is a separate substantial
feature, not a prerequisite for the native allocators and initializers above.
The current new-only path also continues to omit initialization if `__new__`
returns a subclass instance with its own initializer; that limitation belongs
to the deferred combined protocol.

Future combined construction must preserve original arguments, bind the methods
independently, test allocation results with `is_instance_of_known_class`, and
resolve initialization from the actual returned class after allocation. An
unrelated result skips initialization. Initializer replacement during allocation
and coordinated default-method argument rules are future protocol work.

Current verification covers layout-specific opcode selection, allocation sizes,
valid default fields, exact and subclass identity, initializer arguments and
non-`None` results, unsupported layouts, and constructor lookup invalidation.
Run opcode frame checks for interpreter changes and measure allocation paths
when changing their performance. Immutable family construction is verified on
the existing new-only path, separately from allocator-plus-init families.

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
