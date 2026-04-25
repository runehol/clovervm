# Unified Object Model Transition Plan

## Goal

Move from the original split model:

- instances have Shapes and slot storage
- classes have a separate member vector and base-chain lookup

to the unified model described in
[doc/unified-object-model.md](./unified-object-model.md):

- every object has a Shape
- `__class__` lives on the Shape and in a fixed object slot
- class objects use the same shape/slot protocol as instances
- Shapes track present vs latent descriptors and stable slots

## Current Implementation Summary

Today the runtime is substantially closer to that design for user-created
instances and classes, but it is not yet a unified object model.

### Instances already use Shapes

Instances use shape-backed own-property storage. Their `__class__` value lives
in a predefined read-only stable slot and is kept in sync with the Shape's type
identity.

Relevant code:

- [src/instance.h](../src/instance.h)
- [src/instance.cpp](../src/instance.cpp)

### Classes use Shapes

`ClassObject` now uses shape-backed storage for class attributes. Class
metadata such as `__class__`, `__name__`, `__bases__`, and `__mro__` is stored
in predefined Shape-backed slots, and class lookup walks the materialized MRO.

Relevant code:

- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)

### Shapes support present and latent descriptors

`Shape` now models present and latent descriptors, explicit storage locations,
descriptor flags, Shape flags, and stable predefined slots. This supports the
class and instance storage work already completed, while still leaving the
broader object-header and builtin-object migration unfinished.

Relevant code:

- [src/shape.h](../src/shape.h)
- [src/shape.cpp](../src/shape.cpp)
- [tests/test_shape.cpp](../tests/test_shape.cpp)

### Attribute lookup still has split-model remnants

Attribute lookup now uses Shape presence and materialized `__mro__` for the
class chain, but it still distinguishes native object kinds directly and still
contains compatibility behavior around `__class__` and C++ `Klass` type
identity.

Relevant code:

- [src/attr.cpp](../src/attr.cpp)

### Class hierarchy state is MRO-shaped, but not fully typed

`ClassObject` materializes `__bases__` and `__mro__` from the current
single-base representation. Lookup consumes that MRO, but builtin root type
objects such as `object` and `type` are not yet bootstrapped.

Relevant code:

- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)

### Class construction is shape-backed for user classes

Class body execution now installs attributes through the class mutation helper,
and class calls allocate instances from the class's initial Shape. This path
still assumes the default metaclass story because builtin `type` does not exist
as a real type object yet.

Relevant code:

- [src/interpreter.cpp](../src/interpreter.cpp)

### Shape ownership is not yet on `Object`

`Instance` and `ClassObject` now carry Shapes, but the common `Object` header
still carries the native C++ `Klass` tag rather than the unified model's Shape
reference.

That means generic runtime code still cannot ask any object for its Shape
without first knowing the object's concrete C++ layout.

Relevant code:

- [src/object.h](../src/object.h)
- [src/instance.h](../src/instance.h)
- [src/class_object.h](../src/class_object.h)

### Builtin objects are not yet shape-backed

Builtin runtime objects such as `String`, `List`, `Dict`, `Function`,
`BuiltinFunction`, `Scope`, and iterator objects are still represented by
native C++ `Klass` dispatch tags only. They do not yet have Python-visible type
objects, Shapes, or ordinary attribute storage.

Relevant code:

- [src/str.h](../src/str.h)
- [src/list.h](../src/list.h)
- [src/dict.h](../src/dict.h)
- [src/function.h](../src/function.h)
- [src/builtin_function.h](../src/builtin_function.h)
- [src/scope.h](../src/scope.h)

### `Klass` is still the runtime type authority

The runtime still uses `Object::klass` and static `Klass` instances for native
dispatch, type checks, allocation support, and refcount finalization. The
unified model instead wants Python-visible type identity to come from the
object's Shape and its `__class__` slot.

`Klass` may remain as native implementation metadata, but it should stop being
the Python-level type identity.

Relevant code:

- [src/object.h](../src/object.h)
- [src/klass.h](../src/klass.h)
- [src/typed_value.h](../src/typed_value.h)

### Builtin `type` is not bootstrapped

The current runtime has user class objects and C++ `Klass` tags, but it does
not yet have the builtin type object cycle:

```text
type.__class__ == type
object.__class__ == type
```

Until that exists, user classes cannot fully have a real metaclass slot, and
class-object `__class__` remains partly a compatibility bridge.

## Transition Plan

User instances and user classes now share Shape-backed property storage closely
enough to exercise the descriptor machinery. The remaining work is to make all
Python-visible objects have Shapes and real Python-visible type objects. Lookup
validity cells are deliberately moved behind that work.

### 1. Move Shape ownership into the common object header

Status: not started.

Give every heap object a common way to expose its current Shape. The target is
not necessarily one identical property layout for every native object, but it
must become possible for generic attribute code to ask any object:

- what Shape are you using?
- what Python-visible type object does that Shape identify?
- do you have ordinary attribute storage?
- if so, where are your inline and overflow slots?

This likely means moving a Shape reference, or a compact equivalent that can
produce a Shape, into `Object`. Existing `Instance` and `ClassObject` Shape
fields should then collapse into the shared mechanism.

Keep native `Klass` during this step. It is still useful for C++ dispatch,
allocation, finalization, and low-level fast type predicates. The semantic
change is that `klass` stops being the source of Python-visible `__class__`.

Implementation notes:

- introduce common `Object` helpers for `get_shape()` and type-shape access
- preserve concrete object slot helpers for layouts that differ by native type
- migrate `Instance` and `ClassObject` to the common Shape reference first
- add assertions that `obj.__class__` lookup agrees with `obj->get_shape()`
- keep `TValue<T>` checks based on native `Klass` until type-object dispatch is
  ready

Primary files:

- [src/object.h](../src/object.h)
- [src/instance.h](../src/instance.h)
- [src/instance.cpp](../src/instance.cpp)
- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)
- [src/shape_backed_object.h](../src/shape_backed_object.h)

### 2. Define builtin type objects and the bootstrap roots

Status: not started.

Create real runtime objects for the builtin types that anchor the model. At a
minimum this needs:

- `type`
- `object`
- `str`
- `list`
- `dict`
- `function`
- `builtin_function`
- any iterator or scope types that are directly user-visible

The bootstrap must explicitly handle the self-cycle:

```text
type.__class__ == type
object.__class__ == type
str.__class__ == type
```

Do this with a small, well-contained bootstrap path rather than ad hoc partial
initialization spread across constructors. The bootstrap path should allocate
the root type objects, create their Shapes, install fixed metadata slots, and
then seal the invariants before normal runtime allocation starts using them.

Required invariants:

- every builtin type object is a `ClassObject` or the chosen type-object native
  representation
- every builtin type object has a class-object Shape
- builtin type object Shapes use `ShapeFlag::IsClassObject`
- immutable builtin type Shapes use `ShapeFlag::IsImmutableType`
- `type` is its own `__class__`
- all root type objects expose ordinary read-only `__name__`, `__bases__`, and
  `__mro__` metadata

Primary files:

- [src/virtual_machine.h](../src/virtual_machine.h)
- [src/virtual_machine.cpp](../src/virtual_machine.cpp)
- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)
- [src/shape.h](../src/shape.h)
- [src/shape.cpp](../src/shape.cpp)

### 3. Give builtin instances Shapes

Status: not started.

After builtin type objects exist, migrate builtin runtime objects so their
Shapes point at those type objects. This is the point where `String`, `List`,
`Dict`, `Function`, `BuiltinFunction`, and similar objects start participating
in ordinary `__class__` lookup.

Not every builtin object needs dynamic user attributes in the first cut. The
plan should distinguish:

- fixed-shape builtin values with no ordinary instance dictionary yet
- builtin values with fixed native fields plus optional overflow attributes
- builtin values that should reject arbitrary attributes until their Python
  semantics are implemented

That distinction belongs in Shape flags and object-local storage helpers, not
in `attr.cpp` special cases. A builtin object can be shape-backed before it
supports arbitrary attribute writes.

Implementation notes:

- add root Shapes for each builtin instance kind
- initialize builtin object Shapes at allocation time
- make `obj.__class__` for builtin values use Shape-backed lookup
- reject ordinary attribute writes through the generic path when the builtin
  type does not yet support extra attributes
- add interpreter tests for `().__class__`-style equivalents as each value kind
  exists in the language

Primary files:

- [src/str.h](../src/str.h)
- [src/list.h](../src/list.h)
- [src/dict.h](../src/dict.h)
- [src/function.h](../src/function.h)
- [src/builtin_function.h](../src/builtin_function.h)
- [src/virtual_machine.cpp](../src/virtual_machine.cpp)
- [src/attr.cpp](../src/attr.cpp)

### 4. Reframe `Klass` as native implementation metadata

Status: not started.

Once builtin objects have Shapes, split the responsibilities currently bundled
into `Klass`.

Keep `Klass` for native concerns:

- C++ layout identity
- allocation and finalization support
- fast unchecked casts used by `TValue<T>`
- debugging names for native object kinds

Move Python-visible type concerns to Shapes and type objects:

- `__class__`
- user-visible type names
- class hierarchy and MRO
- descriptor lookup through type objects
- metaclass behavior

The end state should make this distinction clear in APIs. Code that needs a
native layout asks for `native_klass` or equivalent. Code that needs Python type
identity asks for the object's Shape/type object.

Migration steps:

- rename or wrap call sites where `klass` is being used for Python semantics
- make `load_dunder_class()` return the Shape/type object for every object
- keep native `klass` checks for low-level dispatch until replaced deliberately
- update tests that currently assert `&ClassObject::klass` for Python type
  behavior

Primary files:

- [src/object.h](../src/object.h)
- [src/klass.h](../src/klass.h)
- [src/typed_value.h](../src/typed_value.h)
- [src/attr.cpp](../src/attr.cpp)
- [tests/test_attr.cpp](../tests/test_attr.cpp)
- [tests/test_interpreter.cpp](../tests/test_interpreter.cpp)

### 5. Make class-object `__class__` a real metaclass slot

Status: not started.

After `type` exists and `Klass` no longer supplies Python type identity, remove
the remaining class-object `__class__` compatibility behavior. User classes
should have an ordinary read-only stable `__class__` slot whose value is their
metaclass, initially `type`.

This step should also make class creation consume the metaclass object
explicitly, even if the language still only supports the default metaclass.
That keeps the constructor path honest for later custom metaclass support.

Required behavior:

- `Cls.__class__ is type` for ordinary user classes
- `type.__class__ is type`
- class-object lookup of `__class__` goes through normal shape-backed storage
- ordinary assignment to class `__class__` is rejected
- any future supported metaclass reassignment goes through a checked transition
  path

Primary files:

- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)
- [src/interpreter.cpp](../src/interpreter.cpp)
- [src/attr.cpp](../src/attr.cpp)
- [tests/test_attr.cpp](../tests/test_attr.cpp)
- [tests/test_interpreter.cpp](../tests/test_interpreter.cpp)

### 6. Move generic attribute access onto the object protocol

Status: not started.

With every Python-visible object carrying a Shape, `attr.cpp` should stop being
a dispatch table over `Instance` versus `ClassObject` versus builtin native
types. It should become the default object protocol:

- receiver-local present lookup
- type/MRO lookup
- descriptor classification
- descriptor invocation
- custom `__getattribute__`, `__getattr__`, `__setattr__`, and `__delattr__`
  hooks as they become implemented

Native kinds may still supply layout helpers, but they should not define
separate Python attribute semantics unless the language requires it.

This is also the right time to delete any remaining special case that returns
C++ `Klass` as a Python-visible type.

Primary files:

- [src/attr.cpp](../src/attr.cpp)
- [src/shape_backed_object.h](../src/shape_backed_object.h)
- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)
- [src/instance.h](../src/instance.h)
- [src/instance.cpp](../src/instance.cpp)

### 7. Complete descriptor and custom attribute semantics

Status: not started.

The current lookup code preserves existing method binding behavior, but the
full unified model needs Python descriptor precedence before caches are worth
building.

Add or complete:

- data descriptor precedence over receiver-local attributes
- non-data descriptor binding after receiver-local lookup misses
- `__get__`, `__set__`, and `__delete__` invocation
- `__set_name__` notification during class creation
- `staticmethod`, `classmethod`, and `property`
- `__getattribute__`, `__getattr__`, `__setattr__`, and `__delattr__`
- explicit disabling of default fast paths when custom hooks are present

Descriptor classification must itself use the unified type lookup path. Cache
eligibility can be conservative until immutable type Shapes and lookup cells
exist.

Primary files:

- [src/attr.cpp](../src/attr.cpp)
- [src/interpreter.cpp](../src/interpreter.cpp)
- [src/builtin_function.h](../src/builtin_function.h)
- [tests/test_attr.cpp](../tests/test_attr.cpp)
- [tests/test_interpreter.cpp](../tests/test_interpreter.cpp)

### 8. Add lookup invalidation only after the object model is unified

The unified-object-model doc describes lookup validity cells as the long-term
replacement for ad hoc invalidation. That work should come after classes and
instances, builtin objects, and builtin type objects all use the same
shape-based lookup model.

Inline caches should store a cached resolved value for class-chain hits, not a
raw pointer to the resolved slot. Because every successful class attribute write
invalidates attached lookup validity cells, replacing a class attribute kills
the cache before the stale resolved value can be used again.

Receiver-slot hits still need a receiver-relative storage location
(`StorageKind` and slot index), not a raw pointer to the resolved slot. Overflow
or extra-slot storage may be reallocated by unrelated mutations, so direct slot
pointers are not stable enough.

The lookup validity cell should be independent of the cached value. A
receiver-local slot hit still needs validity for class-chain assumptions,
because installing a data descriptor for the same name on the class or a base
class must invalidate the self-slot fast path.

Inline caches should also record the resolved access kind explicitly rather
than inferring binding behavior from cached-value presence. The access kind
decides whether the receiver slot is read, the cached resolved value is returned
directly, the cached resolved value is passed through descriptor `__get__`, or
the access is treated as a miss / `__getattr__` fallback.

Plan cache structures around semantic results, not the current interpreter
shortcut. A resolved lookup result should carry at least:

- lookup mode
- winning path, such as receiver slot, instance class chain, metaclass path, or
  class path
- access kind
- descriptor receiver kind when descriptor invocation is needed
- cached value or receiver-relative storage location
- lookup validity cell

That shape lets direct method calls, escaped method values, and ordinary
attribute loads share resolution while still making different binding choices.

If class-write invalidation later becomes more selective, for example by
preserving lookup cells for ordinary value-to-ordinary-value writes, class-chain
caches should switch back to storing a resolved object plus storage location so
they can reread the current slot contents while the lookup cell remains valid.

Receiver-local slot caches need descriptor-precedence protection. They should
only be emitted when the class-chain lookup for the same name either misses or
resolves to a value whose type Shape has `IsImmutableType`. This permits mutable
descriptor objects while requiring immutable descriptor protocol behavior.

Class-object writes must invalidate attached lookup validity cells even when
the write updates an already-present attribute and therefore does not change the
class object's Shape. This catches replacement of an ordinary value or non-data
descriptor with a data descriptor. Class attribute deletes remain covered by
Shape-transition invalidation.

## Recommended Implementation Order

The safe order is:

1. Move Shape ownership/access into the common `Object` header while preserving
   native `Klass` for C++ implementation concerns.
2. Bootstrap real builtin type objects, including the `type.__class__ == type`
   cycle.
3. Give builtin runtime objects Shapes that point at their builtin type
   objects.
4. Reframe `Klass` APIs so Python-visible type identity comes from Shapes and
   type objects.
5. Make user class `__class__` a real metaclass slot, initially always `type`.
6. Move generic attribute access onto the shared object protocol.
7. Complete descriptor and custom attribute semantics.
8. Add lookup validity cells and inline-cache integration.

## Main Risk To Avoid

The biggest trap now is adding lookup validity cells before type identity is
actually unified. A cache built around native `Klass` or special-cased builtin
objects would immediately need a second design once builtin type objects and
Shape-backed builtin instances land.

So the critical sequencing rule for the next phase is:

- common Shape access first
- builtin `type` / builtin type objects second
- builtin object Shapes third
- lookup caches last
