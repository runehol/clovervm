# Native Subtype Storage

| Field | Value |
|---|---|
| Document type | Investigation |
| Status | Proposed |
| Implementation | Not started |
| Scope | Storage and construction options for Python subclasses of native builtin types |
| Owning layers | Runtime object model, native layouts, allocation, class construction, and extension API |
| Validated against | N/A |
| Supersedes | N/A |

This note records the design problem around subclassing native builtin types.
It is intentionally not a decision. The immediate dictionary-shape work should
not solve this problem by accident.

## Problem

Some Python builtin values are represented by compact native layouts in
CloverVM. Exact builtin instances can be fast and small because their storage is
known up front:

- small `int` values are inline SMIs;
- `bool` values are inline singletons/tags;
- `str`, `list`, and `dict` are heap native objects with type-specific payloads;
- ordinary instance attributes use `SlotObject` storage, with the overflow
  pointer placed before the subclass payload.

Subclassing native builtins introduces a different requirement. A subtype
instance needs Python object identity and class identity from the subtype, while
also behaving like the builtin base type. It may also need instance attributes
or slots. For inline values such as SMIs there is nowhere to attach a class
pointer, `__dict__`, or slot storage. For heap native objects, adding a slot
pointer to every exact builtin instance may be too expensive.

The central question is where subtype attribute storage lives without taxing
exact builtin values or creating a cross product of native classes.

Construction exposes the same question from another direction. A class call
needs to distinguish:

- Python-visible construction policy in `__new__` and `__init__`;
- internal physical allocation for the concrete requested class;
- the instance layout and tracing metadata consumed by the collector.

CPython represents these separately with `tp_new`, `tp_init`, and the
non-Python-visible `tp_alloc` slot. A native `tp_new` normally asks the concrete
class's `tp_alloc` to allocate storage, so an inherited constructor can allocate
a subtype rather than an exact base object. CloverVM does not need to copy those
slots directly, but a builtin `__new__` that hard-codes allocation of an exact
C++ type would leave the same subtype problem unresolved.

## CPython Behavior

CPython permits ordinary instance attributes on plain subclasses of several
native builtin types:

```python
class D(dict):
    pass

d = D(a=1)
d.x = 42
```

`d["x"]` and `d.x` are separate storage systems. The mapping entries remain in
the `dict` payload, while `d.x` lives in the subtype instance attribute storage.

Observed behavior on CPython:

- `class D(dict): pass` gets an instance `__dict__`.
- `class L(list): pass` gets an instance `__dict__`.
- `class S(str): pass` gets an instance `__dict__`.
- `class I(int): pass` gets an instance `__dict__`; CPython represents ints as
  heap objects, so this is not the same representation problem as CloverVM's
  inline SMIs.
- `class T(tuple): pass` gets an instance `__dict__`.
- `bool` is a subclass of `int`, but `bool` itself is not subclassable.
- Nonempty `__slots__` are accepted for `str`, `list`, and `dict` subtypes, but
  rejected for `int` and `tuple` subtypes.

CPython makes this work with subtype layout machinery around `PyObject` and
`PyTypeObject`. Builtin payloads such as `PyDictObject` remain the C-level
prefix of suitable subtype instances, while managed instance dictionaries or
extra slot storage are attached by the type system. CloverVM does not need to
copy this architecture, but it must preserve the visible distinction between
payload state and subtype attributes if it supports these subclasses.

References:

- CPython `PyDictObject` definition:
  <https://github.com/python/cpython/blob/main/Include/cpython/dictobject.h>
- CPython dictionary implementation:
  <https://github.com/python/cpython/blob/main/Objects/dictobject.c>
- CPython type and subtype layout machinery:
  <https://github.com/python/cpython/blob/main/Objects/typeobject.c>

## CloverVM Constraints

Inline values cannot carry subtype state. An `int` subclass instance cannot be
an SMI, because it needs heap identity, a subtype class pointer, and possibly
attribute storage. Exact SMIs can remain exact `int` values, but `MyInt(5)` must
be represented by a heap object if `MyInt` is a Python subclass.

Heap native values have a different tradeoff. `str`, `list`, and `dict` could
carry `SlotObject` storage directly, but that would add an overflow-storage
pointer to every exact instance. For `str` this is especially unattractive: exact
strings are pervasive, and future UCS1/UCS2/UCS4 string layouts would multiply
any slot-capable subclass variants.

CloverVM already has a useful precedent: one Python-level type relationship does
not have to mean one native layout. Exact fast paths can guard exact shapes or
native layouts, while semantic paths can accept a broader Python-level family.
That pattern may be the right foundation for native subtypes.

## Candidate Construction Boundary

One possible boundary is an internal instance-allocation plan stored on every
`ClassObject`. All class objects would retain the same C++ representation; the
plan would describe future instances of that class. Conceptually it could
answer:

- whether the class is instantiable;
- which allocator constructs its native payload;
- whether and how the layout may be extended by a subtype;
- where Python-visible attribute storage may live;
- how allocation extent and managed fields are reported to the collector.

Python-visible `__new__` could call this internal allocator, while Python code
would have no direct `__alloc__`-like operation. This is an option rather than a
settled API. In particular, it remains open whether the plan belongs directly
on `ClassObject`, in a reusable instance-layout record, or in existing metadata
extended for this purpose.

`NativeLayoutId` should not silently serve as this plan. It identifies how to
interpret an object that already exists, but does not by itself state whether
the class is instantiable, how arguments establish a valid initial state, or
how a subtype extends the payload.

## Shape-Relative Physical Storage

Shapes are tied to physical object layout. This permits an attribute cache to
guard the exact Shape and cache a resolved physical access plan rather than a
logical slot number relative to one universal `SlotObject` base. Candidate
plans include:

```text
direct:   Value at receiver + byte_offset
overflow: OverflowSlots pointer at receiver + anchor_offset, then element_index
```

This could allow fixed-size native objects to place direct slots and an
overflow anchor wherever their finalized layout permits. The hot path would
reload the receiver and apply cached offsets; it must not cache a raw field
address that movement would invalidate.

An exact Shape guard is sufficient only while every Shape belongs to one
physical instance layout. Reusing a Shape across layouts with different offsets
would require a separate layout guard and would weaken this option.

## Option 1: Make Heap Native Builtins Slot-Capable

Change native heap classes such as `Dict`, `List`, and `String` to inherit from
`SlotObject`, probably with zero inline slots for exact builtin instances and
lazy overflow storage.

Advantages:

- One native class per builtin family.
- Subtype instances reuse the exact same payload implementation.
- Attribute lookup uses the existing shape and slot machinery.
- Exact/subtype distinction can be mostly shape-based.

Costs:

- Every exact instance pays for the slot/overflow-storage pointer.
- Exact builtin object sizes change on hot types.
- It does not solve inline types such as SMI-backed `int`.
- It may become awkward for future multiple string payload layouts.

This option is simple locally, but it taxes the common case.

It does have one implementation advantage: the existing common slot base and
overflow-anchor position remain available to attribute access and tracing. The
memory cost should therefore be weighed against the extra layout and GC
machinery required by the alternatives, rather than dismissed in isolation.

## Option 2: Separate Slot-Capable Native Subtype Layouts

Keep exact native builtin layouts compact. Add subtype-specific native layouts,
for example `DictSubtypeInstance`, `ListSubtypeInstance`, and
`StringSubtypeInstance`, each with slot storage plus the relevant native payload.

Advantages:

- Exact builtins stay compact.
- Native subtype support is explicit and easy to guard.
- Exact fast paths remain exact-layout checks.

Costs:

- Duplicates payload classes or requires shared payload structs.
- Creates a cross product with future representation variants, especially for
  strings.
- C++ APIs must distinguish exact payload layouts from Python-level compatibility
  layouts.

This option is attractive for preserving exact builtin performance, but the
class explosion risk is real.

One variant avoids a separate C++ subclass for every fixed-size native family.
The concrete class's instance-layout plan could allocate extra tail bytes after
the ordinary native prefix:

```text
[fixed native payload][overflow anchor][direct Value slots]
```

The native prefix remains usable by existing builtin operations, while the
Shape records physical offsets into the tail. Exact builtin instances retain a
smaller allocation. This variant still needs object-local extent metadata and a
trace/update plan for the appended references. It applies only when the native
payload has a fixed end.

## Option 3: Generic Native Subtype Wrapper

Represent native subtype instances with one generic slot-capable wrapper:

```cpp
class NativeSubtypeInstance : public SlotObject
{
    Value payload;
};
```

The wrapper owns Python identity, shape, class, and attributes. The payload owns
the exact builtin representation:

- `MyInt(5)` payload could be `Value::from_smi(5)`;
- `MyStr("x")` payload could be an exact `String`;
- `MyDict({...})` payload could be an exact `Dict`;
- `MyList([...])` payload could be an exact `List`.

Advantages:

- One generic system handles inline and heap native bases.
- Exact builtin layouts stay compact.
- Avoids `SlotCapableUCS1String`, `SlotCapableUCS2String`, etc.
- The subtype tax is paid only for subtype instances.

Costs:

- Every operation must preserve the wrapper when object identity or class
  matters.
- Payload extraction becomes dangerous if helper names are too casual.
- Native operations must decide deliberately whether they accept exact payloads
  only or native subtype wrappers too.
- Mutable payloads such as list and dict need clear ownership and mutation
  rules through the wrapper.
- C API and internal helper boundaries become more explicit.

This is the most general option, but it is also the easiest to get subtly wrong.
The API must make wrapper-versus-payload boundaries impossible to miss.

Wrappers may be particularly relevant for native representations with a
variable inline tail. For an allocation such as:

```text
[Tuple header][N inline tuple items]
```

storage appended after the native payload has an instance-dependent offset, so
one Shape cannot cache a constant positive offset to it. A wrapper or a
family-specific subtype representation can instead keep attribute storage at
fixed offsets and reference an exact immutable tuple, string, or inline-value
payload. This preserves compact exact values but introduces subtype-aware
payload access throughout the owning builtin family.

## Option 4: Side Storage for Native Subtypes

Keep exact native object layouts unchanged and attach subtype attributes through
external side storage keyed by object identity.

Advantages:

- Exact object layouts stay compact.
- Native payload objects remain direct instances of their existing C++ classes.
- Avoids a payload wrapper for heap-native subtype instances.

Costs:

- Requires side-table lifetime, GC, and reclamation integration.
- Adds attribute lookup indirection.
- Does not naturally handle inline SMIs unless they are boxed first.
- Makes object identity and side-storage cleanup a cross-cutting concern.

This option avoids per-object layout cost, but the lifetime machinery is likely
too invasive to be a quick solution.

## Option 5: Prefix Storage Before Variable Native Payloads

Another representation would reserve subtype-only storage before the native
object pointer:

```text
[subtype slots or overflow anchor][native header][variable native payload]
                                    ^ object pointer
```

This gives attribute storage constant negative offsets without adding fields to
exact builtin instances. CPython's managed dictionary storage uses a related
preheader technique.

For CloverVM this conflicts with important current and planned memory-system
invariants. The heap currently treats `HeapObject *` as the allocation and
bitmap object start, pointer tagging requires strong alignment, and a copying
collector benefits from copying and forwarding an allocation beginning at that
same address. Supporting an interior object pointer would require allocation,
discovery, object-size, reclamation, forwarding, and relocation paths to recover
the hidden prefix. This remains an option, but its cross-cutting cost makes it
unattractive unless other designs fail.

## Variable Allocation And GC Extent

Native subtype storage also affects the copying-GC extent contract. The
collector must recover the complete allocation extent from the object header,
fields in the same allocation, and immutable descriptor tables. It must not
follow `Shape` or `ClassObject` while deciding how many bytes to copy: those
objects may themselves be awaiting relocation or repair.

This leaves several possible extent representations:

1. Store an exact allocated size in every movable object. This makes evacuation
   simple but grows the common header, needs a policy for sizes beyond the
   compact encoding, and partly recreates CloverVM's older compact/expanded
   layout-and-size header scheme. It still does not describe tracing or custom
   relocation by itself.
2. Continue deriving extent from `NativeLayoutId` descriptors plus object-local
   counts. Static layouts use a constant; ordinary dynamic layouts use an
   affine formula such as `base + stride * count`; unusual layouts use a custom
   local query. This avoids a universal size field but constrains how native
   payload and subtype storage may be composed.
3. Record another compact physical-layout or size-class discriminator in the
   object. This may reduce common header cost, but it introduces another layout
   identity whose relationship with `NativeLayoutId` and Shape must remain
   explicit.

The existing descriptor direction favors option 2, but native subtypes are a
forcing input rather than a settled confirmation. A useful simplifying rule
would be to avoid two independently variable tails in one movable allocation.
Exact tuple storage could remain variable-sized, for example, while tuple
subtypes use a wrapper or remain unsupported. Fixed-size native prefixes could
use locally counted tail storage. Whether that restriction is preferable to an
exact size field remains open.

Tracing is a separate concern from byte extent. Appended subtype fields may not
be contiguous with the native prefix's existing owned `Value` fields, so the
collector may need an additional subtype span or a custom visitor even when the
allocation size is easy to derive. An exact-size header alone would not solve
that problem.

## Extension-Type Declaration Options

A future extension-type API may need to declare more than native layout
identity. Possible declarations include:

- fixed-size and tail-extensible, allowing CloverVM to append subtype storage;
- variable-sized with an extension-supplied subtype allocator and trace policy;
- stable and extension-owned, participating through explicit tracing without
  ordinary nursery evacuation;
- non-subclassable until a compatible subtype representation exists.

Class creation should reject an unsupported native base rather than silently
give its instances the ordinary `Instance` layout. The exact declaration and
validation API remain open.

## Naming and API Hazards

Any design that separates wrapper identity from native payload needs sharp C++
names:

- exact checks should say exact, such as `is_exact_dict`;
- semantic compatibility checks should say compatible or subtype-aware;
- payload extraction should be visibly dangerous, such as
  `extract_native_payload_for_arithmetic` or a similarly explicit local name;
- operations that may drop subtype identity must not be used for object identity,
  attribute lookup, or `type(...)`.

The most important bug to avoid is accidentally replacing a subtype object with
its exact payload. For example, `MyInt(5)` must not silently become the SMI `5`
in any path where identity, type, attributes, or method dispatch remain
observable.

## Open Questions

- Should `ClassObject` store a reusable internal instance-allocation plan, and
  what minimum information belongs in it?
- Should cached attribute locations become direct byte/word offsets plus an
  overflow-anchor offset, replacing the assumption of one inline slot base?
- Can fixed-size native subtypes use appended storage without making GC trace
  descriptors too irregular?
- Should movable objects store an exact allocation size, continue using local
  extent formulas, or carry a compact physical-layout discriminator?
- Which variable-tail native types should use wrappers, and which should remain
  temporarily non-subclassable?
- Should CloverVM initially reject subclassing of some native builtins, matching
  CPython only where the implementation is straightforward?
- Should `int` subclasses be implemented first as the forcing case for inline
  payload wrappers?
- Do mutable native subtype wrappers own their payload exclusively, or can the
  payload be shared with another object?
- Which APIs should mean exact native layout, and which should mean
  Python-level builtin compatibility?
- How should C API entry points expose exact payload access versus
  subtype-compatible behavior?
- Should nonempty `__slots__` on native subtypes be supported separately from
  ordinary instance `__dict__` storage?

For now, exact builtin shape work should not depend on this design. Native
subtype storage is a separate object-model problem.
