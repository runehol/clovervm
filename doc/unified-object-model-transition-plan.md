# Unified Object Model Transition Plan

## Goal

Move from the current split model:

- instances have Shapes and slot storage
- classes have a separate member vector and base-chain lookup

to the unified model described in
[doc/unified-object-model.md](./unified-object-model.md):

- every object has a Shape
- `__class__` lives on the Shape and in a fixed object slot
- class objects use the same shape/slot protocol as instances
- Shapes track present vs latent descriptors and stable slots

## Current Implementation Summary

Today the runtime is only partially aligned with that design.

### Instances already use Shapes

Instances store both a class pointer and a Shape pointer, and own-property
updates go through shape transitions and slot storage.

Relevant code:

- [src/instance.h](../src/instance.h)
- [src/instance.cpp](../src/instance.cpp)

### Classes are still separate

`ClassObject` does not use shape-backed property storage for class attributes.
Instead it stores members in an ordered vector, does local lookup in that
vector, and falls back recursively through `base`.

Relevant code:

- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)

### Shapes are simpler than the target model

Current `Shape` objects model only a flat ordered list of present properties.
Deleting a property removes its descriptor entirely. Re-adding the same name
allocates a fresh slot instead of reusing a latent stable slot.

Relevant code:

- [src/shape.h](../src/shape.h)
- [src/shape.cpp](../src/shape.cpp)
- [tests/test_shape.cpp](../tests/test_shape.cpp)

### Attribute lookup hard-codes the split

Attribute lookup and method binding distinguish instances from classes and
special-case `__class__`.

Relevant code:

- [src/attr.cpp](../src/attr.cpp)

### Class construction also hard-codes the split

Class body execution builds a `ClassObject` and installs attributes through
`set_member()`, while calling a class allocates an `Instance` from the class's
initial instance shape.

Relevant code:

- [src/interpreter.cpp](../src/interpreter.cpp)

## Important Note About `method_version`

`method_version` currently exists on `ClassObject`, but no runtime code appears
to use it for lookup, caching, or invalidation. Because of that, it can be
deleted directly once the class storage rewrite begins; it does not need a
compatibility or migration phase.

Relevant code:

- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)
- [tests/test_shape.cpp](../tests/test_shape.cpp)

## Transition Plan

### 1. Introduce a shared shape-backed object API

Before changing semantics, factor out the common operations that both instances
and classes will need:

- `get_shape()`
- own-property load/store/delete by name
- slot read/write helpers
- overflow-slot management where needed

The goal is to make class migration an internal storage change rather than a
full API rewrite across the runtime.

This does not require a virtual interface. Given runtime sensitivity around
vtables, the intended direction is:

- matching non-virtual methods on concrete types
- shared helper functions for common shape/slot logic
- explicit kind checks where a call site handles multiple object kinds

If the implementation truly converges later, these operations can migrate onto
`Object` as ordinary non-virtual helpers. The important thing in this phase is
shared semantics and shared invariants, not polymorphic dispatch.

Primary files:

- [src/instance.h](../src/instance.h)
- [src/instance.cpp](../src/instance.cpp)
- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)

### 2. Extend `Shape` to represent the target descriptor model

Upgrade `Shape` from a flat list of present properties to a descriptor array
with:

- explicit slot index
- storage kind
- descriptor flags
- Shape flags such as `IsClassObject`, `IsImmutableType`, and custom attribute
  access markers
- `present_count`
- present and latent descriptor regions

This step should land before migrating class storage, because classes need the
full descriptor model immediately.

Primary files:

- [src/shape.h](../src/shape.h)
- [src/shape.cpp](../src/shape.cpp)
- [tests/test_shape.cpp](../tests/test_shape.cpp)

### 3. Change instance transitions to use present/latent semantics

Rework instance add/delete transitions so that:

- deleting a predefined fixed-slot descriptor moves it from present to latent
- deleting an ordinary user attribute may drop or compact its descriptor instead
  of preserving it forever
- reinsertion reuses the latent descriptor's slot when a retained latent
  descriptor exists
- present descriptors preserve insertion order
- shape presence becomes separate from slot allocation

This replaces the current delete-and-forget behavior for fixed slots without
requiring unbounded latent descriptor retention for arbitrary user attributes.

Primary files:

- [src/shape.cpp](../src/shape.cpp)
- [src/instance.cpp](../src/instance.cpp)
- [tests/test_shape.cpp](../tests/test_shape.cpp)

### 4. Move instance `__class__` into the shape-backed model

Replace the current special-case `__class__` logic with a predefined
read-only stable slot. Ordinary attribute stores to that slot should be
rejected; supported `__class__` reassignment must go through a dedicated
checked transition path.

Invariants to establish:

- the object's fixed `__class__` slot and the Shape's type identity match
- ordinary `obj.__class__` lookup can use the normal object attribute path
- valid `__class__` reassignment updates the Shape and fixed object slot
  together after compatibility checks
- invalid `__class__` reassignment does not reach the ordinary slot write path

Primary files:

- [src/attr.cpp](../src/attr.cpp)
- [src/instance.h](../src/instance.h)
- [src/instance.cpp](../src/instance.cpp)
- [src/shape.h](../src/shape.h)
- [src/shape.cpp](../src/shape.cpp)

### 5. Give `ClassObject` a real Shape and slot-backed property storage

Migrate class attributes off the `members` vector and onto the same kind of
shape-driven property storage used by instances.

The C++ layout can still remain class-specific. The important change is that
class-visible properties become ordinary object properties with descriptor and
slot metadata.

This is also the point where `method_version` can be deleted.

Primary files:

- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)
- [tests/test_shape.cpp](../tests/test_shape.cpp)

### 6. Define class-specific predefined slots

Reserve stable slots for class metadata and hot protocol names. Stable latent
slot retention is a guarantee for these predefined fixed slots, not a blanket
requirement for every user-created attribute name.

Always-present candidates:

- `__class__`
- `__name__`
- `__bases__`
- later `__mro__`

Optional stable-slot candidates:

- `__new__`
- `__init__`

The key distinction is that some names are always present, while others may
move between present and latent regions while keeping a stable slot.

Primary files:

- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)
- [src/shape.h](../src/shape.h)
- [src/shape.cpp](../src/shape.cpp)

### 7. Rework attribute lookup to use Shape presence

Replace vector lookup plus `base` recursion with a resolver that understands:

- present descriptor: found here
- latent descriptor: reserved here but not defined, so continue to bases
- absent descriptor: continue normally

This is the semantic change that makes latent predefined dunder slots useful
for class lookup.

The implementation should share class-chain search primitives across lookup
modes, but instance lookup and class-object lookup are different algorithms:

- instance lookup searches `obj.__class__.__mro__`, then the receiver object's
  own storage, then the class-chain result as appropriate
- class-object lookup searches both `Class.__class__.__mro__` and
  `Class.__mro__`; descriptors found on the metaclass path are invoked with the
  class object as receiver, while descriptors found on the class path are
  invoked with no instance receiver

Descriptor handling will be layered onto the resolved result, so descriptor
objects decide binding behavior with the correct receiver convention rather
than the cache inferring it only from where the value is stored.

This resolver must eventually implement Python descriptor precedence:

- data descriptors found in the class/metaclass chain override receiver-local
  attributes
- receiver-local attributes override non-data descriptors and ordinary
  class-chain values
- non-data descriptors bind only after receiver-local lookup misses
- missing attributes fall through to `__getattr__` when supported

For descriptor invocation, keep track of the winning lookup path. Instance
lookup invokes descriptors as `descriptor.__get__(obj, obj.__class__)`.
Class-object lookup through the metaclass path invokes descriptors as
`descriptor.__get__(Class, Class.__class__)`. Class-object lookup through
`Class.__mro__` invokes descriptors as `descriptor.__get__(Value::None(),
Class)`.

Store and delete paths need matching descriptor-aware semantics. The generic
store/delete path must respect `__setattr__` and `__delattr__` overrides when
present, and the default path must consult the class/metaclass chain for data
descriptors with `__set__` or `__delete__` before mutating receiver-local
storage directly.

Primary files:

- [src/class_object.cpp](../src/class_object.cpp)
- [src/attr.cpp](../src/attr.cpp)
- [tests/test_attr.cpp](../tests/test_attr.cpp)
- [tests/test_shape.cpp](../tests/test_shape.cpp)

### 8. Route all class mutation through Shape transitions

Once classes are shape-backed, class attribute writes and deletes should use
the same transition machinery as instances:

- write to existing present attribute updates the slot only
- adding a new attribute changes Shape
- deleting an attribute transitions to a new Shape and writes
  `Value::not_present()` into the slot

After this step, the old `members` API should disappear or become a thin
adapter over the shape-backed representation used only by tests.

Primary files:

- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)
- [src/shape.h](../src/shape.h)
- [src/shape.cpp](../src/shape.cpp)

### 9. Update interpreter paths that create and populate classes

Change class-body result construction to populate class properties through the
new object-property API instead of `set_member()`.

After class properties are installed, `BuildClass` should run the
`__set_name__` notification pass for class-body values that define it.

Also ensure instance construction uses initial shapes whose predefined
`__class__` invariants are already established.

Primary files:

- [src/interpreter.cpp](../src/interpreter.cpp)

### 10. Rewrite tests around the new invariants

The tests need to move from the current split assumptions to the unified
object model.

Add or rewrite coverage for:

- present vs latent descriptor behavior
- stable predefined-slot reuse after delete/re-add
- `__class__` shape/object-slot synchronization
- class objects storing ordinary properties via Shapes
- class-chain lookup continuing past latent descriptors
- data descriptors overriding receiver-local attributes
- receiver-local attributes overriding non-data descriptors
- descriptor invocation receiving the original lookup receiver
- descriptor `__set__` / `__delete__` taking precedence over direct storage
- custom `__getattribute__`, `__setattr__`, and `__delattr__` disabling the
  default fast paths

Primary files:

- [tests/test_shape.cpp](../tests/test_shape.cpp)
- [tests/test_attr.cpp](../tests/test_attr.cpp)
- [tests/test_interpreter.cpp](../tests/test_interpreter.cpp)

### 11. Add lookup invalidation only after the storage model is unified

The unified-object-model doc describes lookup validity cells as the long-term
replacement for ad hoc invalidation. That work should come after classes and
instances both use the same shape-based lookup model.

Inline caches should store an owning object plus a storage location
(`StorageKind` and slot index), not a raw pointer to the resolved slot. Overflow
or extra-slot storage may be reallocated by unrelated mutations, so direct slot
pointers are not stable enough for cached class-chain lookup.

The lookup validity cell should be independent of the cached owner. A
receiver-local slot hit still needs validity for class-chain assumptions,
because installing a data descriptor for the same name on the class or a base
class must invalidate the self-slot fast path. The cached owner, not the
presence of a validity cell, should decide whether the storage location is
receiver-relative or owner-relative.

Inline caches should also record the resolved access kind explicitly rather
than inferring binding behavior from owner presence. The access kind decides
whether the cached storage location is returned directly, passed through
descriptor `__get__`, or treated as a miss / `__getattr__` fallback.

Receiver-local slot caches need descriptor-precedence protection. They should
only be emitted when the class-chain lookup for the same name either misses or
resolves to a value whose type Shape has `IsImmutableType`. This permits mutable
descriptor objects while requiring immutable descriptor protocol behavior.

Class-object writes must invalidate attached lookup validity cells even when
the write updates an already-present attribute and therefore does not change the
class object's Shape. This catches replacement of an ordinary value or non-data
descriptor with a data descriptor. Class attribute deletes remain covered by
Shape-transition invalidation.

Because `method_version` is currently unused, there is no need to preserve it
while introducing lookup cells later.

## Recommended Implementation Order

If this work is done in multiple PRs, the safest order is:

1. Refactor toward a shared shape-backed object API.
2. Upgrade `Shape` to support present/latent descriptors and flags.
3. Switch instance transitions to the new descriptor semantics.
4. Move instance `__class__` to predefined shape-backed slots.
5. Migrate `ClassObject` storage from `members` to shape-backed slots.
6. Delete `method_version`.
7. Rework class lookup to use shape presence and latent descriptors.
8. Update interpreter class construction paths.
9. Add lookup validity cells and inline-cache integration later.

## Main Risk To Avoid

The biggest trap is migrating class storage onto Shapes before `Shape` can
express latent descriptors and predefined stable slots. Doing that would force
an immediate second rewrite of the class side.

So the critical sequencing rule is:

- upgrade `Shape` first
- then migrate `ClassObject`
