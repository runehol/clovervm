# Builtin Object Model

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Implemented |
| Scope | Representation and bootstrap of builtin and runtime object types |
| Owning layers | Runtime object model, shapes, native layouts, and builtin types |
| Validated against | `ad1c5054` (2026-08-07) |
| Supersedes | N/A |

## Purpose

This document describes how builtin/runtime objects fit into the unified object
model.

The current direction is:

- every Python-visible object is an `Object`
- every Python-visible object has a Shape
- Python-visible type identity comes from VM-owned `ClassObject` instances and
  Shape-backed `__class__` slots
- native C++ layout identity is represented separately by `NativeLayoutId`
- builtin objects use the same slot protocol as user instances and classes when
  they expose Python-visible attributes

Internal runtime support records, such as scopes, backing arrays, validity
cells, and other non-Python-visible structures, may remain lower-level
`HeapObject`s.

## VM-Specific Builtin Type Objects

Builtin type objects are allocated while bootstrapping a particular
`VirtualMachine`. They are not process-global type identities.

Each VM currently owns builtin class objects for native layouts such as:

- `type`
- `object`
- `str`
- `bytes`
- `list`
- `dict`
- `function`
- `code`
- `range_iterator`

These type objects are inserted into the VM's builtin scope. Python code
observes them through ordinary builtin lookup.

At the Python level, the bootstrap relationships are:

```text
type.__class__ == type
object.__class__ == type
str.__class__ == type
```

## Shape Versus Native Layout

The runtime deliberately separates:

- Python-visible type identity
- native C++ layout identity

Python-visible identity is represented by `ClassObject` and Shape-backed
`__class__` metadata. Native layout identity is an implementation detail used
for casts, allocation, fast dispatch, reclamation, and object-size queries.

The runtime keeps these facts on the appropriate base:

- `HeapObject` stores `NativeLayoutId native_layout_id_` for native layout
  dispatch.
- `Object` stores `Shape *shape` for Python-visible class identity.
- `SlotObject` stores overflow-slot state for layouts that physically support
  inline or overflow attributes.

Code that needs Python semantics should ask for the object's class/Shape. Code
that needs native layout should use `native_layout_id()` or typed conversion
helpers.

## Native Layout Id

`NativeLayoutId` replaces the old static `Klass`-style runtime type metadata.

Examples of native layouts include:

- `Instance`
- `ClassObject`
- `String`
- `Bytes`
- `List`
- `Dict`
- `Function`
- `RangeIterator`
- `Scope`
- `Shape`
- `CodeObject`
- VM array backing objects

Native layout ids are appropriate for:

- `TValue<T>` checked construction
- interpreter dispatch for callable/runtime object kinds
- low-level subscript dispatch
- debug and assertion-only layout checks
- allocation and finalization support

Native layout ids are not returned by `obj.__class__`.

## Builtin Attribute Policy

Builtin objects can be Shape-backed before they support arbitrary user
attributes. The policy should be explicit per builtin type:

- fixed-shape builtin values that reject arbitrary writes
- builtin values with fixed native fields plus optional overflow attributes
- ordinary user instances with dynamic attribute add/delete

The policy belongs in Shape flags and shared object helpers, not in ad hoc
attribute lookup branches. For example, a fixed builtin instance Shape can use
`DisallowAttributeUpdates` and `DisallowAttributeAddDelete` to reject unsupported
ordinary writes through the generic attribute path.

Current builtin instance semantics are intentionally conservative: `__class__`
loads should go through the shared path, while arbitrary writes are rejected
until the builtin type explicitly supports them.

## Class Object Layout

`ClassObject` is Python-visible and therefore derives from `Object`.

It has fixed Shape-backed metadata slots:

- `__class__`
- `__name__`
- `__bases__`
- `__mro__`

It also has a fixed inline class-attribute area. The current size is deliberately
larger than the early prototype because class objects are few but often carry a
substantial namespace. Instance root Shape, lookup validity-cell state, and the
default instance inline slot count are C++ fields associated with class-object
behavior, not Python-visible attributes directly.

## Runtime Layout Metadata

Heap-object release and opaque size queries are described by native-layout
descriptors keyed by `NativeLayoutId`. The old packed heap-layout header is gone.

Release descriptors describe one of:

- an empty or fixed contiguous owned-`Value` span
- a dynamic span counted from an SMI field
- a dynamic span counted from `HeapObject`'s auxiliary count
- a custom deallocator

Object-size descriptors provide the cold
`object_size_in_bytes(const HeapObject *)` query. Allocation itself remains
type-directed through `sizeof(T)` for static layouts or `T::size_for(...)` for
dynamic layouts.

Some internal non-`Object` records still have native fields and custom value
offsets, but they participate in the same descriptor system without carrying the
full Python-visible `Object` header.

## Immutable Numeric Builtins

`Float` is a fixed-size `Object` containing one C++ `double` and no
float-specific child `Value`s. It remains a boxed pointer value at runtime;
mixed numeric adaptation, Python-visible arithmetic, comparison, and result
construction belong to the float builtin's native and trusted handlers rather
than to `Value` tagging or the generic object model. Compiled code may use an
explicit unboxed F64 representation internally, but boxing remains required at
Python-visible identity boundaries.

Arbitrary-size integer representation and SMI transitions are documented in
[BigInt Representation And Arithmetic](bigint.md).

## Variable-Size Immutable Builtins

`Bytes` is the representative variable-size immutable builtin. It is an
`Object` with a VM-owned `bytes` class and `NativeLayoutId::Bytes`. Its byte
count is a traced SMI member and its raw bytes occupy untraced inline tail
storage; the count, not a terminator, is authoritative. Allocation uses
`Bytes::size_for(...)`, and the native layout descriptor reports the same custom
extent for reclamation and heap inspection.

Call-scoped native helpers expose payloads as `std::span<const uint8_t>`.
Constructed `Bytes` objects copy that payload into owned storage, so no span or
external buffer becomes object lifetime state. Python-visible indexing,
slicing, concatenation, comparison, containment, hashing, representation, and
the implemented search methods remain owned by `bytes.cpp`; exact builtin fast
paths may use trusted handlers without changing those semantics.

The implemented constructor accepts no source, an existing bytes object, or an
exact list/tuple of integer byte values. General iterable construction,
`bytes(n)`, arbitrary `__index__` conversion, encoding from strings, and public
native raw-byte APIs remain separate semantic/API work rather than hidden
extensions of the storage contract.

## Bootstrap Outline

Builtin type bootstrap is centralized in VM initialization:

1. Allocate placeholder root type objects for `type` and `object`.
2. Install the `type.__class__ == type` cycle.
3. Populate readonly metadata slots such as `__name__`, `__bases__`, and
   `__mro__`.
4. Allocate the remaining builtin type objects.
5. Create root Shapes for builtin instance layouts such as `str`, `list`, and
   `dict`.
6. Register builtin classes by `NativeLayoutId`.
7. Insert builtin type objects and builtin functions into the VM's builtin
   scope.
8. Finish bootstrap before any Python bytecode can observe partially
   initialized objects.

## Remaining Questions

- Which builtin types should support arbitrary user attributes, and when?
- Which Python-visible builtin objects need fixed native fields exposed through
  ordinary descriptors rather than direct C++ helpers?
- How much of descriptor protocol for immutable builtin types should be marked
  through `ShapeFlag::IsImmutable` after bootstrap?
- Which internal runtime records should stay non-Object `HeapObject`s forever?
