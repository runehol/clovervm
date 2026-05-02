# Unified Object Model Transition Plan

## Current State

The runtime has mostly crossed from the old split model to the unified object
model described in [doc/unified-object-model.md](./unified-object-model.md).
The important completed pieces are:

- `Object` owns Python-visible class identity, current Shape, overflow storage,
  native layout id, and the shared own-property operations.
- User instances and class objects both use Shape-backed storage.
- `ClassObject` stores `__class__`, `__name__`, `__bases__`, and `__mro__` in
  predefined Shape-backed slots, with 48 fixed inline class slots for class
  attributes.
- `Shape` supports present and latent descriptors, stable slots, explicit
  storage locations, descriptor flags, Shape flags, and cached add/delete
  transitions.
- VM-specific builtin class objects exist for the current Python-visible native
  layouts, and `NativeLayoutId` has replaced the old static `Klass` type
  identity.
- Attribute lookup returns shared read/write descriptor records and executable
  plans.
- `CallMethodAttr` replaced the old method lookup/call split for direct method
  calls, with receiver and explicit arguments in one contiguous register span.
- Lookup validity cells exist, are lazily created per class, attach to base
  classes along the materialized MRO, and are invalidated by class membership
  changes and class slot updates.
- `LoadAttr`, `StoreAttr`, and `CallMethodAttr` have inline-cache side arrays on
  `CodeObject` and execute cacheable descriptor plans on their hot paths.
- Receiver-own-slot reads now carry the receiver class lookup validity cell, so
  existing instance attribute reads are cacheable while still being protected
  from later class/base mutations that introduce a data descriptor.

Relevant code:

- [src/object.h](../src/object.h)
- [src/object.cpp](../src/object.cpp)
- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)
- [src/shape.h](../src/shape.h)
- [src/shape.cpp](../src/shape.cpp)
- [src/attribute_descriptor.h](../src/attribute_descriptor.h)
- [src/attribute_cache.h](../src/attribute_cache.h)
- [src/attr.cpp](../src/attr.cpp)
- [src/interpreter.cpp](../src/interpreter.cpp)
- [src/code_object.h](../src/code_object.h)

## Current Attribute Pipeline

Attribute lookup is split into two levels:

1. **Resolution** returns an `AttributeReadDescriptor` or
   `AttributeWriteDescriptor`.
2. **Execution** runs the descriptor's successful plan against the current
   receiver.

The split matters because cached execution must be reusable for another object
with the same receiver Shape. Receiver-local plans therefore use
receiver-relative storage (`storage_owner == nullptr`), while class-chain plans
may point at the class object that owns the resolved slot.

Read descriptors currently cover:

- receiver-own slots
- instance class-chain hits
- class-object chain hits
- metaclass-chain hits
- function binding for instance method calls
- descriptor get classification as `DataDescriptorGet` or
  `NonDataDescriptorGet`

Write descriptors currently cover existing-slot writes. A missing write that
adds a new receiver-local property is deliberately not represented as a
cacheable write plan, because the add immediately changes the receiver Shape.

Descriptor `__get__`, `__set__`, and `__delete__` invocation is still surfaced
to the interpreter rather than performed inside lookup helpers. That boundary
is intentional: descriptor invocation may execute Python bytecode and must not
be hidden in an inlineable lookup path.

## Lookup Validity Invariants

For a class `C`:

- if `C.primary_lookup_validity_cell` is non-null and valid, it is the primary
  cell for lookups rooted at `C`
- that primary cell is attached to every base class in `C.__mro__[1:]`
- the primary cell is not attached to `C` itself; `C` invalidates it directly
- the hot getter only checks the pointer and the cell's valid bit
- the cold path creates a new cell and performs all MRO attachment

On class lookup-relevant mutation:

- invalidate every attached cell
- clear the attached-cell array, keeping its backing storage available
- invalidate the primary cell if present
- set the primary cell pointer to null

Any class-object Shape transition invalidates lookup cells. Successful writes
to an already-present class attribute also invalidate lookup cells, even though
they do not change the Shape, because the replacement value may change the
resolved value or descriptor behavior.

## Inline Cache State

`CodeObject` owns separate read and write cache arrays:

```cpp
std::vector<AttributeReadInlineCache> attribute_read_caches;
std::vector<AttributeWriteInlineCache> attribute_write_caches;
```

Read caches are used by `LoadAttr` and `CallMethodAttr`; write caches are used
by `StoreAttr`.

Each cache entry stores:

- `receiver_shape`
- the executable read or write plan

The cache hit test is:

- receiver is an object pointer
- receiver Shape matches
- the plan has a lookup validity cell
- the lookup validity cell is still valid

`AttributeCacheBlockers` remain useful diagnostic metadata on descriptors, but
the cache hit path is driven by the plan's lookup validity cell and by whether
the plan kind can be executed without falling into descriptor dispatch.

The current cacheable read plan kinds are the direct ones:

- `ReceiverSlot`
- `ResolvedValue`
- `BindFunctionReceiver`
- `ReturnValue`

`DataDescriptorGet` and `NonDataDescriptorGet` are intentionally not cached or
executed on the inline path yet; descriptor invocation needs interpreter-owned
dispatch.

## Remaining Work

### Finish Descriptor Semantics

The lookup machinery classifies descriptors, but full descriptor execution is
still incomplete.

Remaining work:

- execute descriptor `__get__` through interpreter-controlled call dispatch
- implement descriptor-aware writes and deletes for `__set__` and `__delete__`
- add `staticmethod`, `classmethod`, and `property`
- add `__set_name__` notification during class creation
- make escaped method values produce observable bound-method objects
- keep direct `CallMethodAttr` calls allocation-free on the hot path

### Finish Custom Attribute Hooks

Shape flags already have room for custom attribute behavior, but the runtime
does not yet implement the full custom hook story.

Remaining work:

- populate custom hook Shape flags when relevant attributes are added or
  removed
- implement `__getattribute__`, `__getattr__`, `__setattr__`, and `__delattr__`
- disable default attribute caches when these hooks are present unless a
  specialized hook cache exists

### Move More Builtin Semantics Onto the Shared Object Protocol

Builtin objects now have VM-specific builtin classes and Shapes, but generic
attribute behavior is still conservative for many native layouts.

Remaining work:

- decide per builtin type whether arbitrary instance attributes are supported
- reject unsupported arbitrary writes through Shape policy and shared helpers
- remove native-layout branches from generic attribute semantics when they are
  no longer needed for layout assertions
- keep low-level native dispatch, such as call and subscript dispatch, separate
  from Python-visible type semantics

### Improve Construction And Instance Layout Prediction

Class instantiation now has a tier-1 fast path for ordinary constructors. An
eligible class lazily owns a hidden constructor thunk guarded by its existing
MRO shape+contents validity cell. The thunk allocates the instance, enters the
resolved `__init__` with a prepared frame when one is present, and rejects
non-`None` initializer returns.

Remaining work:

- add outer `CallKw` support so keyword constructor calls can still reuse the
  prepared thunk body after call-site adaptation
- implement the generic construction path for custom `__new__`
- add the custom metaclass `__call__` story with an explicit revisioned guard
- normalize constructor errors into specific VM exceptions
- eventually recognize statically visible `__init__` member initialization and
  seed the instance root Shape/default inline slot count accordingly

### Tighten Cache And JIT Readiness

The first inline caches are working and already cover the important happy
paths, but the cache format is still the interpreter-era representation.

Remaining work:

- keep shaving cache-hit instruction count in `interpreter.cpp`
- add descriptor execution only through cold opcode paths or carefully factored
  interpreter dispatch
- decide whether read/write cache structures can share more representation
- keep `CallMethodAttr` lowering aligned with the same read-plan cache model
- add codegen/JIT-facing tests once specialization decisions become visible

### Runtime Scanning And Lifetime

Heap layout metadata is cleaned up enough for current object families, but
runtime object scanning and reclamation remain a separate unfinished slice.

Remaining work:

- implement runtime scanning of `HeapLayout` value regions
- verify inline-cache side arrays and validity cells keep referenced heap
  objects alive
- continue keeping internal non-`Object` heap records free to use custom value
  offsets when a full Python-visible object header would be wasteful
