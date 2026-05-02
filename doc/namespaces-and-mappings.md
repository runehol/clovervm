# Namespaces And Mapping Views

## Goal

We need to reconcile two different pressures in the runtime:

- execution wants stable slot-based access
- Python reflection wants mutable ordered mappings

These should agree semantically without forcing all namespace-like structures to
share the same C++ representation.

## The Three Mapping-Like Structures

There are three related but distinct structures to support:

1. `Scope`
   A name-to-slot structure used by compiled code and runtime scope lookup.

2. `Shape` / instance property metadata
   An insertion-ordered property-descriptor structure used to describe instance
   layout and shape transitions.

3. Python `dict`
   A general-purpose mapping with normal Python key, hash, equality, mutation,
   and insertion-order semantics.

These should not be forced into a single runtime type.

## Common Direction

All three structures can use the same broad storage idea:

- an ordered entry table
- an open-addressed probe table
- VM-managed arrays rather than `std::vector`

The probe table is the hash table used for lookup. It stores positions into the
ordered entry table together with special values for empty and tombstone slots.

The ordered entry table preserves logical insertion order.

The shared pattern is useful for some structures, but the payload and semantics
differ by user:

- scope entry: name plus stable slot identity
- shape entry: ordered property descriptor
- Python dict entry: hash plus key plus value

## String-Keyed Internal Tables Versus Python Dict

`Scope` and object-attribute lookup are string-keyed internal mechanisms.

That means:

- keys are strings only
- lookup should not invoke arbitrary Python `__hash__` or `__eq__`
- hashes can be recomputed from the string when needed
- storing the hash is an optimization, not a semantic requirement

Python `dict` is different:

- keys may be any hashable object
- hashing may invoke Python-level `__hash__`
- equality may invoke Python-level `__eq__`
- stored per-entry hashes are part of the natural representation

This is a strong reason not to couple Python `dict` implementation with the
name-resolution path for scopes or object attributes.

## Scope Invariants

`Scope` is not just a mapping from name to value. It is a mapping from name to
stable slot identity, where compiled code embeds slot indices directly into
bytecode.

For example, global and local variable bytecodes are emitted with concrete slot
indices chosen at codegen time. That makes the slot table part of the compiled
ABI of the code object.

Therefore:

- once a scope slot index is allocated, it must remain valid for compiled code
- deleting a variable must not destroy the slot identity
- reinserting a deleted variable must reuse the original slot

This is different from normal Python `dict` behavior, where delete plus
reinsert moves the key to the end and creates a fresh logical mapping entry.

## Scope Storage Model

To support both compiled slot access and Python-visible mapping order, scope
should use two levels of identity:

- stable slot identity
- logical ordered mapping entry

The intended lookup shape is:

- probe table -> ordered scope entry
- ordered scope entry -> slot index
- slot index -> live value / `not_present` state

The current direction for `Scope` is now more slot-centered than the original
`name -> entry -> slot` sketch.

The implementation has moved away from `IndirectDict` and now treats the slot
as the primary identity:

- probe table maps `name -> slot index`
- ordered entry table stores insertion order as `slot index` records
- slot index addresses the live value storage directly

The slot itself also carries the metadata needed for slot-addressed mutation:

- canonical slot name for named namespace slots
- current ordered-entry index

This means the effective lookup shape for scopes is:

- probe table -> slot index
- slot index -> live value
- slot index -> current ordered entry when order metadata is needed

That is a deliberate specialization for scopes. It keeps slot-addressed
bytecodes natural while still preserving a distinct ordered-entry layer for
future mapping views.

The key consequence remains the same:

- slot identity is stable
- mapping insertion order is separate

but the indirection now hangs off the slot rather than sitting on the hot name
lookup path.

## Scope Delete/Reinsert Semantics

Delete and reinsert should be handled as follows:

- deleting a name marks the slot as not present
- deleting a name may leave the current ordered entry in place temporarily
- reinserting a deleted variable tombstones the superseded ordered entry when a
  fresh ordered occurrence is created
- reinserting the same name reuses the original slot index
- reinserting the same name creates or reactivates a logical ordered entry at
  the end

This gives us the desired combination:

- compiled bytecode still sees a stable slot index
- Python-visible iteration can still behave like dictionary reinsertion

In other words:

- slot identity is permanent
- ordered-entry identity is not necessarily permanent

For the current scope implementation, an ordered entry can also be absent
entirely for a tracked-but-never-bound name. This happens when a scope creates
a placeholder slot only to cache parent-scope lookup information. Such a slot
has a stable name and slot index, but no ordered-entry representative until it
is actually bound locally.

## Parent Scope Lookup

The current scope design also precomputes parent-scope slot links.

When a scope registers a read for a name that is not present locally, it also
registers the name in the parent scope and stores the parent slot index inside
the local slot's `not_present` value.

This means global or builtin lookup can become:

- local slot access
- if not present, direct access to parent slot by precomputed index

rather than repeated name probing at runtime.

This is an important property to preserve when changing the storage
representation.

Because stores can also happen by slot index alone, scope must be able to
reconstruct local-binding insertion semantics from the slot itself. That is why
the slot metadata keeps:

- the canonical name for named namespace slots
- the current ordered-entry index, or `-1` if no ordered entry exists yet

This lets slot-addressed stores revive a deleted binding by:

- reusing the stable slot
- appending a fresh ordered entry at the end
- tombstoning the superseded ordered entry if there was one

## Object Attribute Storage And Mapping Views

Objects have a similar but not identical pressure.

For objects, slot indices are shape-relative rather than globally permanent:

- a property name maps to a slot under a particular shape
- optimized access is valid only under a shape guard
- deleting or adding a property naturally corresponds to a shape transition

This gives more freedom than scopes, because object slot indices are not baked
into bytecode unconditionally. They are only valid under the appropriate shape
assumptions.

However, Python-visible mapping behavior still matters.

`obj.__dict__` should therefore not expose the raw shape table directly.
Instead it should be a mutable mapping view over the object itself:

- reads consult current instance attributes
- writes perform normal attribute-store semantics
- deletes perform normal attribute-delete semantics
- shape transitions happen through those operations

This keeps one semantic path for:

- `obj.x = 1`
- `setattr(obj, "x", 1)`
- `obj.__dict__["x"] = 1`

The mapping view must present normal dictionary-like behavior even if the
underlying object storage is shape-based.

## Shape Representation

Shapes should not be treated as ordinary dictionaries in the first
implementation.

The common case for shapes is:

- few properties
- string keys only
- shared by many instances
- immutable after publication

That suggests a compact insertion-ordered descriptor table rather than a hash
table.

The first representation should therefore be:

- an ordered array of property descriptors

where each descriptor contains:

- property name
- storage location

where storage location is:

- storage kind: inline or overflow
- physical storage index within that storage

The logical property index can simply be the descriptor index in the shape.

This gives:

- deterministic property order
- compact storage
- simple linear-scan lookup in the common case
- no need for tombstones or resize machinery inside published shapes

For immutable shapes, prefer a single contiguous allocation rather than
multiple separately allocated helper arrays.

The likely layout is:

- fixed shape header
- out-of-line transition metadata or transition pointers
- contiguous property-name storage in the trailing `Value` region
- trailing raw storage-location metadata after the `Value` prefix

This keeps published shapes compact and read-friendly.

If shapes later become large enough that linear scan is not good enough, an
optional probe table can be added as an indexing aid without changing the
fundamental ordered descriptor representation.

## Shape Order Versus Physical Storage

For shapes, logical property order and physical object storage location should
be treated as separate things.

Logical property order is represented by descriptor order in the shape.

Physical storage location is represented by:

- inline versus overflow storage kind
- physical slot index within that storage

This separation is important for delete and reinsert behavior.

If a property is deleted and later reinserted:

- the new shape should place the logical property entry at the end
- the runtime may still reuse an existing physical slot if that is safe and
  profitable

That allows:

- Python-visible insertion-order behavior
- efficient reuse of object storage
- avoiding unnecessary overflow-array churn

So for object storage, the useful indirection is:

- shape descriptor order gives logical mapping order
- descriptor metadata gives physical storage location

Those two should not be forced to coincide.

For the first implementation, compact packed raw metadata is likely preferable
to several parallel tiny arrays. A good fit is:

- contiguous property-name storage in the `Value` region
- a compact raw metadata tail storing storage kind and physical index

That is now the chosen direction for `Shape`.

The current runtime design uses:

- an immutable shape payload allocated inline with the `Shape` object
- trailing `Value` cells for descriptor names
- trailing raw `StorageLocation` metadata immediately after those values
- out-of-line transition cache records that may be populated after publication

This keeps the hot descriptor payload contiguous while still allowing shape
transition caching to grow over time.

If needed, the physical index can use a smaller packed integer width rather
than a full machine-word field.

The runtime also keeps a monotonic `next_slot_index` on each shape family.
That counter is not stored per descriptor. It is only used to allocate the
next logical slot when creating a successor shape.

Current delete/reinsert policy for shapes is:

- deleting a property removes its descriptor from the successor shape
- deleting a property does not decrement `next_slot_index`
- reinserting the same property name appends a fresh descriptor at the end
- reinserting may reuse an existing storage location when that location is part
  of a fixed latent descriptor; otherwise the runtime can choose a fresh
  storage location

This means shape order remains dictionary-like even though physical storage is
specialized.

## Globals, Locals, And Mapping Views

`globals()` should return a live mutable mapping view over the module/global
scope.

Module-scope `locals()` should behave the same way, since module globals and
locals are the same namespace.

Function-scope `locals()` is different. The compiler lowers local-variable
accesses to slots, so function `locals()` should be treated as a derived view,
not the primary representation.

The intended direction is:

- module `globals()`: live mutable mapping view
- module `locals()`: same live mapping view
- function `locals()`: snapshot dict

This follows modern CPython direction and keeps reflective function-locals
behavior from dominating the local-variable fast path.

## Future Module Objects And Global Lookup

Module objects should eventually become the primary home for module globals.

The attractive long-term direction is for a module to be a normal shape-backed
object whose namespace is stored through the object/shape property machinery.
That would make these operations agree on one backing store:

- bytecode global access in the module
- `module.x`
- `globals()["x"]`
- the module's eventual `__dict__` mapping view

In that design, global lookup is similar to a tiny namespace-chain or MRO:

- first search the module namespace
- if absent, search the builtins namespace

The analogy is useful, but the semantics are simpler than class attribute
lookup. Module global lookup does not need descriptors, metaclasses,
instance/class precedence, bound methods, or descriptor-kind cache
classification. It is only a string-keyed namespace chain.

Reads, writes, and deletes deliberately use different parts of that chain:

- `LOAD_GLOBAL x`: module namespace, then builtins namespace
- `STORE_GLOBAL x`: module namespace only
- `DEL_GLOBAL x`: module namespace only
- `module.x`: module namespace only

This means builtin fallback is a lookup rule, not a storage rule. A missing
module binding should not be represented as a module attribute whose value is a
builtin. Likewise, deleting `x` must never delete `builtins.x`; it should only
delete the module's own binding and raise `NameError` if that binding is not
live.

If modules become shape-backed, global inline caches can reuse the broad
invalidation machinery from object/class attribute caches while keeping a
module-specific plan:

- module-hit plans guard the module namespace and ignore builtins
- builtin-hit plans guard both module absence and the builtin namespace
- module stores and deletes invalidate module namespace caches
- builtin mutation invalidates builtin-hit caches

This would be a nice unification with the object model, but it should not be
confused with the current `Scope` parent-slot shortcut. `Scope` stores
`not_present(parent_slot_idx)` holes to accelerate nested or builtin lookup.
Those holes are an implementation detail of scope lookup, not observable
module attributes. A future module object should not expose those parent
lookup holes through `module.x` or `__dict__`.

## Shape Tables Are Not Python Dicts

Shape metadata should remain VM-internal and specialized.

Shapes differ significantly from both scope tables and Python dicts:

- shapes are immutable after publication
- shape mutation is represented as transition to another shape
- shapes are naturally represented by ordered descriptors rather than live hash
  entries
- shape transition lookup is a first-class operation

Because of that, shape representation should not be forced into the same
concrete implementation as `ScopeNameTable` or Python `dict`, even if some
small low-level helpers are shared.

## Python Dict Remains Separate

Python `dict` should have its own implementation path.

It may reuse low-level storage helpers such as:

- VM-managed arrays
- probing utilities
- tombstone conventions

But it should not share the same high-level implementation as scope or object
attribute name tables.

The main reasons are:

- arbitrary key types
- Python-level hash and equality semantics
- re-entrancy concerns
- different mutation and ordering invariants

For Python `dict`, an ordered entry table like `[hash, key, value]` is the
natural representation.

For scope and shape tables, other payload layouts are more appropriate.

## Suggested Concrete Entry Shapes

The likely direction is:

- `ScopeNameTable`
  - string-keyed probe table mapping `key -> slot_index`
  - optional cached string hash

- `Scope` ordered entries
  - ordered records like `[slot_index]`
  - tombstoned by storing `-1`

- `Scope` slot metadata
  - cold metadata like `[key-or-None, current_entry_index, extra]`

- `Scope` slot values
  - hot payload cells like `[value]`

- `Shape`
  - ordered property descriptors like `[key, storage_location]`
  - storage location is `[storage_kind, physical_index]`
  - monotonic `next_slot_index` stored on the shape, not per descriptor
  - optional side index later if needed

- `Dict`
  - ordered entries like `[hash, key, value]`

Scope does not need `[hash, key, value]` because the live value already lives
in the slot-value array, while the canonical name lives in cold slot metadata.
Duplicating either in the ordered table would add sync problems without helping
the compiled fast path.

## GC-Friendly Object Layout

For VM-managed variable-sized objects, the variable-sized payload should place
all GC-visible `Value` cells first and raw storage after them.

That is:

- first a prefix of `Value` cells in the payload
- then a suffix of raw cells such as bytes, integers, probe-table contents, or
  inline character data

This allows collectors or decref walkers to scan the `Value` prefix only.

The fixed C++ object header can stay pragmatic and use ordinary integer fields
where that makes implementation easier. The main layout rule applies most
strongly to the trailing variable-sized payload.

The relevant payload metadata belongs in the object or allocation layout, not
in Python-visible type identity. Different instances of the same high-level
kind may have different trailing storage sizes.

## Current `IndirectDict`

`IndirectDict` is already useful as a building block, but it should not be
treated as the final API for any of the three mapping families.

It is closest to the shared ordered-hash-table substrate:

- insertion-ordered name storage
- open-addressed probe table
- string-keyed lookup

Its current limitations are:

- still models only string-keyed indirection rather than a full dict payload
- only stores keys and slot indices indirectly through external structures
- does not express the final delete/reinsert semantics directly
- does not represent Python dict payloads
- does not represent shape transition semantics

Its VM-managed storage substrate is now in place, so the remaining direction is
to specialize upward from there rather than stretching it into a single
universal dictionary type.

For `Scope`, that specialization has now effectively happened in-place:

- `Scope` owns its own string-keyed probe table
- `Scope` owns its own ordered entry array
- `Scope` owns split hot/cold slot storage

So `IndirectDict` should no longer be treated as the likely long-term substrate
for scope lookup itself.

## Scope Progress

The current implementation has completed the first specialization step for
scopes:

- `IndirectDict` has been removed from `Scope`
- scope lookup is now `name -> slot index`
- ordered scope entries exist and are distinct from slots
- reinsertion reuses the original slot and appends a fresh ordered entry
- slot-addressed stores can revive deleted bindings correctly
- slot storage is split into hot value cells and cold metadata
- `Scope` backing storage now uses VM-managed arrays instead of `std::vector`
- `IndirectDict` backing storage now uses VM-managed arrays instead of
  `std::vector`
- tracked-but-never-bound names may have a slot with no ordered entry yet

What remains for scope-related work is mostly cleanup and follow-on layering:

- decide how future mapping views should iterate and filter scope entries
- keep the slot/entry invariants clear as object-model work lands

## Shape Progress

The current implementation has moved beyond the first raw storage slice and now
uses Shapes as the ordinary object-attribute substrate:

- `Shape` exists as its own runtime type
- shape identity is pointer identity
- shapes are immutable in payload after publication
- transition caches may still grow after publication
- shape descriptors are stored inline in the `Shape` allocation
- descriptor lookup is insertion-ordered linear scan
- descriptor metadata resolves directly to a storage location
- Python-visible `Object` subclasses store their current Shape and fixed
  `__class__` slot through the shared `Object` layout
- ordinary object storage is split into inline slots and overflow slots
- class objects and user instances both use Shape-backed own-property storage
- own-property get/set/add/delete helpers are implemented for string keys
- deleting a property clears its old slot to `not_present`
- shape back-pointers are non-owning to avoid refcount cycles
- attribute lookup returns shared descriptor/plan records
- `LoadAttr`, `StoreAttr`, and `CallMethodAttr` have side-array inline caches
  guarded by receiver Shape and lookup validity cells

What remains for shape-related work is now mostly semantic layering:

- full descriptor protocol execution
- custom attribute hooks such as `__getattribute__`, `__getattr__`,
  `__setattr__`, and `__delattr__`
- `obj.__dict__` and other mapping views
- later optimization work such as selective cache invalidation and JIT guards

## Near-Term Implementation Plan

1. Keep the current `name -> slot` probe-table shape for scope lookup.
2. Keep explicit ordered scope entries distinct from slot storage so logical
   insertion order remains decoupled from stable slot identity.
3. Keep slot metadata rich enough to support slot-addressed revival and parent
   lookup without repeated name probing.
4. Keep scope ordering and slot identity decoupled so a future `globals()` view
   can present dict-like iteration behavior without disturbing compiled slot
   identity.
5. Keep `Shape` as a single immutable contiguous allocation containing ordered
   property names and packed storage metadata, with transition caches growing
   separately.
6. Preserve logical property order separately from physical storage choices so
   a future `obj.__dict__` view can present dict-like behavior without forcing
   unnecessary object-storage churn.
7. Keep attribute-cache dependencies expressed through receiver Shape guards
   and lookup validity cells rather than baking class-chain facts directly into
   mapping views.
8. Implement Python `dict` separately, with full Python key, hash, equality,
   mutation, and insertion-order behavior. This should follow relatively soon
   after the object model work, even if `globals()` and `obj.__dict__` mapping
   views arrive later.

## Summary

The main rule is:

- execution uses stable specialized storage
- reflection uses mapping views with Python semantics

Those two concerns should be connected carefully, but they should not be forced
into one concrete dictionary implementation.
