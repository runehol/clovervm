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

### Attribute lookup is moving onto descriptor results

Attribute lookup now returns shared `AttributeReadDescriptor` /
`AttributeReadAccess` records. `Object` emits receiver-slot descriptors,
`ClassObject` emits instance-chain, class-chain, and metaclass-chain
descriptors, and `attr.cpp` composes the top-level lookup order. This gives the
interpreter and future inline caches a common representation for successful
access, misses, cache blockers, and call-context binding.

The current descriptor support is intentionally narrow but real: instance
lookup recognizes `__get__`, `__set__`, and `__delete__` on the candidate
value's type, data descriptors take precedence over receiver-local attributes,
and non-data descriptors run after receiver-local attributes. Descriptor
invocation is deliberately not performed inside `attr.cpp`; it must fall out to
interpreter opcode handlers because invoking `__get__` may execute Python
bytecode. Direct method-call bytecode now consumes `AttributeReadDescriptor`
results directly, so future inline caches can be attached to the semantic read
result instead of to the legacy `(callable, maybe_self)` adapter.

Relevant code:

- [src/attribute_descriptor.h](../src/attribute_descriptor.h)
- [src/attribute_descriptor.cpp](../src/attribute_descriptor.cpp)
- [src/attr.cpp](../src/attr.cpp)
- [src/object.cpp](../src/object.cpp)
- [src/class_object.cpp](../src/class_object.cpp)

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

### 3. Clean up heap layout metadata

Status: completed.

The heap layout metadata still carries `value_offset_in_words`, and that should
remain true for internal non-`Object` heap records such as Shape,
OverflowSlots, Scope, and array backing stores. Those records have useful raw
prefixes and should not be forced to carry the full Python-visible `Object`
header just to describe their scanned `Value` fields.

For Python-visible `Object` subclasses, the important cleanup is instead to
make inherited fixed `Value` counts compose safely. `Object` defines the common
Python-visible value region, and subclasses layer fixed `Value` members after
their base class. The layout declaration macros now support that explicitly.

Introduce layout declaration macros that understand static base layouts:

- a derived static layout macro should name its base class
- it should assert that the base class does not have dynamic layout
- it should reuse the base class value-region offset
- it should add the base class `static_value_count()` to the derived class's
  own fixed `Value` field count
- it should assert that the named base class is actually a C++ base class

Implementation notes:

- inherited static and dynamic layout declaration macros now live in
  `heap_object.h`
- current `Object` subclasses use those inherited declarations
- keep dynamic layout support for variable tail counts
- keep per-layout value-region offsets for internal heap records
- update [doc/object-metadata.md](./object-metadata.md) if the runtime layout
  encoding changes in the future

Primary files:

- [src/heap_object.h](../src/heap_object.h)
- [src/heap.h](../src/heap.h)
- [src/object.h](../src/object.h)
- [src/class_object.h](../src/class_object.h)
- [src/instance.h](../src/instance.h)
- [doc/object-metadata.md](./object-metadata.md)

### 4. Unify builtin instance attribute policy

Status: partially complete.

Builtin runtime objects receive VM-specific builtin classes during normal
Object construction and now have Shapes through their class instance root
Shapes. The remaining work is to finish generic attribute semantics for builtin
instances and make `attr.cpp` stop branching on native layout except for
genuinely native behavior.

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

- audit each builtin native layout for fixed native fields versus dynamic
  Python-visible attributes
- reject ordinary attribute writes through the generic path when the builtin
  type does not yet support extra attributes
- keep `__class__` lookup on object-backed builtin values covered by attr tests

Primary files:

- [src/str.h](../src/str.h)
- [src/list.h](../src/list.h)
- [src/dict.h](../src/dict.h)
- [src/function.h](../src/function.h)
- [src/builtin_function.h](../src/builtin_function.h)
- [src/virtual_machine.cpp](../src/virtual_machine.cpp)
- [src/attr.cpp](../src/attr.cpp)

### 5. Reframe `Klass` as native implementation metadata

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

### 6. Make class-object `__class__` a real metaclass slot

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

### 7. Move generic attribute access onto the object protocol

Status: partially complete.

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

Current progress:

- `AttributeReadDescriptor` and `AttributeReadAccess` are shared runtime
  records.
- `Object` emits receiver-local slot descriptors.
- `ClassObject` emits instance-chain, class-chain, and metaclass-chain
  descriptors.
- `attr.cpp` composes the top-level lookup order and executes descriptors for
  the legacy load APIs.
- receiver-slot descriptors are executable plans, not captured-value snapshots.
- receiver-slot descriptors carry `MissingLookupCell` until class-chain
  descriptor-precedence dependencies are represented.

Remaining work:

- keep moving protocol-specific decisions out of `attr.cpp` where they belong
  on object/class helpers
- remove remaining native-layout checks from generic attribute semantics where
  they are not low-level layout assertions
- add delete and write descriptor paths alongside read descriptors

Primary files:

- [src/attribute_descriptor.h](../src/attribute_descriptor.h)
- [src/attr.cpp](../src/attr.cpp)
- [src/object.h](../src/object.h)
- [src/object.cpp](../src/object.cpp)
- [src/class_object.h](../src/class_object.h)
- [src/class_object.cpp](../src/class_object.cpp)
- [src/instance.h](../src/instance.h)
- [src/instance.cpp](../src/instance.cpp)

### 8. Complete descriptor and custom attribute semantics

Status: partially complete.

The current lookup code preserves existing method binding behavior and now has
the first descriptor-precedence slice:

- descriptor protocol classification looks for `__get__`, `__set__`, and
  `__delete__` on the candidate value's type
- data descriptors win over receiver-local attributes
- receiver-local attributes win over non-data descriptors
- non-data descriptors run after receiver-local lookup misses
- descriptor invocation is surfaced as `DataDescriptorGet` or
  `NonDataDescriptorGet`
- descriptor get accesses carry `UnsupportedDescriptorKind`; direct method
  calls route those cases through a cold interpreter error path until opcode
  handlers can execute descriptor calls

The full unified model still needs the rest of Python descriptor behavior before
caches are worth building broadly. Lookup validity cells and narrow inline
caches can still begin before all descriptors are implemented, as long as cache
eligibility stays conservative: unsupported descriptor access kinds, mutable
descriptor protocol, and custom attribute hooks must block caching rather than
being approximated.

Add or complete:

- interpreter opcode execution for descriptor `__get__`
- general user-function `__get__`, `__set__`, and `__delete__` invocation
- descriptor-aware writes and deletes
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

### 9. Combine method lookup and method call bytecodes

Status: complete for the current no-cache-index bytecode shape.

Before adding lookup validity cells, replace the current `LOAD_METHOD` /
`CALL_METHOD` split with one combined method-call opcode. The split opcode pair
is a useful interpreter shortcut today, but it is the wrong shape to cache
directly: the interesting semantic unit is resolving an attribute in call
context and immediately invoking it with the correct binding behavior.

The combined opcode should:

- keep the receiver and explicit arguments visible to one interpreter handler
- perform method-context attribute resolution once
- decide whether a receiver argument is inserted based on the resolved access
  kind
- avoid materializing a transient callable/self pair as the bytecode contract
- leave ordinary escaped method-value lookup to normal attribute load semantics

The current fused opcode is `CallMethodAttr receiver_and_arg_span, name, argc`.
It deliberately skips the future cache-index operand; adding that operand is
part of inline-cache integration rather than the semantic opcode transition.
Codegen lays out one contiguous register span:

```text
receiver, explicit_arg0, explicit_arg1, ...
```

The interpreter handler resolves an `AttributeReadDescriptor` in call context
and then either uses the receiver slot as the implicit first argument or calls
with the explicit-argument tail of the same span. It may use private handler
state for the resolved callable, but no interpreter-visible `(callable,
maybe_self)` pair is produced. Descriptor `__get__` access kinds are surfaced
explicitly and rejected through a cold opcode path until descriptor invocation
is implemented in interpreter-controlled call machinery.

This gives lookup caches a single call-context operation to specialize later.
The cache can then record the semantic result of the call lookup rather than
coupling itself to the current two-op stack/register convention.

Primary files:

- [src/bytecode.h](../src/bytecode.h)
- [src/codegen.cpp](../src/codegen.cpp)
- [src/interpreter.cpp](../src/interpreter.cpp)
- [src/attr.cpp](../src/attr.cpp)
- [tests/test_codegen.cpp](../tests/test_codegen.cpp)
- [tests/test_interpreter.cpp](../tests/test_interpreter.cpp)

### 10. Add lookup invalidation only after the object model is unified

The unified-object-model doc describes lookup validity cells as the long-term
replacement for ad hoc invalidation. That work should come after classes and
instances, builtin objects, and builtin type objects all use the same
shape-based lookup model.

This does not require every descriptor feature to be complete first. It does
require cache entries to represent descriptor-related uncertainty honestly:
cache only successful access kinds that the interpreter can execute directly,
and let `AttributeCacheBlockers` reject descriptor calls, custom attribute
hooks, mutable descriptor protocol, and missing lookup-cell dependencies until
those paths have precise invalidation.

The first validity-cell implementation should establish the invariants before
adding any inline-cache fast path: a class primary cell is created only by the
cold get-or-create path, that creation path attaches the cell to every base
class in the materialized MRO, and invalidation clears attached cells plus the
class's own primary cell.

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
2. Clean up heap layout metadata so inherited fixed `Value` fields compose
   safely.
3. Tighten the remaining ClassObject fixed-slot layout cleanup.
4. Move generic attribute access onto the shared object protocol.
5. Complete descriptor and custom attribute semantics.
6. Combine method lookup and method call into one opcode.
7. Add lookup validity cells and inline-cache integration.

## Main Risk To Avoid

The biggest trap now is adding lookup validity cells before attribute semantics
are actually unified. A cache built around special-cased builtin objects would
immediately need a second design once builtin instance lookup moves fully onto
the shared object protocol.

So the critical sequencing rule for the next phase is:

- builtin instance attribute semantics first
- generic object-protocol lookup second
- lookup caches last
