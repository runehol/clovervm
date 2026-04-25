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

### Classes use Shapes, with compatibility shims

`ClassObject` now uses shape-backed storage for class attributes, but the old
member-facing API remains as a compatibility shim. Class metadata such as
`__class__`, `__name__`, `__bases__`, and `__mro__` is stored in predefined
Shape-backed slots.

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

## Important Note About `method_version`

`method_version` previously existed on `ClassObject`, but no runtime code used
it for lookup, caching, or invalidation. It was deleted as part of the class
storage rewrite and does not need a compatibility or migration phase.

Relevant code:

- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)
- [tests/test_shape.cpp](../tests/test_shape.cpp)

## Transition Plan

### 1. Introduce a shared shape-backed object API

Status: done. Instances now route own-property load/store/delete through shared
shape-backed helpers, Shape exposes an explicit present/latent/absent
descriptor-query API, and `ClassObject` has matching local own-property methods
backed by the existing member vector until class storage is migrated.

Before changing semantics, factor out the common operations that both instances
and classes will need:

- `get_shape()`
- own-property load/store/delete by name
- slot read/write helpers
- overflow-slot management where needed
- a way to ask whether a name is present, latent, or absent without reading the
  slot payload

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
- [src/attr.cpp](../src/attr.cpp)
- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)

Keep this layer narrow. It should expose object-local storage operations only;
descriptor binding, base-chain traversal, `__getattr__`, and custom
`__getattribute__` handling belong in the lookup layer. Mixing those concerns
here would make the class storage migration harder to review.

### 2. Extend `Shape` to represent the target descriptor model

Status: done. Shape now stores packed 64-bit `DescriptorInfo` entries with
storage location plus descriptor flags, has an explicit `present_count` boundary
for present vs latent descriptor regions, carries Shape flags, and supports
descriptor-flagged add transitions.

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

Make the transition API explicit about descriptor state. In addition to
resolving present attributes for normal lookup, `Shape` should support queries
roughly equivalent to:

- `lookup_descriptor_including_latent(name) -> {present, latent, absent,
  descriptor info}` for structural transition code
- `lookup_descriptor_index(name) -> present descriptor index or not found` for
  the normal present-only path
- `resolve_present_property(name) -> storage location or not found`
- `find_latent_descriptor(name) -> descriptor info or not found`

Keep the latent-aware operation deliberately explicit. Ordinary attribute
resolution should use the shorter present-only lookup, while transition code
that may reuse latent stable slots should call the longer
`lookup_descriptor_including_latent()` form.

Also separate `next_slot_index` from descriptor count early. Once deletion can
move a descriptor to the latent region, the highest allocated slot and the
number of descriptors are no longer interchangeable.

Primary files:

- [src/shape.h](../src/shape.h)
- [src/shape.cpp](../src/shape.cpp)
- [tests/test_shape.cpp](../tests/test_shape.cpp)

### 3. Change instance transitions to use present/latent semantics

Status: done. Instance add/delete now uses Shape descriptor presence rather than
flat property membership: ordinary descriptors may be dropped on delete, stable
slot descriptors move to the latent region and reuse their slot on re-add, and
slot payloads are cleared to `Value::not_present()` when deletion succeeds.
`READ_ONLY` descriptors reject ordinary store/delete before slot mutation.

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

The slot payload must be updated as part of the object mutation, not only as
part of Shape derivation:

- transition to present: write the new value into the resolved slot
- transition to latent: write `Value::not_present()` into the resolved slot
- transition that drops an ordinary descriptor: clear or ignore the old storage
  according to the chosen compaction policy

Tests should assert both sides of the invariant: Shape membership says whether
lookup should see a name, and the corresponding fixed slot holds
`Value::not_present()` whenever a predefined descriptor is latent.

Primary files:

- [src/shape.cpp](../src/shape.cpp)
- [src/instance.cpp](../src/instance.cpp)
- [tests/test_shape.cpp](../tests/test_shape.cpp)

### 4. Move instance `__class__` into the shape-backed model

Status: done. New instance root Shapes now contain a predefined read-only,
stable `__class__` descriptor at inline slot 0, `Instance` construction
initializes that slot to the class object, and instance `obj.__class__` lookup
now travels through the ordinary shape-backed own-property path.

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

Do this in two cuts:

1. Add the predefined descriptor and initialize the slot for newly-created
   instances while keeping the existing special-case lookup as a fallback.
2. Once the slot invariant is covered by tests, remove the special-case
   `load_dunder_class()` path for instances and route lookup through the common
   object-property path.

The dedicated reassignment path can initially reject all `__class__`
reassignment except identity-preserving assignments. That keeps the invariant
honest without needing to solve layout compatibility in the same change.

Primary files:

- [src/attr.cpp](../src/attr.cpp)
- [src/instance.h](../src/instance.h)
- [src/instance.cpp](../src/instance.cpp)
- [src/shape.h](../src/shape.h)
- [src/shape.cpp](../src/shape.cpp)

### 5. Define class-specific predefined slots

Status: done for user `ClassObject` metadata. Class objects now carry a
class-object Shape with read-only stable descriptors for `__class__`,
`__name__`, `__bases__`, and `__mro__`; `__bases__` and `__mro__` are
materialized as `List` values from the current single-base chain. The native
`type.__class__ == type` bootstrap cycle is intentionally deferred until the
runtime has real builtin type objects instead of only C++ `Klass` dispatch
tags.

Reserve stable slots for class metadata and hot protocol names. Stable latent
slot retention is a guarantee for these predefined fixed slots, not a blanket
requirement for every user-created attribute name.

Always-present candidates:

- `__class__`
- `__name__`
- `__bases__`
- `__mro__`

Optional stable-slot candidates:

- `__new__`
- `__init__`

The key distinction is that some names are always present, while others may
move between present and latent regions while keeping a stable slot.

Do not postpone the shape of `__mro__` past class lookup migration. Even if the
runtime still only supports single inheritance, materialize an MRO tuple/list
slot for each class before the resolver is rewritten. The initial MRO can be
the linear chain `[Class, base, ..., object]` derived from the existing `base`
field, but the lookup code should consume `__mro__` rather than recursively
following `base`.

The original version of this step tried to include the builtin type bootstrap:

- what Shape is used for class objects whose `__class__` is the builtin `type`
- how the `type.__class__ == type` cycle is represented without partially
  initialized slots
- which builtin class Shapes are marked `IsImmutableType`
- when predefined class slots are populated relative to class allocation

That was too much for the class-storage phase. Those decisions are now moved to
steps 11-15, where the common object header, builtin type objects, and native
`Klass` bridge can be handled together.

Primary files:

- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)
- [src/shape.h](../src/shape.h)
- [src/shape.cpp](../src/shape.cpp)

### 6. Give `ClassObject` a real Shape and slot-backed property storage

Status: done. `ClassObject` now stores ordinary class attributes through its
class Shape and fixed/overflow slot storage, while the legacy member API remains
as a compatibility view over present non-metadata descriptors. `method_version`
and the separate member vector have been removed.

After the class predefined-slot scheme exists, migrate class attributes off the
`members` vector and onto the same kind of shape-driven property storage used by
instances.

The C++ layout can still remain class-specific. The important change is that
class-visible properties become ordinary object properties with descriptor and
slot metadata.

This is also the point where `method_version` can be deleted.

Use a compatibility shim while call sites move:

- keep `get_member()`, `set_member()`, `delete_member()`, `member_count()`, and
  member iteration temporarily, but implement them over the new slot-backed
  storage
- stop adding new direct uses of `members`
- delete the vector and shim only after interpreter construction, tests, and
  attribute lookup no longer depend on vector semantics

Class object storage also needs a clear placement policy. Fixed metadata and
hot predefined slots should have stable inline locations. User-defined class
body attributes can use the same inline/overflow split as instances, but the
transition plan should preserve insertion order for present class attributes so
later `__dict__` behavior has a sensible base.

Primary files:

- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)
- [tests/test_shape.cpp](../tests/test_shape.cpp)

### 7. Rework attribute lookup to use Shape presence

Status: done for the current non-descriptor lookup model. Class-chain lookup
now walks the materialized `__mro__` list and uses Shape descriptor presence:
present descriptors read the class slot, while latent and absent descriptors
continue through the MRO. Instance and class-object attribute lookup keep the
existing binding behavior but now share that class-chain search primitive.

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

Split the implementation into reusable pieces before wiring the full algorithm:

- object-local present lookup: searches only the present descriptor region
- class-chain lookup: walks an already-materialized `__mro__` and treats latent
  descriptors as "continue"
- descriptor classification: determines data vs non-data descriptor by looking
  for `__set__` or `__delete__` on the descriptor value's type
- descriptor invocation: applies the receiver convention selected by the
  winning path

Descriptor classification is a semantic lookup of its own. Until lookup cells
exist, keep it conservative and centralized so later cache eligibility can reuse
the same rule instead of duplicating "is this a data descriptor?" checks in
multiple places.

The first migrated lookup can support the current feature set while preserving
the new structure: functions-as-methods, receiver-local properties, class
properties, and latent predefined slots. Add the full `property`,
`staticmethod`, `classmethod`, `__getattr__`, and custom attribute override
coverage as the corresponding runtime objects exist.

Primary files:

- [src/class_object.cpp](../src/class_object.cpp)
- [src/attr.cpp](../src/attr.cpp)
- [tests/test_attr.cpp](../tests/test_attr.cpp)
- [tests/test_shape.cpp](../tests/test_shape.cpp)

### 8. Route all class mutation through Shape transitions

Status: done. Class mutation now has explicit direct store/delete methods on
`ClassObject`; class construction, class attribute stores, and the member
compatibility shim route through those methods, which are the future
invalidation hook around Shape transitions and slot updates.

Once classes are shape-backed, class attribute writes and deletes should use
the same transition machinery as instances:

- write to existing present attribute updates the slot only
- adding a new attribute changes Shape
- deleting an attribute transitions to a new Shape and writes
  `Value::not_present()` into the slot

After this step, the old `members` API should disappear or become a thin
adapter over the shape-backed representation used only by tests.

Centralize class mutation behind one helper used by bytecode stores,
interpreter class construction, tests, and internal runtime setup. That helper
is the future invalidation hook: even before lookup validity cells exist, it
should be the only place that distinguishes "slot update on existing present
descriptor" from "Shape transition".

Read-only predefined class metadata should reject ordinary stores and deletes
here. Supported changes to `__bases__`, `__mro__`, or class `__class__` should
remain separate checked operations so they can recompute hierarchy state and
transition Shape metadata together.

Primary files:

- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)
- [src/shape.h](../src/shape.h)
- [src/shape.cpp](../src/shape.cpp)

### 9. Update interpreter paths that create and populate classes

Status: done. `BuildClass` now allocates `ClassObject` with predefined
metadata slots already in place, installs class-body properties through the
direct class mutation helper, rejects attempted writes to read-only predefined
class metadata, and class calls allocate instances from the class initial Shape
with the `__class__` slot invariant already established. `__set_name__`
notification sites are detected explicitly and currently raise the documented
temporary unsupported error until the runtime has a generic internal call helper
for descriptor callbacks.

Change class-body result construction to populate class properties through the
new object-property API instead of `set_member()`.

After class properties are installed, `BuildClass` should run the
`__set_name__` notification pass for class-body values that define it.

Also ensure instance construction uses initial shapes whose predefined
`__class__` invariants are already established.

Class creation should have a fixed order:

1. allocate the class object with bootstrap metadata slots available
2. compute and store `__bases__`
3. compute and store `__mro__`
4. install class-body attributes through the class mutation helper
5. run `__set_name__`
6. publish the completed class value

If metaclass selection is not implemented yet, document and test the temporary
rule explicitly: user classes are created with the builtin `type` metaclass, and
class `__class__` reassignment is rejected by the checked path.

Primary files:

- [src/interpreter.cpp](../src/interpreter.cpp)

### 10. Rewrite tests around the new invariants

Status: done for the runtime features that currently exist. The test suite now
checks present/latent descriptor behavior, predefined stable slot reuse,
shape-backed instance `__class__`, shape-backed class property storage,
class-chain lookup through materialized `__mro__`, readonly predefined class
metadata, and class construction installing properties through the class
mutation helper in insertion order. Descriptor invocation and custom
`__getattribute__` / `__setattr__` / `__delattr__` fast-path disabling remain
future test items because those runtime objects and hooks are not implemented
yet.

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
- `__mro__`-driven lookup matching the old single-base behavior during the
  transition
- class `__class__`, `__name__`, `__bases__`, and `__mro__` rejecting ordinary
  store/delete
- deleting optional predefined class attributes leaves latent descriptors and
  `Value::not_present()` payloads
- class construction installs attributes through the class mutation helper and
  preserves insertion order
- bootstrap type objects satisfy `type.__class__ == type` and have immutable
  type Shapes where expected

Primary files:

- [tests/test_shape.cpp](../tests/test_shape.cpp)
- [tests/test_attr.cpp](../tests/test_attr.cpp)
- [tests/test_interpreter.cpp](../tests/test_interpreter.cpp)

## Next Transition Plan

The completed steps above should be treated as phase 1: user instances and user
classes now share Shape-backed property storage closely enough to exercise the
descriptor machinery. They did not complete the unified object model.

The next phase should make all Python-visible objects have Shapes and real
Python-visible type objects. Lookup validity cells are deliberately moved behind
that work.

### 11. Move Shape ownership into the common object header

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

### 12. Define builtin type objects and the bootstrap roots

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

### 13. Give builtin instances Shapes

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

### 14. Reframe `Klass` as native implementation metadata

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

### 15. Make class-object `__class__` a real metaclass slot

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

### 16. Move generic attribute access onto the object protocol

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

This is also the right time to delete the old member compatibility API and any
remaining special case that returns C++ `Klass` as a Python-visible type.

Primary files:

- [src/attr.cpp](../src/attr.cpp)
- [src/shape_backed_object.h](../src/shape_backed_object.h)
- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)
- [src/instance.h](../src/instance.h)
- [src/instance.cpp](../src/instance.cpp)

### 17. Complete descriptor and custom attribute semantics

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

### 18. Add lookup invalidation only after the object model is unified

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

Because `method_version` is currently unused, there is no need to preserve it
while introducing lookup cells later.

## Recommended Implementation Order

The first ten steps are the completed class/instance storage phase. The next
safe order is:

1. Move Shape ownership/access into the common `Object` header while preserving
   native `Klass` for C++ implementation concerns.
2. Bootstrap real builtin type objects, including the `type.__class__ == type`
   cycle.
3. Give builtin runtime objects Shapes that point at their builtin type
   objects.
4. Reframe `Klass` APIs so Python-visible type identity comes from Shapes and
   type objects.
5. Make user class `__class__` a real metaclass slot, initially always `type`.
6. Move generic attribute access onto the shared object protocol and delete
   remaining class-member compatibility APIs.
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
