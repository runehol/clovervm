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
- pointer to base class later
- pointer to the initial instance shape
- constructor / call metadata if needed

### `Instance`

Represents a Python object instance.

Minimum contents:

- pointer to `Class`
- pointer to current `Shape`
- inline or trailing slot storage

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

This will also give the JIT a natural representation for guards.

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
- decide whether instance slots are trailing inline cells or separately owned
- implement shape transitions for adding instance attributes
- implement a minimal class member table

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

- no inheritance
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

### 2. Should instances always support arbitrary new attributes?

Probably yes for Python compatibility.

That means:

- shapes must support transition-on-store
- pathological objects can still fall back to a slower dictionary mode later if
  needed

### 3. How should slot storage be laid out?

Best initial answer:

- use trailing `Value` cells after the object header
- size derived from shape slot count

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
