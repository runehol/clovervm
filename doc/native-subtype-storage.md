# Native Subtype Storage

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
