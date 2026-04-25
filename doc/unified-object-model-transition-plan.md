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

The builtin-object and native-layout direction is described in
[doc/builtin-object-model.md](./builtin-object-model.md).

## Current Implementation Summary

Today the runtime is substantially closer to that design. `Object` owns the
common Python-visible metadata (`__class__`, Shape, overflow storage, and native
layout id), builtin type objects are bootstrapped per VM, and user-created
instances and classes both use Shape-backed property storage.

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
class and instance storage work already completed, while still leaving builtin
instance attribute semantics and descriptor behavior unfinished.

Relevant code:

- [src/shape.h](../src/shape.h)
- [src/shape.cpp](../src/shape.cpp)
- [tests/test_shape.cpp](../tests/test_shape.cpp)

### Object owns Shape-backed property storage

`Object` owns the common Shape pointer, overflow storage, `__class__` slot, and
the ordinary own-property operations. `Instance` now relies on that shared
machinery directly. `ClassObject` also uses the shared property operations, but
still has a deliberately fixed class-object slot layout for `__name__`,
`__bases__`, `__mro__`, and a small inline class-attribute area.

Relevant code:

- [src/object.h](../src/object.h)
- [src/object.cpp](../src/object.cpp)
- [src/instance.h](../src/instance.h)
- [src/instance.cpp](../src/instance.cpp)
- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)

### Builtin classes are bootstrapped per VM

The VM builds immortal builtin `ClassObject` instances for native layouts such
as `type`, `object`, `str`, `list`, `dict`, `function`, `builtin_function`,
`code`, and `range_iterator`. These are registered in both the builtin scope
and the VM's native-layout class table as appropriate. `type.__class__ == type`
is handled by the bootstrap path.

Relevant code:

- [src/virtual_machine.h](../src/virtual_machine.h)
- [src/virtual_machine.cpp](../src/virtual_machine.cpp)
- [src/builtin_class_registry.h](../src/builtin_class_registry.h)
- [src/native_layout_id.h](../src/native_layout_id.h)

### Native layout id replaced `Klass`

Native C++ layout identity is now represented by `NativeLayoutId`, not by
static `Klass` objects. This keeps low-level downcasts explicit while leaving
Python-visible type identity to the VM-specific builtin class objects and
Shape-backed `__class__` slots.

Relevant code:

- [src/native_layout_id.h](../src/native_layout_id.h)
- [src/object.h](../src/object.h)
- [src/typed_value.h](../src/typed_value.h)

### Attribute lookup still has split-model remnants

Attribute lookup now uses Shape presence and materialized `__mro__` for the
class chain, but it still distinguishes native object kinds directly and still
has separate paths for instance and class-object behavior.

Relevant code:

- [src/attr.cpp](../src/attr.cpp)

### Class hierarchy state is MRO-shaped

`ClassObject` materializes `__bases__` and `__mro__` from the current
single-base representation. Lookup consumes that MRO. The remaining limitation
is not the absence of builtin root type objects, but the still-narrow class
creation and metaclass story.

Relevant code:

- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)

### Class construction is shape-backed for user classes

Class body execution now installs attributes through the class mutation helper,
and class calls allocate instances from the class's instance root Shape. This
path still assumes the default metaclass story.

Relevant code:

- [src/interpreter.cpp](../src/interpreter.cpp)

### Builtin instance attribute semantics are still narrow

Builtin runtime objects such as `String`, `List`, `Dict`, `Function`,
`BuiltinFunction`, `CodeObject`, and iterator objects now have VM-specific
builtin classes. Their native layouts are Object-backed, but generic attribute
semantics are still conservative and `attr.cpp` still branches on native
layout. Internal heap records such as `Scope` remain non-Object heap records.

Relevant code:

- [src/str.h](../src/str.h)
- [src/list.h](../src/list.h)
- [src/dict.h](../src/dict.h)
- [src/function.h](../src/function.h)
- [src/builtin_function.h](../src/builtin_function.h)
- [src/code_object.h](../src/code_object.h)
- [src/range_iterator.h](../src/range_iterator.h)

## Transition Plan

User instances and user classes now share Shape-backed property storage closely
enough to exercise the descriptor machinery. The remaining work is to make all
Python-visible objects have Shapes and real Python-visible type objects. Lookup
validity cells are deliberately moved behind that work.

### 1. Move Shape ownership into the common object header

Status: completed.

Give every heap object a common way to expose its current Shape. The target is
not necessarily one identical property layout for every native object, but it
must become possible for generic attribute code to ask any object:

- what Shape are you using?
- what Python-visible type object does that Shape identify?
- do you have ordinary attribute storage?
- if so, where are your inline and overflow slots?

`Object` now stores the Shape reference, `__class__` slot, overflow storage, and
native layout id. `Instance` uses that machinery directly. `ClassObject` uses
the shared Shape and property operations while still preserving its fixed
class-object slots for metadata and inline class attributes.

Implementation notes:

- keep tightening ClassObject's fixed-slot layout terminology
- eventually make Shape allocation metadata less dependent on bouncing through
  the owner class

Primary files:

- [src/object.h](../src/object.h)
- [src/instance.h](../src/instance.h)
- [src/instance.cpp](../src/instance.cpp)
- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)

### 2. Define builtin type objects and the bootstrap roots

Status: completed for the current builtin set.

The VM creates immortal runtime `ClassObject` instances for the builtin types
that currently exist:

- `type`
- `object`
- `str`
- `list`
- `dict`
- `function`
- `builtin_function`
- `code`
- `range_iterator`

The bootstrap handles the self-cycle:

```text
type.__class__ == type
object.__class__ == type
str.__class__ == type
```

The bootstrap path allocates root type objects, creates their Shapes, installs
fixed metadata slots, registers classes by native layout id, patches early
interned strings, and then lets normal runtime allocation use the class table.

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

Status: partially complete.

Builtin runtime objects now receive VM-specific builtin classes during normal
Object construction. The remaining work is to finish generic attribute
semantics for builtin instances and make `attr.cpp` stop branching on native
layout except for genuinely native behavior.

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

- audit each builtin native layout for fixed slots versus dynamic attributes
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

Status: completed by replacing `Klass` with `NativeLayoutId`.

Native C++ layout identity is represented by `NativeLayoutId`. Keep that id for
native concerns:

- C++ layout identity
- allocation and finalization support
- fast unchecked casts used by `TValue<T>`
- debugging names for native object kinds, if needed

Move Python-visible type concerns to Shapes and type objects:

- `__class__`
- user-visible type names
- class hierarchy and MRO
- descriptor lookup through type objects
- metaclass behavior

The distinction is now explicit in APIs: code that needs native layout asks for
`native_layout_id()`, while code that needs Python type identity asks for the
object's Shape/type object.

Migration steps:

- make `load_dunder_class()` return the Shape/type object for every object
- keep native layout checks only for low-level dispatch

Primary files:

- [src/object.h](../src/object.h)
- [src/native_layout_id.h](../src/native_layout_id.h)
- [src/typed_value.h](../src/typed_value.h)
- [src/attr.cpp](../src/attr.cpp)
- [tests/test_attr.cpp](../tests/test_attr.cpp)
- [tests/test_interpreter.cpp](../tests/test_interpreter.cpp)

### 5. Make class-object `__class__` a real metaclass slot

Status: completed for the default metaclass path.

User classes have an ordinary read-only stable `__class__` slot whose value is
their metaclass, currently always `type`. The remaining future work is custom
metaclass support rather than the default slot model.

Class creation still only supports the default metaclass. When custom
metaclasses become language-visible, class creation should consume the
metaclass object explicitly.

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

This is also the right time to delete any remaining special case that treats a
native layout check as Python-visible type semantics.

Primary files:

- [src/attr.cpp](../src/attr.cpp)
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

1. Finish builtin instance attribute semantics on top of the shared `Object`
   protocol.
2. Tighten the remaining ClassObject fixed-slot layout cleanup.
3. Move generic attribute access onto the shared object protocol.
4. Complete descriptor and custom attribute semantics.
5. Add lookup validity cells and inline-cache integration.

## Main Risk To Avoid

The biggest trap now is adding lookup validity cells before attribute semantics
are actually unified. A cache built around special-cased builtin objects would
immediately need a second design once builtin instance lookup moves fully onto
the shared object protocol.

So the critical sequencing rule for the next phase is:

- builtin instance attribute semantics first
- generic object-protocol lookup second
- lookup caches last
