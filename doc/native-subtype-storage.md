# Native Subtype Storage

| Field | Value |
|---|---|
| Document type | Architecture contract |
| Status | Accepted |
| Implementation | Implemented |
| Scope | Native subclass storage, class-creation validation, allocation, and the current single-method constructor boundary |
| Owning layers | Object model owns layouts, Shapes, and class validation; memory layer owns allocation extents and reclamation; builtin families own payload construction; constructor thunks own call adaptation |
| Validated against | `3eca186b` (2026-10-11); latest implementation checks: 1,580 passed, one skipped |
| Supersedes | The earlier proposal and staged implementation discussion in this document |

Native subclasses keep the builtin's physical payload and their own Python class
identity. Additional attributes use common overflow storage when the native
layout has no inline attribute region. This document records the implemented
contracts; deferred features are tracked in [Small Tasks](small-tasks.md).
The representation decision is [D-0013](decision-log.md#d-0013-use-common-overflow-storage-for-native-builtin-subclasses).

## Storage And Ownership

Every Python-visible heap `Object` contains an initially null
`MemberHeapPtr<OverflowSlots>`. Allocate overflow storage only when needed.
`InlineSlotObject` adds the physical inline Value region, not a second overflow
pointer. Internal heap records without Python object identity, such as Shapes,
do not acquire the pointer.

```text
Object: heap header, Shape pointer, overflow-storage pointer
native builtin: Object followed by its native fields and payload
```

Native layout ID identifies the physical storage contract; Shape identifies the
actual Python class and attribute locations. A layout without an inline region
must have zero Shape inline capacity. Giving it positive capacity could overwrite
native payload fields. String length, tuple length, and integer digit count must
not determine attribute offsets or create different attribute Shapes for otherwise
identical subclass instances.

`instance_inline_slot_count`, exposed by
`ClassObject::get_instance_inline_slot_capacity()`, must agree with the instance
root Shape and allocator. Each concrete Object type declares
`kPredefinedInlineSlotCount`, including inherited predefined locations. This is a
minimum physical inline extent, not a GC release-span count or initialized-prefix
count. Transitions and clones preserve the selected layout and capacity.

| Type | Native layout | Minimum inline slots | Predefined fields |
|---|---|---:|---|
| Instance | Instance | 0 | Ordinary attributes; current initial capacity is ten |
| ModuleLoaderObject | ModuleLoaderObject | 3 | kind, name, path |
| ModuleSpecObject | ModuleSpecObject | 6 | name, loader, origin, submodule_search_locations, has_location, parent |
| Slice | Slice | 3 | start, stop, step |
| ExceptionObject | Exception | 1 | args |
| StopIterationObject | StopIteration | 2 | args, value |

Keep these native IDs and existing physical capacities distinct. Function,
ClassObject, and ModuleObject likewise retain their own layouts and metadata.
Matching slot counts do not imply matching payload or initialization contracts.

## Class Creation And Layout Compatibility

`ClassObject::make` owns the unpublished candidate, computes its C3 MRO, and uses
`select_instance_storage` before installing the final instance root Shape.
Inspect ancestors' instance layouts, not the layout of their ClassObject records.

- Ignore Instance ancestors when choosing builtin storage; ordinary mixins and
  object ancestry do not replace a native payload.
- Scan from most-derived to least-derived. Select one builtin storage provider;
  repeated equal native IDs are compatible.
- StopIteration extends Exception storage. Keep a selected StopIteration when
  later Exception ancestors appear. If Exception was selected first, upgrade
  provider, layout, and capacity together when StopIteration appears later.
- Reject every other distinct non-Instance combination with TypeError at class
  creation. List plus Dict and Slice plus Exception are conflicts.
- Use the provider's existing capacity and predefined descriptors. All-Instance
  ancestry uses ordinary instance capacity; never add ancestor capacities.

C3 handles inconsistent base ordering. A valid StopIteration-first Exception
combination needs no additional declared-base restriction. Neither this special
extension nor compatible mixins permits composing unrelated native prefixes.

Builtin ancestry gets a fresh root carrying the selected provider's descriptors,
occupied extent, and capacity. Preserve structural flags and descriptor
permissions while allowing subclass attributes independently of exact-builtin
attribute restrictions.

`SubclassPolicy` is a separate class contract. Builtin factories declare Allowed
or Disallowed; direct-base validation rejects prohibited bases such as bool even
where storage would fit. Constructor availability is also independent: permitting
a tuple subclass does not imply that tuple construction is implemented.

## Attributes And Cached Access

Overflow access operates through Object. Cached locations use a fixed inline
base or the common overflow anchor plus an element index. Cache guards prove the
receiver Shape and relevant lookup validity. Access must not calculate native
payload extent, follow its length to locate attributes, or retain raw field
addresses across allocation or mutation.

Shape permissions govern attribute mutation. A common overflow pointer does not
make exact builtins attribute-mutable, and immutable payloads do not require
immutable subclass attributes. Dict entries and instance attributes are separate:
`d["x"]` and `d.x` do not share storage.

The live SlotDict mapping is exposed wherever the Shape declares `__dict__`,
including existing exact list, dict, and tuple descriptors. Inline capability and
exact-builtin identity do not gate exposure; existing permissions still govern
writes and deletion. Immediate values retain their existing Shapes.

Keep current `__class__` assignment restrictions. Layout compatibility alone does
not authorize reassignment. Ordinary compatible user classes and the supported
ModuleObject path retain their behavior; Exception and StopIteration cannot be
reassigned to one another.

## Allocation And Reclamation

Native allocation validates allocator layout, requested class, root Shape,
physical capacity, and predefined minimum together. Bootstrap installation and
cached-Shape paths receive the same checks. The temporary self-labelled class
Shape is permitted only during initial ClassObject construction; final metaclass
installation validates identity. Internal assertions enforce these contracts;
Python-facing class creation and construction use explicit exception paths.

Generic Instance allocation must reject classes requiring another native layout.
Native allocation uses the requested class root Shape and establishes valid
payload state before Python initialization or observation.

Reclamation observes the common overflow pointer exactly once. Native fields,
size/release descriptors, and member-offset assertions must agree. Native extent
remains recoverable from layout descriptors and object-local counts; overflow
storage is a separate allocation. Future relocation must recover extent without
following Shape or class metadata awaiting relocation.

The common pointer adds one eight-byte field to former Object-only layouts.
Existing InlineSlotObject layouts have no duplicate field. Allocator rounding to
32-byte extents means the allocated size increase depends on boundaries.

Int subclasses always use native heap BigInt storage, even for zero and small
values. Exact integers retain SMI normalization. Layout compatibility is not an
exact-type test: audit identity-reuse paths separately. Requested subclass
construction preserves identity, while exact conversions and inherited arithmetic
or unary operations follow their own exact-result contracts.

## Constructor Paths

Constructor thunks currently support a resolved `__new__` or `__init__`, rather
than the combined Python protocol. They copy the selected method's signature,
defaults, and keyword adaptation, then forward normalized arguments.

The new-only path calls the resolved method with the requested class. Do not
replace an explicit or inherited method based solely on native layout. Any future
trusted specialization must recognize the resolved implementation and preserve
its applicability, arguments, errors, and lookup validity.

Without a resolved new method, thunk generation selects a dedicated allocation
opcode from the class's layout and root Shape. Handlers do not switch over native
layouts. Allocation consumes no Python constructor arguments; initialization then
receives them and must return None. Unsupported layouts fail during thunk creation
rather than falling through to generic storage.

| Family | Current construction path |
|---|---|
| Ordinary instances | CreateInstanceKnownClass; optional initializer |
| Exceptions | CreateExceptionKnownClass establishes empty args; native variadic initializer assigns args |
| StopIteration | CreateStopIterationKnownClass establishes empty args and None value; native initializer assigns args and first argument or None value |
| List | CreateListKnownClass creates empty storage; Python initializer clears and appends from its iterable |
| Dict | New-only Python wrapper uses a private native allocator and shared update implementation |
| Int | Native new-only conversion; subclass results always use heap BigInt storage |
| Float | Native new-only numeric conversion from float/int/bool, including heap numeric subclasses |
| Str | Native new-only conversion, honoring string subclass `__str__` overrides |
| Bytes | Native new-only empty, integer count, bytes, native list, and native tuple inputs |

Tuple construction is deferred. The storage contract and native tuple allocation
exist, but tuple has no constructor yet.

Builtin family code owns payload policy. Exception initializers are C++ callbacks
wrapped as Python Function objects, using variadic argument packing and existing
Member fields. BaseException initialization changes only args; StopIteration
initialization also resets value. Public descriptor permissions remain unchanged.
List initialization bypasses overridden clear/append through a private class
binding; argument errors precede mutation, and iteration failure leaves partial
contents. Dict population similarly bypasses overridden setitem. The trusted Python implementations share a private module with explicitly seeded
builtin references and disabled public builtin fallback. Only public functions
are exported to builtins; type methods and protocol helpers retain the private
module through their CodeObjects.

Exact-result reuse remains operation-specific. Exact str and bytes inputs can be
reused for exact targets; subclass targets receive fresh native storage. A custom
str conversion may return a string subclass, which exact-target construction
preserves.

The omitted bytes source defaults to an empty bytes object; explicit None is
rejected for exact and subclass targets.

Bytes count construction accepts builtin ints, bools, and int subclasses and
zeroes the final allocation directly. Counts are converted to signed 64-bit
before checking negativity, so out-of-range integers raise OverflowError and
representable negative counts raise ValueError. Positive counts above the SMI
length-storage limit raise OverflowError. Allocation failure retains the heap's
existing fatal behavior; recoverable MemoryError remains separate work.
Bytes conversion protocols and additional input forms are deferred.

Constructor caches retain MRO-and-contents validity guards. Adding, replacing, or
deleting new/init methods, including inherited ones, invalidates subsequent lookup
without cancelling an already executing thunk.

### Constructor Limitations

Combined new/init construction needs original call arguments retained and the two
methods bound independently. The current normalized single-method frame cannot
be forwarded to a second method with different defaults or keyword rules.
General call unpacking/forwarding is separate prerequisite work.

The current new-only path also omits initialization when new returns a subclass
instance with its own initializer. Future combined construction must test the
result with the existing `is_instance_of_known_class` operation, whose exact-class
case is fast, without adding another cache. Unrelated results skip initialization;
qualifying results use special-method lookup on their actual class after allocation.
Initializer mutation during new and coordinated default-method argument rules
must be handled by that future protocol. These tasks are in
[Small Tasks](small-tasks.md#follow-ups-from-native-constructor-work).

## Deferred Storage Features And Revisit Conditions

Explicit Python `__slots__` enforcement is deferred. Clover's automatic Shapes
remain the storage model; for now, `__slots__` is an ordinary class attribute.
Declared-name restrictions, latent descriptors, inheritance, and deletion/rebinding
semantics need separate design work.

Slack tracking may shrink only unused trailing inline capacity common to an entire
root Shape transition tree. Predefined fields and descendant-occupied locations
set its floor. Ordinary Instance initialized-prefix aux counts and physical-size
header cleanup remain part of future moving-GC work.

A tail after variable-sized native payloads would make attribute offsets depend
on payload length. Layout variants or wrappers add payload-access and identity
boundaries. A negative-offset preheader could preserve exact-builtin sizes, but
would change the allocation-start relationship used by pointer tagging and
object discovery on the 32-byte grid. These alternatives remain unselected.
Revisit only if measured exact-builtin memory cost justifies allocator/GC changes,
or extension types require a different explicit storage and tracing contract.

References:

- [Value representation](value-representation.md)
- [Builtin object model](builtin-object-model.md)
- [Native layout descriptors](native-layout-descriptors.md)
- [Refcounting and reclamation](refcounting-and-reclamation.md)
