# Builtin Object Model

## Purpose

This document describes how builtin/runtime objects should fit into the unified
object model.

The target model is:

- every Python-visible object has a `Shape`
- Python-visible type identity comes from that Shape
- builtin type objects such as `type`, `object`, `str`, `list`, and `dict` are
  VM-owned runtime objects
- native C++ layout identity is separate from Python-visible type identity
- builtin objects use the same slot protocol as user instances and classes

This replaces the older pattern where builtin/runtime objects identify their
Python-visible type with static or global C++ `Klass` objects such as:

```cpp
Klass cl_string_klass(L"string", string_str);
```

`Klass`-style metadata may survive temporarily as native implementation
metadata, but it should stop being observable as `obj.__class__`.

## VM-Specific Builtin Type Objects

Builtin type objects should be allocated while bootstrapping a particular
`VirtualMachine`. They are not process-global identities.

For example, each VM owns its own:

- `type`
- `object`
- `str`
- `list`
- `dict`
- `function`
- `builtin_function`
- iterator and runtime-support types that are Python-visible

These type objects should be inserted into the builtin scope for that VM. Python
code observes them through ordinary builtin lookup, not through C++ globals.

This means identities such as `str` and `type` are VM-specific:

```text
vm_a.builtins["str"] != vm_b.builtins["str"]
```

At the Python level, each VM still satisfies the usual bootstrap relationships:

```text
type.__class__ == type
object.__class__ == type
str.__class__ == type
```

## Shape Versus Native Layout

The unified model needs two distinct concepts:

- Python-visible type identity
- native C++ layout identity

Python-visible type identity belongs to `Shape` and builtin type objects.
Native layout identity is an implementation detail used for casts, fast
dispatch, allocation, and value scanning.

The current `Object::klass` field mixes these two ideas. The planned direction
is to replace it with:

```cpp
Shape *shape;
```

Native C++ layout identity should then move to a compact native layout id,
probably packed into the object header.

## Native Layout Id

The runtime is not expected to need many native builtin layouts. An 8-bit
native layout id should be enough for the foreseeable future.

Examples of native layouts:

- `Instance`
- `ClassObject`
- `String`
- `List`
- `Dict`
- `Function`
- `BuiltinFunction`
- `RangeIterator`
- `Scope`
- `Shape`
- `CodeObject`
- VM array backing objects

The native layout id should replace uses of C++ `Klass` for native RTTI:

- `TValue<T>` checked construction
- interpreter dispatch for callable/runtime object kinds
- `attr.cpp` receiver-kind dispatch while generic lookup is still migrating
- `subscript.cpp` list/dict dispatch
- debug and assertion-only layout checks

The native layout id should not be returned by `obj.__class__`.

## Object Header Direction

The current object header is 16 bytes:

```cpp
const Klass *klass;
int32_t refcount;
uint32_t layout;
```

The `layout` word currently packs:

- one expanded-layout bit
- value-region offset
- value count
- compact object size

Once `Object::klass` becomes `Shape *shape`, the pointer-sized field stores
Python-visible shape/type identity. The native layout id can be packed into the
remaining 32-bit layout word.

A plausible compact layout split is:

```text
bit 31      expanded marker
bits 23-30  native layout id
bits 11-22  managed Value slot count
bits 0-10   object size in 16-byte units
```

The exact bit allocation is still tunable. The important design point is that
the old value-offset bits can disappear once builtin objects follow the uniform
layout convention below.

Avoid stealing bits from `refcount`. Refcounting is hot and already subtle
enough; native layout identity belongs with layout metadata.

## Uniform Builtin Object Layout

Builtin objects should use this physical layout:

```text
Object header
managed Value slots
optional native tail
```

The managed Value region is the portion scanned for ownership. The native tail
contains non-Value data such as counters, callback pointers, bytecode vectors,
or character data.

This convention lets the compact header omit a value-region offset. The Value
region always starts immediately after the `Object` header.

Slot metadata in `Shape` remains the semantic authority for logical attributes.
The native layout only decides where builtin-specific fields live physically.

## Current Layout Fit

Several runtime objects are already close to the desired layout.

Already good or nearly good:

- `List`: stores a `ValueArray<Value>` member, and `ValueArray` embeds only
  managed Value fields.
- `Scope`: stores `MemberValue`, `RawArray`, and `ValueArray` fields first;
  those wrappers expose managed references through Value-sized fields.
- `Function`: has a single managed `CodeObject` field.
- `RangeIterator`: stores its integer state in managed `Value`-sized fields.
- `String`: stores managed `count` first, followed by character data.
- `ClassObject`: stores managed metadata/shape/slot fields first, followed by
  native configuration.
- `CodeObject`: stores managed scope/name fields before native vectors.
- `Dict`: stores managed array fields first, followed by `n_valid_entries` in
  the native tail.

Expected to disappear or become generic:

- `Instance::OverflowSlots` should move into the unified object header/slot
  storage protocol instead of remaining a bespoke side object with native
  fields before a Value payload.

## Arrays Are Not The Blocker

`RawArray<T>` and `ValueArray<T>` were already designed for this direction.
Their embedded members are all managed Value-sized fields:

```cpp
MemberTValue<SMI> size_value;
MemberTValue<SMI> capacity_value;
MemberValue backing;
```

The backing objects then hold either:

- no managed Values, for `RawArray<T>`
- a dynamic managed Value payload, for `ValueArray<T>`

This makes `List`, `Dict`, and `Scope` much easier to migrate than they first
appear. The main requirement is member ordering in the owning object.

## Bootstrap Outline

Builtin type bootstrap should be centralized in VM initialization.

The rough order is:

1. Allocate placeholder root type objects for `type` and `object`.
2. Create Shapes for those root type objects.
3. Install the `type.__class__ == type` cycle.
4. Populate readonly metadata slots such as `__name__`, `__bases__`, and
   `__mro__`.
5. Allocate the remaining builtin type objects.
6. Create root Shapes for builtin instance layouts such as `String`, `List`,
   and `Dict`.
7. Insert builtin type objects and builtin functions into the VM's builtin
   scope.
8. Seal invariants before user code can observe the builtin module.

The bootstrap path should be small and explicit. Avoid spreading partially
initialized builtin type objects through ordinary constructors.

## Migration Notes

The safest migration keeps native layout identity available while Python type
identity moves to Shapes.

Suggested sequence:

1. Introduce a native layout id enum while keeping `Object::klass` temporarily.
2. Add helpers for native layout checks and update new code to use them.
3. Bootstrap VM-owned builtin type objects.
4. Add `Shape *shape` to `Object` or replace `Object::klass` with it.
5. Route `obj.__class__` through Shape/type objects for every Python-visible
   object.
6. Convert builtin object layouts to the uniform managed-Value-prefix form.
7. Remove static/global builtin `Klass` objects such as `cl_string_klass`.
8. Pack native layout id into the object header and delete remaining native
   `Klass` dependencies.

During the transition, exact native checks remain useful. They should be named
as native layout checks so they do not imply Python-level type identity.

## Open Questions

- Should every builtin object support arbitrary user attributes immediately, or
  should some builtin Shapes reject extra attributes until their Python
  semantics are implemented?
- Should native layout ids be compile-time enum values only, or should the VM
  also maintain a layout table for debug names and layout metadata?
- Which objects are Python-visible builtin objects, and which are purely
  internal runtime support objects that only need native layout ids?
- Should builtin type object Shapes with immutable descriptor protocol behavior
  be marked `ShapeFlag::IsImmutableType` during bootstrap?
