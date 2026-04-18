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

- stable shape identity by pointer
- pointer to owning class
- monotonic next-slot counter for successor-shape allocation
- ordered property descriptors
- transition table for "add attribute X" -> next shape
- optional flags for future optimizations

Properties:

- immutable after publication
- shared by many instances
- cheap to compare by pointer or id

Current refinement:

- shape identity is `Shape *`
- the immutable descriptor payload is stored inline in the `Shape` allocation
- transition caches may still be populated after publication
- the back-pointer to the previous shape is non-owning

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

Current refinement:

- instances store both `cls` and `shape`
- `cls` exists so `obj.__class__` is not treated as an own-property
- overflow storage is currently a dedicated `Instance::OverflowSlots` runtime
  object

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

Current storage policy:

- shapes assign new properties to inline storage while inline capacity remains
- later properties spill into overflow storage
- descriptor order is logical property order
- descriptor storage location is separate from logical order
- delete does not roll back slot allocation
- delete + re-add appends at the end and gets a fresh storage location

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

Prerequisite now completed:

- instance own-property storage and shape transitions exist independently of
  parser/codegen support

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

### Shape-vs-class responsibility split

Shapes should capture:

- class identity
- instance-owned field layout
- whether an instance currently has an own property that shadows a class member

Shapes should not capture:

- the contents of the class member table
- the contents of ancestor member tables
- the effective result of method lookup through the MRO

That means:

- shape identity implies class identity for optimized instance operations
- class mutation does not rewrite existing instance shapes
- optimized lookup depends on both:
  - instance shape, for own-field layout and shadowing
  - class-side lookup validity, for MRO-resolved members

This keeps per-instance layout and per-class method resolution cleanly
separated.

### Call-site guards and compression

For an optimized method call like `c.f()`, the runtime wants to prove:

- the receiver still has the expected shape
- lookup of `f` starting from the receiver's class still resolves to the same
  callable

Because shapes are class-owned, the shape guard already implies class identity.
So the hot-path guard set should not be:

- shape check
- separate class pointer check
- separate shadowing check

Instead, the intended compressed form is:

- shape check
- one class-side lookup-validity check, or eager invalidation instead of that
  check

In the long-term optimized path, method calls should ideally use:

- shape guard
- direct jump or inlined body

with class-side changes handled by eager invalidation rather than by an always-on
version load in the hot path.

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

## Method Lookup Dependencies And Invalidation

Optimized method calls depend on lookup facts, not merely on the class that
currently owns the method.

Example:

```python
class A:
    def f(self):
        ...

class B(A):
    pass

b = B()
```

An optimized `b.f()` depends on the fact that:

- lookup of `"f"` starting from `B`
- currently resolves to `A.f`

That optimization must be invalidated if:

- `A.f` changes
- `A.f` is deleted
- `B.f` is assigned, causing a nearer shadowing hit
- any earlier class in the relevant MRO begins defining `f`

So the dependency is not just "the current defining class". It is the
first-hit resolution of a name along the receiver class's MRO.

### Required safety rule

For an optimized lookup of `(start_class, name)`, any mutation to `name` on any
class from `start_class` through the class that currently defines the name must
invalidate the optimization.

In single inheritance, if `B.f()` currently resolves to `A.f`, then the
dependency set includes at least:

- `B`
- `A`

because:

- mutating `B.f` can introduce a closer hit
- mutating `A.f` can change the current hit

For deeper hierarchies, the same rule extends through the whole MRO prefix up
to the defining class.

### Practical invalidation strategy

The likely staged design is:

1. bootstrap correctness
   - shape guard
   - class-side version or lookup token check

2. optimized steady state
   - shape guard only on the hot path
   - eager invalidation/deoptimization registry for class-side lookup facts

This matches the expected workload well: classes and methods are usually stable,
so it is better to pay invalidation cost on rare mutation than to pay a class
version load on every hot method call.

### Invalidation granularity

Do not invalidate method-call optimizations for every class variable write.

Instead, separate:

- callable / lookup-relevant member mutations
- ordinary class-data mutations

The intended policy is:

- method-like member writes participate in invalidation
- plain class-variable writes do not invalidate method-call optimizations unless
  optimized code explicitly depends on those variables

This means:

- `C.f = other_function` invalidates optimized `obj.f()`
- `del C.f` invalidates optimized `obj.f()`
- `Base.f = other_function` invalidates optimized inherited `obj.f()`
- `C.x = 3` does not invalidate optimized `obj.f()` unless some optimization
  explicitly depended on `x`

That is a good first tradeoff: coarse enough to implement, but not so coarse
that harmless class-data writes destroy unrelated call-site optimizations.

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

## Implementation Roadmap

The design should be implemented runtime-first. Each phase should leave the VM
in a coherent, testable state.

### Phase 0: Lock down invariants

These invariants should be treated as architecture decisions:

- instance pointers are stable for the object lifetime
- shapes are immutable after publication
- every shape belongs to exactly one class
- shape identity comparison is by pointer or stable id
- class member tables carry invalidation metadata for optimized lookups
- instance member storage is split into inline slots and optional overflow
  storage
- generic lookup APIs are phrased in terms of MRO traversal even if the first
  implementation only uses a single-base chain

### Phase 1: Add runtime representations

Add the core heap object types needed for classes and instances:

- `Shape`
- `Class`
- `Instance`
- overflow slot array representation
- minimal exception object representation

Recommended choices:

- keep `Klass` as the VM meta-type description and add a separate Python-level
  `Class` runtime object
- use a dictionary-like class member table with invalidation metadata
- prefer a dedicated slot-array object over `CLShortVector` if resizing and
  refcount behavior would otherwise become awkward

Exit criteria:

- tests can allocate classes and instances manually from C++ helpers
- tests can inspect shapes, inline slots, and overflow slots directly

Status:

- reached

### Phase 2: Implement shape growth and storage semantics

Before any syntax work, make attribute storage real:

- implement shape transition tables
- implement slot assignment policy
- implement overflow growth policy
- add manual runtime helpers for reading and writing properties by name

Recommended choices:

- fixed inline capacity per class for the first cut
- simple geometric growth for overflow arrays
- assign inline slots first, then overflow slots, never re-pack existing shapes

Exit criteria:

- two instances with the same property-add order share shape transitions
- instances continue to work after spilling into overflow storage

Status:

- reached

### Phase 3: Implement generic lookup semantics

Implement Python-like member resolution independently of parser/codegen work:

- instance property lookup
- class member lookup
- ancestor walk
- internal attribute-miss reporting

Generic lookup should return a richer resolution record, not just a `Value`.
That record should include:

- resolved value
- where it was found: instance slot, defining class, ancestor class
- slot metadata when applicable
- enough dependency information to invalidate optimized inherited lookups when a
  nearer class begins shadowing a name

Exit criteria:

- generic lookup works correctly for instance hits, defining-class hits,
  inherited hits, and misses

Status:

- not started

### Phase 4: Add VM-native exception propagation

Stop using C++ exceptions as the execution model:

- add pending-exception state
- add helper constructors for built-in exception classes
- teach calls and returns to propagate exception state
- move attribute failures onto the new path first

Recommended choice:

- store a raw exception object `Value` first
- add source-location attachment later

Exit criteria:

- nested interpreter calls propagate exceptions without `throw`
- attribute misses produce VM-managed exception objects

Status:

- not started

### Phase 5: Add attribute syntax and bytecode

Connect the runtime machinery to the language pipeline:

- parser support for attribute expressions
- parser support for attribute assignment
- `LoadAttr` / `StoreAttr` bytecodes
- codegen lowering
- interpreter execution

Recommended choice:

- use constant-table interned strings for attribute names first
- add cache metadata later rather than complicating the first opcode shape

Exit criteria:

- attribute access works end to end from source text

Status:

- not started

### Phase 6: Add method-call fast path

Make direct method calls cheap before full class syntax arrives:

- add method-aware lookup results
- add `obj.method(args...)` lowering
- insert `self` on direct class-function calls
- allocate a wrapper only when method values escape

Recommended choice:

- prefer `LoadMethod` / `CallMethod` if that fits later inline caching better
- allow escaping bound methods to stay slow or unsupported initially

Exit criteria:

- direct method calls work without bound-method allocation on the hot path
- invalidation correctly handles inherited-method shadowing such as `B` gaining
  `f` after optimized calls previously resolved to `A.f`

### Phase 7: Add Python class syntax

Expose the runtime model through Python `class` definitions:

- `class` parser support
- class body execution model
- class object creation
- method installation onto class member tables

Recommended choice:

- execute class bodies in a temporary scope or dictionary-like environment
- construct the class object from that environment afterward
- start with no inheritance syntax or single-base syntax only, while keeping
  the representation ready for multiple inheritance

Exit criteria:

- user code can define a basic class with methods and instantiate it

### Phase 8: Add full inheritance and MRO

Enable Python’s class-linearization semantics beyond the single-base subset:

- multiple-base parsing
- C3 linearization
- explicit MRO storage
- cache invalidation across the effective MRO
- `super()` support later

Exit criteria:

- diamond inheritance resolves according to Python’s MRO rules

### Phase 9: Add inline caches and JIT hooks

Turn the correct object model into a fast one:

- monomorphic attribute caches in the interpreter
- eager invalidation or lookup-version dependencies for method resolution
- method-call caches
- JIT guard representation for shapes and class-member lookup assumptions

Exit criteria:

- the interpreter and later JIT can specialize instance/member access without
  dictionary lookup on the hot path

## First Slice

The recommended first implementation slice is intentionally smaller than full
Python classes and smaller than full Python exception syntax.

### Goal

Be able to create and manipulate shape-backed heap instances in the
interpreter, with attribute loads/stores and VM-native exception propagation,
without yet exposing full Python `class` syntax or `try` / `except`.

### Scope

Build the system up through Phase 5:

- runtime representations
- shape growth and overflow storage
- generic lookup
- VM-native exception propagation
- attribute syntax and bytecode

Method-call optimization is the next step after that slice is stable.

### Concrete checklist

1. Runtime data structures
   Add `src/shape.h` / `src/shape.cpp`.
   Define `Shape` with pointer identity, owning `ClassObject *`, property
   count, monotonic next-slot counter, inline descriptor payload, and a
   transition table.
   Add descriptor metadata recording property name plus resolved storage
   location.
   Add a `ClassObject` runtime type with class name, inline slot capacity, and
   initial instance shape.
   Add `Instance` with `cls`, `shape`, inline slots, and overflow slot array
   pointer.

   Status:
   This is now implemented, with the runtime types split across
   `src/shape.*`, `src/class_object.*`, and `src/instance.*`.

2. Overflow storage
   Choose the overflow representation.
   Add helpers to allocate the first overflow array lazily, grow it without
   moving the instance object, and read/write both inline and overflow slots.
   Ensure refcounting is correct for overflow-stored values.

   Status:
   This is now implemented with `Instance::OverflowSlots`, geometric growth,
   and `not_present` initialization for every capacity slot.

3. Shape transitions
   Implement "lookup existing transition by property name".
   Implement "create new shape for added property".
   Assign new properties to inline storage while capacity remains, and later
   properties to overflow storage.
   Preserve shape sharing across instances that add properties in the same
   order.

   Status:
   This is now implemented for add/delete transitions on own properties.

4. Class and ancestor lookup
   Add helper to look up one member in a class member table.
   Add helper to walk a class linearization until hit or miss.
   Keep the API MRO-shaped even if the first implementation only fills it from
   a single-base chain.
   Optimize the implementation so the single-base case uses the inline base
   pointer without an extra array or MRO indirection.

   Status:
   This is now the next natural runtime step.

5. VM exception substrate
   Add pending-exception state to `ThreadState` or equivalent VM-owned state.
   Add helper constructors for internal `TypeError`, `NameError`,
   `AttributeError`, and `ValueError`.
   Propagate `Value::exception_marker()` through call and return paths.
   Convert attribute lookup/store failures onto this path first.

6. Generic attribute helpers
   Implement runtime `load_attr(obj, name)` and `store_attr(obj, name, value)`.
   Use shape metadata for instance-slot hits.
   Fall back to class and ancestor lookup on instance miss.
   Create new shapes on new-property stores.
   Spill into overflow storage when inline capacity is exhausted.
   Raise `AttributeError` on full lookup miss.

   Status:
   Partially started only in the narrow sense that own-property helpers exist.
   Full generic attribute lookup is still pending on class/ancestor lookup and
   exception propagation.

7. Syntax and bytecode
   Extend the parser for attribute expressions `obj.name`.
   Extend the parser for attribute assignment `obj.name = value`.
   Add `LoadAttr` / `StoreAttr` bytecodes.
   Add codegen lowering and interpreter support for the new bytecodes.

8. Tests for the stopping point
   Cover storing and loading one inline property.
   Cover growing past inline capacity and using overflow storage.
   Cover reusing the same shape chain across two instances with identical add
   order.
   Cover resolving a class member when the instance does not have the property.
   Cover resolving an inherited member through the class chain.
   Cover raising `AttributeError` on full miss.
   Cover propagating an attribute exception through nested calls.

### Expected stopping point

After this slice, the VM should have:

- shape-backed instances
- class and ancestor lookup
- attribute bytecodes
- VM-native exception propagation
- a clean substrate for later class syntax and method optimizations

Current reached stopping point:

- shape-backed instances
- class-owned root shapes
- add/delete shape transitions
- inline and overflow own-property storage
- string-keyed own-property get/set/delete helpers

## What Still Needs To Be Pinned Down

The plan now hangs together directionally, but a few details still need one
more short appendix before coding starts in earnest:

- the exact split between `Klass` and Python-level `Class`
- the concrete representation of shape transition tables
- the concrete representation of class member tables and invalidation metadata
- the exact overflow slot array type
- the exact API returned by generic member lookup
- the exact dependency-registration data structure for invalidating optimized
  inherited method lookups when nearer classes begin shadowing a name
- the exact bytecode operand shape for `LoadAttr`, `StoreAttr`, and likely
  `LoadMethod` / `CallMethod`

Several of these are now pinned down and implemented:

- `Klass` remains the VM meta-type description
- `ClassObject` is the current Python-level class runtime scaffold
- shape transition tables are cached per-shape and keyed by `(name, verb)`
- overflow storage is `Instance::OverflowSlots`

The main remaining design questions are now above raw storage:

- class member table representation
- ancestor/MRO lookup representation
- generic lookup result shape
- exception propagation substrate
- attribute bytecode operand shape

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

## Open Questions

### Where should class members live?

Best initial answer:

- in a dictionary-like class member table
- with invalidation metadata for optimized lookups

### How should inheritance be represented?

Best initial answer:

- keep an inline single-base pointer for the common case
- add an optional bases array only for multiple inheritance
- keep lookup helpers abstracted over class linearization rather than baking in
  recursive single-base lookup
- add explicit MRO storage once multiple inheritance is enabled

### Should instances always support arbitrary new attributes?

Probably yes for Python compatibility.

That means:

- shapes must support transition-on-store
- pathological objects can still fall back to a slower dictionary mode later if
  needed

### How should slot storage be laid out?

Best initial answer:

- use trailing inline `Value` cells after the object header
- add an out-of-object overflow array once inline capacity is exhausted

### How should bound-method escape values be represented?

Best initial answer:

- allocate a small wrapper object only when needed
- do not allocate on direct method-call syntax

## Recommendation

Do not start with full Python `class` syntax.

Start with:

- shape-backed instances
- class and ancestor lookup
- attribute load/store bytecodes
- VM-level exception propagation

Once those pieces are stable, `class` syntax becomes a relatively thin layer on
top of a runtime that already knows how to represent objects, look up members,
and fail correctly.
