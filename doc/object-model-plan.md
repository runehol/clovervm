# Object Model And Exceptions Plan

## Goal

Add a Python class/object model that can eventually support JIT-specialized
attribute access without dictionary lookups on the hot path, while staging
exceptions in a way that does not force a giant all-at-once runtime rewrite.

The intended steady state is:

- objects carry a shape identifier
- the shape determines slot layout
- instance attribute loads/stores can specialize on shape
- JIT code can guard on shape and then perform direct memory loads/stores
- method calls avoid allocating temporary bound-method objects on the fast path

## Constraints From The Current VM

The current runtime already has a few useful ingredients:

- all heap values already flow through `Object` / `Klass`
- scopes already use slot-index-based lookup for fast variable access
- bytecode and interpreter dispatch are simple enough that we can still change
  call and attribute protocols without huge compatibility baggage
- exception handling is still mostly `std::runtime_error`, so we have not yet
  committed to a Python-visible exception model

That makes this a good time to define the object model before object-heavy
features spread through the VM.

## High-Level Direction

Separate three concerns that Python often entangles:

1. Storage layout
2. Attribute lookup semantics
3. Call/method binding semantics

The storage layout should use hidden-class-style shapes and slots.

Attribute lookup should still be Pythonic:

- instance dictionary-style semantics
- class attribute lookup
- later inheritance and descriptors

But the fast path should not literally execute those semantics via dictionary
lookup every time. Instead, it should:

- guard on shape
- use cached slot offsets or class-member cells
- deopt or fall back when the guard fails

## Runtime Objects To Add

### `Shape`

Represents the layout of an instance.

Minimum contents:

- stable shape id
- pointer to owning class
- number of in-object slots
- mapping from attribute name to slot index
- transition table for "add attribute X" -> next shape
- optional flags for future optimizations

Properties:

- immutable after publication
- shared by many instances
- cheap to compare by pointer or id

### `Class`

Represents a Python class object.

Minimum contents:

- class name
- class dictionary / member table
- inline single-base pointer for the common case
- optional base-class list for multiple inheritance
- optional linearized MRO storage
- pointer to the initial instance shape
- version tag for member-table invalidation later
- constructor / call metadata if needed

### `Instance`

Represents a Python object instance.

Minimum contents:

- pointer to `Class`
- pointer to current `Shape`
- inline or trailing slot storage
- optional overflow slot array pointer

The important invariant is that `Shape` fully determines the slot layout.

### `Exception`

Represents a Python-visible exception object.

Minimum contents:

- exception class
- payload / args tuple later
- optional source location later

The first implementation can keep this extremely small.

## Shape Model

The shape model should follow the common hidden-class transition approach.

Example:

```python
obj.x = 1
obj.y = 2
```

could evolve shapes like:

- empty shape `S0`
- after storing `x`, transition to `S1`
- after storing `y`, transition to `S2`

If another instance receives attributes in the same order, it reuses the same
shape chain and therefore the same slot layout.

That gives us:

- compact instance storage
- stable offsets for JIT specialization
- predictable fast paths for common object shapes

## Attribute Lookup Strategy

Stage attribute access in layers.

### Stage 1: Correct but simple

Implement:

- `LoadAttr`
- `StoreAttr`

with interpreter semantics only.

Behavior:

- instance slot lookup via shape metadata
- if attribute is not present on the instance, consult class members
- class-member lookup walks the class and then the class MRO until miss
- if still not found, raise attribute error later

This stage can use helper calls and does not need inline caches yet.

### Stage 2: Monomorphic inline caches in the interpreter

Add a cache entry per attribute access site:

- expected shape id
- resolved slot index or class-member cell
- optional version tag for class dictionary

Fast path:

- if object shape matches, load/store directly
- otherwise call the generic lookup helper and refresh or invalidate cache
- class-member caches must account for class and ancestor invalidation

This will also give the JIT a natural representation for guards.

### Common-case optimization

The representation should optimize hard for the overwhelmingly common case:

- one class
- zero or one base class
- methods defined directly on the class or its single base

We should not pay an extra indirection on every attribute or method lookup just
to keep the door open for multiple inheritance.

Recommended representation:

- keep an inline `base` pointer on `Class` for the single-inheritance fast path
- only allocate/store a separate base-class array when the class actually has
  multiple bases
- only allocate/store an explicit MRO object when needed beyond the trivial
  single-base chain

Fast-path lookup can then be:

- current class
- inline base chain

while the generic lookup helper can switch to full MRO traversal for
multiple-inheritance classes.

### Stage 3: JIT specialization

Lower cached attribute sites into:

- guard on shape id or shape pointer
- direct memory load/store
- deopt / slow-path branch on miss

At this stage the hidden-class model starts paying off in the generated code.

## Method Binding Without Bound-Method Allocation

Avoid allocating bound-method objects on the common immediate-call path.

Instead, distinguish these cases:

### Case 1: `obj.method(...)`

Compile this as a dedicated method-call path, not as:

1. load attribute
2. allocate bound method
3. call bound method

Instead:

- perform attribute lookup with a "call context"
- if the resolved member is a function defined on the class, call it with
  `self` inserted as argument 0
- if the resolved member is a non-function descriptor later, fall back to the
  generic protocol

Possible bytecode shape:

- `CallMethod <name>, <argc>`
- or `LoadMethod <name>` followed by `CallMethod`

Either approach is fine; the important part is that the fast path carries both:

- resolved callable
- receiver `self`

without allocating an intermediate object.

### Case 2: `f = obj.method`

Here Python does require a first-class callable value with `self` bound.

For this case we have options:

- initially allocate a real bound-method object only when the method escapes
- later use a tiny pair object or specialized callable wrapper

This keeps immediate method calls allocation-free while preserving Python
semantics when the method value is observed.

### Recommended rule

- optimize direct method call syntax first
- support escaping method values second
- do not block the whole object model on a perfect no-allocation answer for
  escaping bound methods

That mirrors what many VMs do: optimize the overwhelmingly common call pattern,
and let the rarer "store the method object" path pay more.

## Inheritance And MRO

Python attribute and method lookup follow the class MRO, not a naive recursive
left-to-right walk.

Important points:

- lookup starts on the concrete class
- Python computes a C3 linearization for the class hierarchy
- the MRO visits each class at most once in a deterministic order
- `super()` means "next class in the MRO", not simply "immediate parent"

For example, in a diamond:

- `D(B, C)`
- `B(A)`
- `C(A)`

the MRO is:

- `D`
- `B`
- `C`
- `A`
- `object`

Design implication:

- all generic member lookup APIs should be phrased in terms of MRO traversal
- the single-inheritance fast path may use the inline `base` chain directly
- multiple-inheritance classes must either cache or precompute a linearized MRO
- cache invalidation should treat any class appearing in the effective MRO as a
  dependency

## Instance Storage Strategy

Instances cannot rely on in-place growth once they are widely referenced.

Recommended layout:

- fixed instance object header
- trailing inline `Value` slots sized at allocation time
- optional overflow slot array pointer for out-of-object properties

Behavior:

- shapes assign low-numbered properties to inline slots first
- once inline capacity is exhausted, new properties are assigned to overflow
  storage
- the first overflow write allocates an external slot array
- later overflow growth reallocates only the external slot array, never the
  instance object itself

This follows the same basic idea as V8's out-of-object property storage while
keeping the instance pointer stable.

The shape must therefore encode, for each property slot:

- logical slot index
- whether the slot is inline or overflow
- the physical index within that storage area

## Exception Staging

Exceptions should be staged separately from Python exception syntax.

### Stage A: Internal VM unwind

Replace scattered `std::runtime_error` control flow with a VM-level unwind
mechanism.

Needed behavior:

- opcode helpers can signal failure
- the interpreter can unwind frames
- call sites can propagate failure without C++ exceptions being the semantic
  model

Representation options:

- special exception marker plus thread-local pending exception object
- or explicit result type carrying either value or exception

The current `Value::exception_marker()` suggests the first option fits the
existing code well.

### Stage B: Python-visible exception objects

Add:

- base exception classes
- `TypeError`, `NameError`, `AttributeError`, `ValueError`
- helper constructors for runtime errors

Now current runtime failures can become real exception objects.

### Stage C: Syntax and handlers

Only after A and B:

- `raise`
- `try` / `except`
- later `finally`

This keeps syntax work from outrunning runtime machinery.

## Recommended Implementation Order

### Milestone 1: Object-layout substrate

- define `Shape`, `Class`, and `Instance` runtime objects
- use trailing inline cells plus optional overflow slot array storage
- implement shape transitions for adding instance attributes
- implement a minimal class member table with ancestor traversal

This milestone does not need parser support for `class` yet if manual test
construction is easier.

### Milestone 2: Attribute bytecodes and interpreter semantics

- add AST/codegen support for attribute access and assignment
- add `LoadAttr` / `StoreAttr`
- implement interpreter helpers using shape metadata
- raise a real internal attribute failure instead of generic errors

### Milestone 3: Internal exception machinery

- replace generic throw-based runtime control flow with pending-exception +
  unwind support
- teach calls and returns to propagate exception state
- switch existing name/type/value errors onto this path

This milestone is valuable even before Python `try` exists.

### Milestone 4: Method-call fast path

- add method-call-aware lookup or bytecode
- optimize `obj.method(args...)` without bound-method allocation
- only allocate a bound-method wrapper when the method value escapes

### Milestone 5: Python class syntax

- parse and compile `class`
- create class objects
- instantiate instances by calling class objects
- support method definitions and `self`

Start with:

- no full multiple-inheritance semantics in the first cut, but class layout and
  lookup must remain extensible to MRO-based resolution
- no descriptors except plain function methods
- no metaclass features

### Milestone 6: Exception syntax

- `raise`
- `try` / `except`
- exception matching by class

### Milestone 7: Caching and JIT hooks

- add monomorphic attribute inline caches in the interpreter
- add class/member versioning needed for invalidation
- expose the same cache/guard model to the JIT

### Milestone 8: Richer Python semantics

- inheritance
- descriptors
- `super`
- `__init__`
- `__getattribute__` / `__getattr__`
- `finally`

## Open Design Questions

### 1. Where should class members live?

Best initial answer:

- in a dictionary-like class member table
- with a version tag for cache invalidation

That is simple and works well with inline caches.

### 1a. How should inheritance be represented?

Best initial answer:

- keep an inline single-base pointer for the common case
- add an optional bases array only for multiple inheritance
- keep lookup helpers abstracted over class linearization rather than baking in
  recursive single-base lookup
- allow the first implementation to populate the effective linearization from
  just:
  - the class itself
  - a single-base chain
- add explicit MRO storage once multiple inheritance is enabled

This keeps the common case fast without baking single inheritance into the
generic lookup model or cache structure.

### 2. Should instances always support arbitrary new attributes?

Probably yes for Python compatibility.

That means:

- shapes must support transition-on-store
- pathological objects can still fall back to a slower dictionary mode later if
  needed

### 3. How should slot storage be laid out?

Best initial answer:

- use trailing inline `Value` cells after the object header
- add an out-of-object overflow array once inline capacity is exhausted

This matches the existing cell-oriented allocator style and is JIT-friendly.

### 4. How should bound-method escape values be represented?

Best initial answer:

- allocate a small wrapper object only when needed
- do not allocate on direct method-call syntax

That captures most of the win without distorting semantics.

## Testing Strategy

Prefer interpreter-level semantic tests first.

Add in order:

1. instance attribute store/load
2. class attribute lookup
3. direct method calls
4. escaping method values
5. attribute miss -> `AttributeError`
6. exception propagation across nested calls
7. `raise`
8. `try` / `except`

Keep codegen/JIT tests structural and focused on:

- attribute bytecode shape
- method-call lowering
- shape-guard specialization decisions

## First Implementation Slice

This is the recommended narrow slice to build next. It is intentionally smaller
than "Python classes" and smaller than "full exceptions".

### Slice goal

Be able to create and manipulate shape-backed heap instances in the
interpreter, with attribute loads/stores and VM-native exception propagation,
without yet exposing full Python `class` syntax or `try` / `except`.

### Milestone A: Runtime layout and allocation

Deliverables:

- add `Shape` runtime object
- add `Instance` runtime object
- choose trailing `Value` slot storage for instances
- add one canonical empty shape per class-like owner
- implement shape transition lookup for `store attr`

Minimum invariants:

- every instance points at exactly one shape
- shape determines slot count and slot meaning
- adding a new attribute either reuses an existing transition or creates one
- shape identity is stable for the life of the shape

Out of scope:

- parser support for `class`
- inheritance
- descriptors

### Milestone B: Generic attribute helpers

Deliverables:

- implement runtime helpers for:
  - load instance attribute by name
  - store instance attribute by name
  - load class member by name
- add a minimal "class-like owner" object or temporary test scaffolding so
  instances have somewhere to resolve class members from

Required behavior:

- instance attribute hit reads the slot identified by the current shape
- instance attribute miss checks the owner/class member table
- missing attribute reports an internal attribute failure

Out of scope:

- inline caches
- JIT specialization

### Milestone C: Internal exception path

Deliverables:

- use `Value::exception_marker()` plus a pending-exception slot on thread state
  or equivalent VM-owned state
- add helpers to raise internal `TypeError`, `NameError`, `AttributeError`,
  and `ValueError`
- teach bytecode dispatch and function calls to propagate exception state
  without relying on C++ exceptions as the VM semantic model

Required behavior:

- opcode helpers may fail without crashing or throwing through unrelated frames
- nested function calls propagate exceptions back to the caller
- existing runtime errors can begin moving onto the new mechanism incrementally

Out of scope:

- Python `raise`
- Python `try` / `except`

### Milestone D: Attribute bytecodes and syntax surface

Deliverables:

- parser support for attribute expressions `obj.name`
- parser support for attribute assignment `obj.name = value`
- codegen support for `LoadAttr` and `StoreAttr`
- interpreter execution of those bytecodes via the generic helpers

Required tests:

1. store and reload one instance field
2. two instances share shape transitions when fields are added in the same
   order
3. loading a missing attribute produces `AttributeError`
4. exceptions raised during attribute access propagate through function calls

Out of scope:

- method calls
- bound method values
- class statements

### Milestone E: Direct method-call fast path

Deliverables:

- parser support for `obj.method(args...)`
- codegen lowering that preserves "receiver + callable" rather than forcing
  bound-method allocation on the direct-call path
- interpreter support to insert `self` for function-valued class members

Required behavior:

- direct method call syntax does not allocate a bound-method object
- method escape cases such as `f = obj.method` may remain unsupported or use a
  slower wrapper path initially

Out of scope:

- general descriptor protocol
- optimization of escaping bound methods

### Suggested stopping point for the slice

Stop after Milestone D if we want the smallest coherent first delivery.

That gives us:

- shape-backed instances
- attribute bytecodes
- VM-native exception propagation
- a clean substrate for later class syntax and method optimizations

Add Milestone E immediately afterward if method syntax is the next priority.

## Concrete Engineering Checklist

This is the recommended implementation sequence for the first slice.

### Checklist 1: Runtime data structures

- add `src/shape.h` and `src/shape.cpp`
- define `Shape` with:
  - stable shape id
  - owning `Class *`
  - total property count
  - inline capacity
  - property-name to slot metadata mapping
  - transition table for property-adds
- define slot metadata that records:
  - logical property slot
  - inline vs overflow storage
  - physical storage index
- add or extend a `Class` runtime type with:
  - class name
  - member table
  - inline single-base pointer
  - optional bases array
  - optional MRO / ancestor-walk placeholder representation
  - initial instance shape
  - version tag placeholder
- add `Instance` with:
  - `Class *`
  - `Shape *`
  - inline slot capacity
  - overflow slot array pointer

### Checklist 2: Overflow storage

- choose the overflow representation:
  - reuse `CLShortVector` after making it exception-safe, or
  - add a dedicated slot-array runtime object
- add helper to lazily allocate the first overflow array
- add helper to grow the overflow array without moving the instance object
- add helpers to read/write inline slots
- add helpers to read/write overflow slots
- ensure refcounting is correct for values stored in overflow slots

### Checklist 3: Shape transitions

- implement "lookup existing transition by property name"
- implement "create new shape for added property"
- assign new properties to inline storage while capacity remains
- assign later properties to overflow storage
- preserve shape sharing across instances that add properties in the same order
- add focused tests for shared transition reuse

### Checklist 4: Class-chain lookup

- add helper to look up one member in a class member table
- add helper to walk a class linearization until hit or miss
- keep the API MRO-shaped even if the first implementation only fills it from a
  single-base chain
- optimize the implementation so the single-base case uses the inline base
  pointer without an extra array/MRO indirection
- define the invalidation/version fields needed for later caches
- add tests for:
  - hit on the defining class
  - hit on a base class
  - miss after full ancestor walk

### Checklist 5: VM exception substrate

- add pending-exception state to `ThreadState` or equivalent VM-owned state
- add helper constructors for internal `TypeError`, `NameError`,
  `AttributeError`, and `ValueError`
- propagate `Value::exception_marker()` through call and return paths
- convert attribute lookup/store failures onto this path first
- add nested-call exception-propagation tests

### Checklist 6: Generic attribute helpers

- implement runtime `load_attr(obj, name)`
- implement runtime `store_attr(obj, name, value)`
- use shape metadata for instance-slot hits
- fall back to class/base-class lookup on instance miss
- create new shapes on new-property stores
- spill into overflow storage when inline capacity is exhausted
- raise `AttributeError` on full lookup miss

### Checklist 7: Syntax and bytecode

- extend the parser for attribute expressions `obj.name`
- extend the parser for attribute assignment `obj.name = value`
- add `LoadAttr` / `StoreAttr` bytecodes
- add codegen lowering for attribute loads and stores
- add interpreter support for the new bytecodes

### Checklist 8: Tests for the first stopping point

- storing and loading one inline property
- growing past inline capacity and using overflow storage
- reusing the same shape chain across two instances with identical add order
- resolving a class member when the instance does not have the property
- resolving a base-class member through the class chain
- raising `AttributeError` on full miss
- propagating an attribute exception through nested calls

### Checklist 9: Optional immediate follow-up for method calls

- classify plain function-valued class members as methods
- add method-call-aware lookup result carrying callable plus receiver
- add lowering for `obj.method(args...)` that avoids bound-method allocation
- allow `f = obj.method` to use a slower wrapper path initially

## Recommendation

Do not start with full Python `class` syntax.

Start with:

- shape-backed instances
- attribute load/store bytecodes
- VM-level exception propagation
- direct method-call optimization without bound-method allocation

Once those pieces are stable, `class` syntax becomes a relatively thin layer on
top of a runtime that already knows how to represent objects, call methods, and
fail correctly.
