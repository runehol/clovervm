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

The current implementation only partially matches this shape.

Today, `IndirectDict` provides:

- a probe table
- an insertion-ordered name table

and `Scope` separately stores slot payloads by slot index.

What does not exist yet is an explicit ordered scope-entry layer that maps
ordered entries to stable slot indices. That still needs to be implemented.

So the current implementation is only close to the desired model; it does not
yet realize the full probe table -> ordered scope entry -> slot index -> value
structure.

The key consequence is that scope slot identity and mapping insertion order are
not the same thing.

## Scope Delete/Reinsert Semantics

Delete and reinsert should be handled as follows:

- deleting a name marks the slot as not present
- deleting a name tombstones the ordered mapping entry
- reinserting the same name reuses the original slot index
- reinserting the same name creates or reactivates a logical ordered entry at
  the end

This gives us the desired combination:

- compiled bytecode still sees a stable slot index
- Python-visible iteration can still behave like dictionary reinsertion

In other words:

- slot identity is permanent
- ordered-entry identity is not necessarily permanent

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
- storage kind: inline or overflow
- physical storage index

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
- optional transition metadata or transition pointers
- contiguous property-name storage
- compact raw packed descriptor metadata

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

If needed, the physical index can use a smaller packed integer width rather
than a full machine-word field.

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
  - ordered entries like `[key, slot_index]`
  - optional cached string hash

- `Shape`
  - ordered property descriptors like `[key, storage_kind, physical_index]`
  - optional side index later if needed

- `Dict`
  - ordered entries like `[hash, key, value]`

Scope does not need `[hash, key, value]` because the live value already lives
in the slot array. Duplicating it in the ordered table would add sync problems
without helping the compiled fast path.

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
in `Klass`. Different instances of the same high-level kind may have different
trailing storage sizes.

## Current `IndirectDict`

`IndirectDict` is already useful as a building block, but it should not be
treated as the final API for any of the three mapping families.

It is closest to the shared ordered-hash-table substrate:

- insertion-ordered name storage
- open-addressed probe table
- string-keyed lookup

Its current limitations are:

- uses `std::vector`
- only stores keys and slot indices indirectly through external structures
- does not express the final delete/reinsert semantics directly
- does not represent Python dict payloads
- does not represent shape transition semantics

The direction should be to replace its storage substrate with VM-managed arrays
and then specialize upward from there, rather than stretching it into a single
universal dictionary type.

## Near-Term Implementation Plan

1. Add the VM-managed storage support needed for variable-sized runtime
   objects.
2. Replace `IndirectDict` backing storage with VM-managed storage.
3. Preserve the current probe-table plus ordered-name-table shape for scope
   name lookup.
4. Introduce explicit ordered scope entries that map logical insertion order to
   stable slot indices.
5. Keep scope slot storage separate from ordered mapping entries, so slot
   identity remains stable across delete and reinsert.
6. Keep scope ordering and slot identity decoupled so a future `globals()` view
   can present dict-like iteration behavior without disturbing compiled slot
   identity.
7. Implement `Shape` as a single immutable contiguous allocation containing
   ordered property names, packed storage metadata, and transition metadata.
8. Preserve logical property order separately from physical storage choices so
   a future `obj.__dict__` view can present dict-like behavior without forcing
   unnecessary object-storage churn.
9. Implement Python `dict` separately, with full Python key, hash, equality,
   mutation, and insertion-order behavior. This should follow relatively soon
   after the object model work, even if `globals()` and `obj.__dict__` mapping
   views arrive later.

## Summary

The main rule is:

- execution uses stable specialized storage
- reflection uses mapping views with Python semantics

Those two concerns should be connected carefully, but they should not be forced
into one concrete dictionary implementation.
