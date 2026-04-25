# Object Model

## Core Concepts

### Shape

A **Shape** is an immutable descriptor of an object's structural state:

- property layout
- storage strategy
- property lookup behavior
- type identity

Every object has an associated Shape.

---

### Type on Shape

The **type** of an object is stored on its Shape:

```text
object.shape.__class__
```

This applies uniformly to:

- instances
- classes (which are themselves objects)

---

## Immutability

Shapes are immutable.

No operation mutates a Shape in place. When an object's structure or
type changes, the object transitions to a new Shape.

---

## Transitions

A **Transition** moves an object from one Shape to another.

Transitions occur when:

- a property is inserted
- a property is deleted
- property attributes change
- lookup behavior changes
- the object's type changes

Setting or getting an already-present attribute does not change the
Shape. Only operations that change attribute membership require a Shape
transition.

The previous Shape remains valid for any other objects that still
reference it.

---

## Objects

All runtime entities are objects:

- instances
- classes
- builtin objects such as `String` and `List`

Each object has:

- a Shape
- property storage consistent with that Shape

Builtin objects such as `String` and `List` also carry Shapes and
participate in the same object protocol as user-visible instances and
classes.

---

## Classes as Objects

Classes are ordinary objects with Shapes.

They may contain:

- methods (stored as properties)
- metadata
- a metaclass relationship

---

## C++ Runtime Layout

Although the Python-level model is uniform, runtime objects do not need
to have identical native C++ layouts.

At the C++ level we may use distinct runtime types such as:

- `Object`
- `InstanceObject`
- `ClassObject`
- builtin object types such as `StringObject` and `ListObject`

This lets the runtime give different object kinds different fixed native
fields while still making them behave as ordinary objects at the Python
level.

Common runtime state should still be available across object kinds,
including:

- a Shape reference
- fixed inline slots where applicable
- a pointer to overflow or "extra" slot storage for attributes that do
  not fit in the inline portion

The Shape remains the semantic authority for logical slot meaning. The
native C++ object layout only determines where a given logical slot is
stored physically.

This is why slot metadata needs to distinguish physical storage, for
example:

- inline storage in the native object
- overflow storage in the extra slot array

From the Python side, these objects still behave uniformly: builtin
objects, user instances, and classes all present as ordinary objects
with Shapes and slots.

---

## Class Relationships

### Instance -> Class

Each instance is associated with a class via:

```text
__class__
```

`__class__` is represented in two places:

- on the Shape, as the object's structural type identity
- in the object itself, as a fixed read-only attribute slot

These must always remain in sync.

Ordinary writes to the `__class__` slot are not allowed. Supported
`__class__` reassignment goes through a dedicated checked path rather
than the normal attribute-store path.

For an assignment such as:

```text
obj.__class__ = NewClass
```

the runtime must first validate that the old and new classes are
compatible for the existing object layout. If the assignment is valid,
the object transitions to a new Shape whose `__class__` reflects
`NewClass`, and the fixed object slot is updated at the same time.

The previous Shape remains valid for other objects that still reference
it.

---

### Class -> Metaclass

Similarly, each class is associated with a metaclass via:

```text
__class__
```

Ordinary writes to the class object's `__class__` slot are also not
allowed. Supported metaclass reassignment, if any, goes through the same
kind of dedicated checked transition path.

For an assignment such as:

```text
Class.__class__ = NewMeta
```

the runtime must validate metaclass compatibility before transitioning
the class object to a new Shape whose `__class__` reflects `NewMeta`.

The previous Shape remains valid for other objects that still reference
it.

---

## Shape Descriptors

Each Shape descriptor carries an ordered set of attribute descriptors.

At the representation level, a Shape may hold parallel arrays such as:

- `descriptor_names`
- `descriptor_infos`

`descriptor_names[i]` identifies the attribute name.

`descriptor_infos[i]` carries the metadata for that attribute,
including:

- slot index
- flags
- physical storage location

One plausible C++ shape for `descriptor_infos` is:

```cpp
struct DescriptorInfo {
    int32_t physical_idx;
    StorageKind kind;
    uint16_t flags;
};
```

One compact representation is:

- 64-bit name
- 32-bit slot index
- 16-bit flags

which fits in 128 bits per descriptor entry.

The slot index is explicit. Descriptor position is not itself the slot
index.

This is needed so a name may keep a stable slot assignment even when it
is not currently present in the object's normal attribute set.

The current storage split already distinguishes physical placement, for
example with:

```cpp
enum class StorageKind : uint8_t {
    Inline,
    Overflow,
};
```

Attaching attribute flags to `descriptor_infos` keeps physical storage
policy and attribute policy together.

The initial flag set is intentionally small:

- `READ_ONLY`
- `STABLE_SLOT`

`READ_ONLY` means ordinary attribute assignment and deletion are not
allowed.

`STABLE_SLOT` means the slot index remains reserved across related
Shapes, even when the name moves to the latent region.

When referring to dunder attributes from C++ code, we do not use the
Python spelling directly as an identifier, since names such as
`__class__` are reserved identifiers in C++. Instead we use the
convention `dunder_class`, `dunder_new`, `dunder_init`, and so on.

---

## Shape Flags

Shape-level flags summarize semantic facts that affect lookup and JIT
specialization. Once a Shape is created its flags do not change, but an
object may transition to a new Shape with different flags when lookup
behavior changes.

One plausible initial set is:

```cpp
enum class ShapeFlag : uint16_t {
    IsClassObject = 1 << 0,
    IsImmutableType = 1 << 1,
    HasCustomGetAttribute = 1 << 2,
    HasCustomGetAttrFallback = 1 << 3,
    HasCustomSetAttribute = 1 << 4,
    HasCustomDelAttribute = 1 << 5,
};
```

`IsClassObject` means objects with this Shape are class objects. This
duplicates a native object-kind fact, but it lets a Shape guard select
the correct lookup mode without an additional object-kind guard.

`IsImmutableType` means a class object's namespace and protocol behavior
cannot change. This flag is meaningful only for class-object Shapes. It
is useful when analyzing descriptor values: if `descriptor.__class__` has
an immutable Shape, then descriptor data-ness and protocol behavior are
stable under a Shape guard.

The custom attribute flags summarize whether default attribute access can
be used:

- `HasCustomGetAttribute`: `__getattribute__` overrides default load
  semantics
- `HasCustomGetAttrFallback`: missing loads may call `__getattr__`
- `HasCustomSetAttribute`: `__setattr__` overrides default store
  semantics
- `HasCustomDelAttribute`: `__delattr__` overrides default delete
  semantics

These flags are not just hints. They are compact summaries of whether the
relevant dunder slots are present. They change through Shape transitions
when those attributes are added or removed, but replacing the contents of
an already-present slot does not change the Shape. These flags exclude
hard custom getter/setter cases from the default fast paths; the runtime
does not inspect a custom getter or setter and try to accelerate its
specific implementation.

---

## Present and Latent Descriptor Regions

The descriptor array is partitioned into two regions:

- **present region**: attributes currently present on the object
- **latent region**: attributes that have assigned stable slots but are
  currently not present

The Shape stores a `present_count` boundary:

```text
descriptors[0, present_count)        -> present
descriptors[present_count, end)      -> latent / not-present
```

### Present Region

The present region:

- is scanned for ordinary attribute lookup
- is kept in insertion order
- defines the iteration order needed for an `obj.__dict__` adaptor

### Latent Region

The latent region:

- is not scanned on the common path for ordinary attribute lookup
- remembers names that already own stable slot indices
- is consulted when inserting an attribute, so reinsertion can reuse the
  existing slot

This lets us preserve stable slots for special names without forcing
ordinary user-attribute lookup to compare against a prefix of rarely
used dunder names.

---

## Stable Slots and Presence

For predefined names, especially hot class-level dunder names such as
`__new__` and `__init__`, we want:

- a stable slot index across related class Shapes
- Shape-level knowledge of whether the name is currently present
- the ability to continue base-class lookup when the name is not present

Presence therefore lives on the Shape, not only in the object slot.

The meaning of the descriptor state is:

- descriptor in present region: the attribute is present on this object
- descriptor in latent region: the attribute has a reserved slot but is
  not present on this object
- no descriptor entry: this Shape has no structural knowledge of the
  name

This is important for JIT compilation and class-chain resolution for
predefined slots, because the compiler may need to determine whether
lookup should stop at this class or continue to bases without having a
concrete object available to inspect.

---

## Predefined Class Slots

Some class-level names may have predefined stable slots shared across
class Shapes.

Examples include:

- `__new__`
- `__init__`

Other always-present class metadata such as `__mro__`, `__name__`, and
`__bases__` may also occupy fixed slots.

The important distinction is:

- always-present fixed metadata such as `__class__`, `__mro__`,
  `__name__`, and `__bases__` lives in the present region for every
  class Shape
- optional predefined dunder names may move between the present and
  latent regions while keeping the same slot index

This gives us CPython-like fixed locations for hot protocol names while
still preserving correct "not present here, continue to bases"
semantics.

Typical flag usage:

- `__class__`: `READ_ONLY | STABLE_SLOT`
- `__mro__`: `READ_ONLY | STABLE_SLOT`
- `__bases__`: `READ_ONLY | STABLE_SLOT`
- `__new__`: `STABLE_SLOT`
- `__init__`: `STABLE_SLOT`

---

## Object Slot Invariants

Although presence is tracked on the Shape, predefined slots also carry a
runtime payload invariant in the object itself.

For a predefined slot:

- if its descriptor is in the present region, the slot holds a real
  value
- if its descriptor is in the latent region, the slot holds
  `Value::not_present()`

This redundancy is intentional.

Shape metadata is the authority for compile-time lookup structure and
base-class resolution. Object slot contents are still useful for runtime
fast paths that operate on a concrete object.

For `__class__`, the object slot is not merely a convenience cache: it
allows ordinary attribute lookup to treat `obj.__class__` like any other
fixed attribute and avoids forcing a special case through the lookup
path.

For example, a fast path for constructing an instance of a class may
check:

```text
myclass.__class__ == type
myclass.slots[predef__new__slot] == Value::not_present()
```

That is valid because Shape transitions maintain the invariant between
descriptor membership and slot contents.

---

## Property Placement

Properties may exist on:

- the object itself
- related objects (e.g. class objects)

Shapes encode how property lookup resolves across these relationships.

Methods defined on classes are ordinary properties on class objects.
They are writable and replaceable, not treated as constant.

---

## Lookup Rules

### Lookup Modes

Attribute lookup has several modes. They share the same Shape and
class-chain primitives, but they are not all the same algorithm:

```cpp
enum class AttributeLookupMode : uint8_t {
    InstanceAttribute,
    ClassAttribute,
    ImplicitProtocol,
};
```

- `InstanceAttribute` is normal lookup on an instance, such as `obj.x`.
- `ClassAttribute` is normal lookup on a class object, such as `C.x`.
- `ImplicitProtocol` is runtime protocol lookup, such as resolving
  `__call__` for `obj()`.

`IsClassObject` on the receiver Shape selects between
`InstanceAttribute` and `ClassAttribute` for ordinary attribute access.
Custom attribute flags on the receiver's type Shape determine whether the
default algorithm is valid.

Ordinary receiver-local lookup scans only the present descriptor region.

Latent descriptors are ignored on this path.

### Instance Attribute Lookup

Full Python-compatible lookup also has to account for descriptors. The
normal default `__getattribute__` instance-attribute algorithm is:

1. Resolve the attribute name through the receiver's class chain.
2. If the class-chain result is a data descriptor, invoke it and return
   the result.
3. Otherwise, search the receiver's own present attributes.
4. If the receiver has the attribute, return that value.
5. If the class-chain result is a non-data descriptor, invoke it and
   return the result.
6. If the class-chain result is an ordinary value, return it.
7. Otherwise, fall back to `__getattr__` if one is defined.

For instance lookup, descriptor invocation passes the original receiver:

```text
descriptor.__get__(obj, obj.__class__)
```

Ordinary values stored directly on the receiver are not descriptors for
this purpose. Descriptor behavior comes from attributes found outside the
receiver's own storage, through the class chain.

If the receiver's type Shape has `HasCustomGetAttribute`, this default
lookup algorithm is not valid. The runtime must call the override or use
a cache specialized for that override. If the receiver's type Shape has
`HasCustomGetAttrFallback`, a missing result may need to call
`__getattr__`.

### Class Attribute Lookup

Normal lookup on a class object is not just instance lookup with
`Class.__class__` substituted for `obj.__class__`. It has two lookup
axes:

- the metaclass path, `Class.__class__.__mro__`
- the class path, `Class.__mro__`

The default class-attribute algorithm is:

1. Resolve the attribute name through the metaclass path.
2. If the metaclass result is a data descriptor, invoke it with the class
   object as receiver and return the result.
3. Resolve the attribute name through the class path.
4. If the class-path result is a descriptor, invoke it with no instance
   receiver and return the result.
5. If the class-path result is an ordinary value, return it.
6. If the metaclass result is a non-data descriptor, invoke it with the
   class object as receiver and return the result.
7. If the metaclass result is an ordinary value, return it.
8. Otherwise, fall back to `__getattr__` if one is defined.

For class-object lookup through the metaclass chain, the class object is
the receiver:

```text
descriptor.__get__(Class, Class.__class__)
```

For descriptors found through the class path, lookup is access through a
class rather than through an instance:

```text
descriptor.__get__(Value::None(), Class)
```

This distinction matters for ordinary functions, `staticmethod`,
`classmethod`, `property`, and user descriptors. The resolver must
surface which path produced the descriptor so the descriptor call receives
the correct receiver arguments.

### Store and Delete on an Object

Store and delete operations have their own descriptor protocol.

For the default store path:

1. If the receiver's type Shape has `HasCustomSetAttribute`, call that
   override.
2. Otherwise, resolve the name through the receiver's class chain.
3. If the class-chain result has descriptor `__set__`, call it.
4. Otherwise, mutate receiver-local storage normally, subject to
   read-only flags and layout rules.

For the default delete path:

1. If the receiver's type Shape has `HasCustomDelAttribute`, call that
   override.
2. Otherwise, resolve the name through the receiver's class chain.
3. If the class-chain result has descriptor `__delete__`, call it.
4. Otherwise, delete from receiver-local storage normally, subject to
   read-only flags and layout rules.

### Implicit Dunder Lookup

Dunder method lookup triggered implicitly by the runtime machinery does
not necessarily follow ordinary object lookup.

For operations such as:

```text
obj()  ->  __call__
```

lookup should skip the object itself and search on the class hierarchy
instead.

This applies to protocol-driven dispatch where the runtime is asking
what operation the object's type supports, rather than what attribute
the object instance exposes directly.

By contrast, if user code explicitly performs an attribute lookup such
as:

```text
obj.__call__
```

that uses the normal object attribute lookup path.

### Insertion

When inserting an attribute:

1. Search the existing Shape transitions for a cached insertion
   transition for this attribute. If one exists, use the cached target
   Shape.
2. Search the latent region for an existing descriptor with the same
   name.
3. If found, reuse its slot index.
4. Build a new Shape whose descriptor array:
   - keeps existing present entries in their current order
   - appends the inserted name at the end of the present region
   - copies the remaining latent entries after the new `present_count`

If the name is not found in the latent region, insertion allocates a new
slot and appends a new descriptor to the end of the present region.

Only predefined fixed-slot names are guaranteed to remain pinned in the
latent region across related Shapes. Ordinary user attributes do not
need permanent latent descriptors merely to preserve Python-visible
attribute order; an implementation may compact or drop latent
descriptors for non-fixed names as long as observable lookup and
`__dict__` behavior remain correct.

### Deletion

When deleting a present attribute:

1. Search the existing Shape transitions for a cached deletion
   transition for this attribute. If one exists, use the cached target
   Shape.
2. Build a new Shape.
3. Copy all remaining present descriptors in order.
4. Append the deleted descriptor to the latent region.
5. Preserve its slot index.
6. Store `Value::not_present()` in the corresponding object slot.

This preserves insertion order for present attributes while keeping the
stable slot available for reinsertion when the descriptor is one of the
predefined fixed-slot names. For ordinary user attributes, preserving a
latent descriptor is an implementation choice, not a semantic
requirement.

### Base-Class Resolution

When resolving an attribute through a class chain:

- present descriptor on a class: lookup succeeds here
- latent descriptor on a class: the class reserves the slot but does not
  currently define the attribute, so lookup continues to base classes
- no descriptor on a class: continue according to the ordinary lookup
  rules

This is why Shape-level presence matters even when object slot contents
also encode `Value::not_present()`.

The same class-chain search primitive can be reused by different lookup
modes, but the full lookup algorithms differ. Instance lookup combines a
class-chain search with receiver-local storage. Class-object lookup
combines a metaclass-chain search with a `Class.__mro__` search. The
winning path determines how descriptors such as functions,
`staticmethod`, `classmethod`, and `property` receive their arguments.

---

## Design Principles

1. Shapes are immutable.
2. All membership changes occur via Shape transitions.
3. Setting or getting an already-present attribute uses the slot only.
4. Classes and instances are uniform objects.
5. `__class__` is stored on the Shape.
6. Presence is a Shape property.
7. Slot payload and Shape membership are kept in sync by transitions.
8. Present attributes preserve insertion order.

---

# Inline Caches and Lookup Validity Cells

## Inline Caches

Attribute access is accelerated using **inline caches (ICs)**.

Each cache entry records:

| Field | Meaning |
|---|---|
| `receiver_shape` | guards receiver layout |
| `lookup_mode` | attribute algorithm being specialized |
| `lookup_cell` | validity for class-chain lookup assumptions |
| `access_kind` | semantic action for the resolved attribute |
| `cached_value` | resolved class-chain value for `ResolvedValue` and descriptor cache kinds; otherwise unused |
| `receiver_storage_location` | storage kind plus slot index relative to the receiver for `ReceiverSlot` |

The lookup mode says which attribute algorithm was specialized. This is
important because `InstanceAttribute` and `ClassAttribute` search
different chains and invoke descriptors with different receiver
arguments.

The cached receiver Shape's `IsClassObject` flag must agree with the
lookup mode. Custom attribute flags on the relevant type Shape control
whether a default lookup specialization is legal at all.

The access kind records the semantic action represented by the cache:

```cpp
enum class AttributeAccessKind : uint8_t {
    ReceiverSlot,
    ResolvedValue,
    DataDescriptorGet,
    NonDataDescriptorGet,
    GetAttrFallback,
    Missing,
};
```

The access kind decides whether the cached value is returned directly,
passed through descriptor `__get__`, or treated as a miss/fallback. It
must not be inferred from the presence of a cached value.

Descriptor access also needs to retain the receiver convention selected
by the lookup mode and winning path:

```cpp
enum class DescriptorReceiverKind : uint8_t {
    InstanceReceiver,    // __get__(obj, obj.__class__)
    ClassReceiver,       // __get__(Class, Class.__class__)
    ClassAttribute,      // __get__(Value::None(), Class)
};
```

The access kind determines where the value comes from:

- `ReceiverSlot` reads from a storage location relative to the receiver
- `ResolvedValue` returns the cached resolved value directly
- descriptor access kinds call descriptor protocol on the cached resolved
  value

The lookup validity cell is independent of the value location. Even a
receiver-local slot hit may depend on class-chain lookup remaining
unchanged, because installing a data descriptor for the same name on the
class or a base class would take precedence over the receiver slot.

Descriptor data-ness is itself lookup-sensitive. A descriptor becomes a
data descriptor when its own type defines `__set__` or `__delete__`, so
receiver-slot caches have extra eligibility and invalidation rules. Those
rules are described below in Descriptor-Precedence Invalidation.

On execution:

```text
if obj.shape != cached_shape: miss
if !lookup_cell.valid: miss
if access_kind == Missing:
    return missing
else if access_kind == GetAttrFallback:
    return fallback full lookup
else if access_kind requires receiver storage:
    value = read_slot(obj, receiver_storage_location)
else:
    value = cached_value
apply access_kind to value
```

The receiver Shape protects receiver-local structure.

Because every successful class attribute write invalidates attached
lookup validity cells, class-chain cache entries do not need to reread
the defining class slot. If a class attribute is replaced, the lookup
cell becomes invalid before the cached value can be used again.

If CloverVM later makes class-write invalidation more selective, for
example by preserving lookup cells for ordinary value-to-ordinary-value
writes, then class-chain cache entries would need to store a resolved
object plus storage location instead of a cached value so they can reread
the current slot contents while the lookup cell remains valid.

---

## Descriptor-Precedence Invalidation

A receiver-slot hit is valid only while no class-chain attribute for the
same name has data-descriptor precedence. CloverVM handles the relevant
cases with three complementary rules:

1. **Shape transitions invalidate lookup cells.**

   Adding or deleting a class attribute changes the class object's Shape.
   The transition invalidates attached lookup validity cells for lookups
   that depended on the previous class-chain membership facts.

2. **All class attribute writes invalidate lookup cells.**

   Assigning a new value into an already-present class attribute may keep
   the same Shape if membership is unchanged. The new value may have
   different descriptor behavior from the old value, so every successful
   class attribute write invalidates the class object's attached lookup
   validity cells regardless of the value being written.

   This applies to every path that mutates class-object attribute
   storage, including bytecode stores, class construction helpers,
   internal runtime setters, and default metaclass attribute assignment.

3. **Receiver-slot caches require stable descriptor classification.**

   A receiver-local slot cache is legal only when the class-chain lookup
   for the same name either misses or resolves to a value whose type
   Shape has `IsImmutableType`. The object itself may be mutable; the
   requirement is that its class cannot later gain `__set__` or
   `__delete__` and thereby turn the existing value into a data
   descriptor.

   Objects whose current type is immutable also do not support
   `__class__` reassignment, so the cached descriptor classification
   cannot be invalidated by changing the value's type identity.

Together these rules cover the descriptor-precedence hazards:

- adding or deleting a descriptor or ordinary class attribute is a Shape
  transition
- replacing an existing class attribute with a value of different
  descriptor-ness is caught by class-write invalidation
- mutating a descriptor value's type to add `__set__` or `__delete__` is
  avoided by refusing receiver-slot caches unless the value's type is
  immutable

---

## Lookup Validity Cells

A lookup validity cell represents:

> Lookup through a given class and its base chain is still valid.

Each class object may hold a **primary lookup cell**, reused across
caches.

---

## Class Object State

| Field | Meaning |
|---|---|
| `primary_lookup_cell` | shared validity cell |
| `attached_lookup_cells` | dependent cells |

---

## Registration

Lookup cells are registered in all classes consulted:

```text
for K in C.__mro__:
    K.attached_lookup_cells.add(cell)
```

---

## Invalidation

On lookup-relevant mutation:

```text
for cell in self.attached_lookup_cells:
    cell.valid = false
self.attached_lookup_cells.clear()

self.primary_lookup_cell = Value::not_present()
```

---

## Self Lookup

Self lookup uses:

- a lookup validity cell guarding the relevant class-chain assumptions
- `access_kind = AttributeAccessKind::ReceiverSlot`
- Shape + receiver-relative storage location

---

## Call Behavior

| Case | Condition | Behavior |
|---|---|---|
| Receiver slot | `access_kind == ReceiverSlot` | no implicit self |
| Resolved value | `access_kind == ResolvedValue` | no implicit self |
| Descriptor get | `access_kind == DataDescriptorGet` or `NonDataDescriptorGet` | descriptor decides binding |
| `__getattr__` fallback | `access_kind == GetAttrFallback` | fallback decides result |

---

## Bound Method Escape

Direct method-call syntax and method-value lookup are different semantic
cases.

For a direct call such as:

```text
obj.method(args...)
```

the runtime should use a method-call path that carries both:

- the resolved callable
- the receiver to use as `self` when binding rules require it

This path must not allocate an intermediate bound-method object merely to
call it immediately.

The preferred opcode shape for this direct-call form is a fused
attribute-call operation:

```text
CallMethodAttr name, cache_index, receiver, argc, args...
```

The opcode performs attribute lookup in call context and then calls the
resolved target. The inline cache records the lookup result and binding
convention, so the interpreter does not need to materialize an
intermediate `(callable, maybe_self)` pair in interpreter-visible
registers.

Different lookup results decide whether and how an implicit receiver is
inserted:

- receiver-local callable: call with the explicit arguments only
- descriptor that binds to the receiver: call with the receiver inserted
- descriptor that binds to the class: call with the class inserted
- descriptor that does not bind: call with the explicit arguments only
- custom lookup fallback: perform the generic lookup, then call the
  resulting value

At JIT compilation time, a populated `CallMethodAttr` inline cache should
identify the concrete path selected by the observed lookup. Compiled code
can then lower directly to that path with the cache's guards, instead of
emitting a runtime switch over all binding cases.

For an attribute lookup such as:

```text
f = obj.method
```

the resulting value is observable. If descriptor lookup produces a bound
method, the runtime must expose a first-class callable value with the
receiver bound according to the descriptor protocol.

The initial implementation may allocate a real bound-method wrapper only
when the method value escapes. Later implementations may replace that
wrapper with a smaller specialized callable representation, as long as
observable Python behavior is preserved.

The important split is:

- direct method calls may stay allocation-free on the hot path
- escaped method values pay for a real observable callable

---

## Garbage Collection

Inline caches must ensure that all pointers used on the fast path remain
live.

If an object has the cached receiver Shape, then that Shape is live and
may be dereferenced. From the receiver Shape, the runtime can reach the
object's class and that class's `__mro__`. Therefore any class object
consulted during lookup remains live through the receiver object and its
Shape.

If the lookup validity cell is valid, then the cached class-chain path
still resolves to the same cached value. The cache must keep that value
alive. Receiver-slot caches store only a storage kind and slot index
relative to the receiver; they must not rely on a raw pointer to a slot,
because overflow or extra-slot storage may be reallocated during
unrelated mutations.

The lookup validity cell itself is not reached through the receiver
object and must therefore be kept alive separately. In a refcounting
runtime, this is done by having inline caches (and class objects, only
when they're still valid) hold references to the cell. Lookup validity
cells do not themselves hold references to the classes they protect,
which avoids introducing reference cycles.

## Testing Direction

Prefer interpreter-level semantic tests for object-model behavior:

1. instance attribute store and load
2. class attribute lookup
3. direct method calls
4. class definition and instantiation
5. `__init__`-driven construction
6. escaped method values such as `f = obj.method`
7. attribute miss producing `AttributeError`
8. exception propagation across nested calls
9. `raise`
10. `try` / `except`

Keep codegen and JIT tests structural. They should pin down high-value
lowering guarantees such as:

- attribute bytecode shape
- method-call lowering
- class-definition lowering shape
- shape-guard and lookup-cell specialization decisions

## Summary

- Objects reference immutable Shapes
- Shapes define structure, type, and lookup behavior
- Descriptor order and slot index are separate
- Present attributes are insertion-ordered
- Latent predefined attributes preserve stable slot assignment
- Presence is tracked on the Shape
- Object slots mirror predefined not-present state with
  `Value::not_present()`
- Mutations that change membership cause transitions to new Shapes
