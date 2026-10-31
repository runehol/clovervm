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
type-directed through `sizeof(T)` for static layouts or `T::allocation_size_for(...)` for
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
`Bytes::allocation_size_for(...)`, and the native layout descriptor reports the same custom
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

## Trusted Python implementation module

The VM executes `src/bootstrap/builtin_implementations.py` in an immortal private module named
`_clover_builtin_impl`. It is absent from `sys.modules` and import discovery.
Its `__builtins__` binding is an empty dict, installed before execution, so a
missing dependency raises NameError rather than falling back to public builtins.
C++ explicitly seeds the builtin classes, constants, native functions, bridges,
and sys reference used by the source. Python functions defined there resolve
ordinary names such as `list` against these private bindings.

Bootstrap exports an explicit list of public Function objects to `builtins`.
The dict, list, and tuple installers read methods from the private module, and
membership fallbacks are retained by the VM. Helpers stay in the private
namespace. Exporting and installing preserve the original Function and
CodeObject identities, global caches, and method dispatch. CodeObjects retain
their defining module; the VM keeps it alive for the same lifetime as builtins
and sys. Caller-sensitive globals()/locals() keep their existing frame rules.

## Canonical builtin class lookup

The existing builtin registration pass records every public and internal class
in a VM-owned registry keyed by its interned name. Names and classes have VM
lifetime, and the registry is immutable after initialization, so reads need no
locking or per-thread cache. `ThreadState::class_for_builtin_name()` interns its
argument and delegates to the VM registry.

Native error helpers and compiler-generated exception constants use these
canonical identities. Rebinding or deleting a public builtin class name still
changes ordinary Python name lookup, but cannot redirect internal exceptions.

## List and Tuple Ordering

Builtin `list` and `tuple` own `__lt__`, `__le__`, `__gt__`, and `__ge__`.
They compare native payloads lexicographically, skipping identical elements,
truth-testing element equality, and returning the requested ordering result at
the first unequal index without coercing it to bool. Exhausted prefixes compare
by length. List callbacks may mutate either operand: the implementation retains
elements across equality callbacks, rechecks current lengths, and reloads the
unequal index before ordering, matching CPython 3.14.5.

Ordinary operator dispatch owns reflected precedence, subclass overrides,
`NotImplemented`, and lookup-cache invalidation. Exact builtin pairs have direct
trusted handlers; inherited calls still validate native receivers. Element
ordering uses VM-owned bytecode helpers, following the equality-helper pattern,
so it shares the regular operator protocol and cache invalidation. Cold and
cached reflectable binary dispatch prepare the native-call frame frontier before
invoking a trusted handler, keeping reentrant helper frames below live caller
slots.

Tuple construction and native subclass allocation use the existing constructor
thunks described in [constructor-thunks.md](constructor-thunks.md).

## Tuple Hashing

`tuple.__hash__` hashes the immutable stored payload, including for tuple
subclasses whose length, indexing, or iteration methods are overridden. Ordinary
special-method dispatch honors subclass hash overrides. Exact tuples use a unary
trusted handler with allocation, safepoint, exception, and Python-call effects;
existing lookup validity cells guard cached dispatch.

The native implementation retains the receiver and current element across each
`ThreadState::hash_value` call. Element hashes use the existing special-method
protocol and canonicalization, preserving equal supported numeric values' hash
compatibility and propagating element exceptions. Independent local accumulator
state supports callback reentry and callbacks that discard external references.
The unsigned 64-bit mixer follows CPython 3.14.5's tuple mixing and reduces its
result through Clover's existing canonical hash representation. Numeric hash
outputs need not match CPython. There is no tuple layout change or cached hash:
unlike CPython 3.14.5, repeated hashing invokes element hash methods again.

Empty and nested tuples can be dictionary keys when their elements are hashable.
General descriptor/callable support and recursion limits retain their existing
boundaries; tuple hashing adds no protocol or recursion machinery.

The existing class-creation policy does not implicitly set `__hash__ = None`
when a subclass defines `__eq__` without defining `__hash__`. Such subclasses
continue to inherit hashing in CloverVM, whereas CPython makes them unhashable.
This applies to other hashable builtin bases as well and remains a separate
class-policy repair. Explicit `__hash__ = None` and explicit hash overrides are
honored by the existing dispatch path.

## List Sorting

`list.sort(self, /, *, key=None, reverse=False)` sorts the native List payload
in place and returns None. The trusted bootstrap wrapper owns argument binding
and reverse truth conversion. The generic sorting algorithm runs entirely in
the trusted Python helper `_clover_list_sort_items`, using ordinary key calls,
ordering, and truth conversion. It preserves existing callable support; callable
instances and first-class bound-method keys retain their existing limitations.
Normal operator dispatch supplies scalar and container comparisons, subclass
reflection, and `NotImplemented` handling. Sorting does not add a separate
lexicographic comparator or general callable dispatcher.

List owns temporary sort state. It swaps its ValueArray into a temporary
`Owned<TValue<List>>`, leaving the receiver empty during key and comparison
callbacks. C++ enters the Python helper once with that exact detached List, the
key callable, and the normalized reverse flag. The helper's frame roots keys,
evaluated once in original order, along with its permutation temporaries. The
C++ sort boundary does not make separate calls for each key or comparison;
ordinary operator implementations retain their existing dispatch. The ValueArray
swap transfers size, capacity, and backing ownership without copying elements
or exposing unrooted backing storage across callbacks.

The Python helper performs a bottom-up merge sort on indices while leaving the
detached items untouched. It makes O(n log n) comparisons and uses O(n) temporary
space. All loops have bounds independent of comparison results, including
inconsistent comparisons. Reverse sorting reverses indices before and after
ascending sorting, preserving source order among equal keys. Only after all
comparisons succeed does the helper apply permutation cycles through exact-List
operations, which do not invoke user callbacks. Its saved item remains rooted
across interpreter safepoints during those cycles.
Key or comparison failures restore original order; this is a valid permutation,
but need not match CPython's post-failure partial ordering. Comparison sequences,
adaptive Timsort behavior, and ordering for inconsistent comparators are not
promised to match CPython. `sorted` remains unimplemented.

Successful List writes set a local mutation flag even when append/pop or
append/clear leaves the final length unchanged. Empty clear/reverse/extend and
failed index writes do not count as changes. Each sort saves and restores the
enclosing flag, so nested sorts of the temporarily empty list succeed without
erasing earlier callback mutations. On every Python exit path the original
storage is restored and callback-created contents are discarded. An existing
key/comparison exception takes precedence over mutation; otherwise mutation
raises `ValueError("list modified during sort")` after restoring sorted items.

JSON integration remains separate. In PR #35's `stdlib/json.py`, replace
`_sort_items_in_place(items)` in `_Encoder._value` with `items.sort()` and delete
both `_sort_items_in_place` and `_sort_items`. Keep the exact-list sharing and
materialization logic before the call. Sorting complete tuple rows preserves
the value tie-break for duplicate keys, including nested containers and malformed
rows compared before validation. A key-only sort would change that behavior.
