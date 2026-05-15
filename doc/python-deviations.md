# Known Python Semantic Deviations

This document tracks intentional or temporary deviations from CPython/Python
semantics. The goal is to make each gap explicit enough that we can accept it
for a slice without forgetting to revisit it later.

This is not a list of unsupported Python features. Add entries here when
clovervm implements a construct or runtime surface, but the behavior differs
from Python.

## Context Managers

### `with` statement `__exit__` lookup timing

Python evaluates and preserves the context manager exit function before calling
`__enter__`. In specification form, it is roughly:

```python
manager = EXPR
exit = type(manager).__exit__
value = type(manager).__enter__(manager)
```

The current clovervm `with` implementation calls `__exit__` later through
`CallSpecialMethod` on the manager. That means mutations between `__enter__` and
block exit may affect which `__exit__` implementation runs.

Reason: clovervm currently does not have a separate method-load primitive that
preserves the callable plus receiver binding, and `LoadAttr` does not
materialize bound method objects.

To close: add either bound method objects, or a method-load representation that
can preserve `(callable, self)` and be called later.

### Traceback argument to `__exit__`

Python passes `(exc_type, exc_value, traceback)` to `__exit__` on exceptional
exit. clovervm currently has exception objects but not full traceback objects.

The initial implementation should pass:

```python
(exc_value.__class__, exc_value, None)
```

Reason: this preserves the most important suppression behavior while tracebacks
are not represented.

To close: add traceback objects and thread them through exception propagation.

## Class Construction

### Class calls do not dispatch custom `__new__`

Python class construction calls `__new__` first, then conditionally calls
`__init__` on the returned object. clovervm's class-call path currently builds a
constructor thunk that directly allocates an instance of the class and then
calls ordinary Python-defined `__init__`.

If a class supplies `__new__`, clovervm does not invoke it through Python's
constructor protocol.

Reason: the current constructor thunk is optimized around the common
allocate-then-init path and does not yet represent the more general
`__new__`/`__init__` interaction.

To close: teach class calls to dispatch `__new__`, handle non-instance returns,
and only call `__init__` when Python would.

## Methods And Attribute Access

### `obj.method` does not produce a bound method object

In Python, reading a function through an instance produces a bound method object:

```python
f = obj.method
f()
```

clovervm's `LoadAttr` currently loads the underlying attribute value. Binding is
handled by `CallMethodAttr` and `CallSpecialMethod`, which internally split a
method call into `callable` and optional `self`.

Reason: this is enough for direct method calls and avoids introducing bound
method objects before the object model needs them.

To close: implement bound method objects, or expose a first-class method-load
operation with equivalent semantics where needed.

### Descriptor protocol execution is partial

Python invokes descriptor methods such as `__get__`, `__set__`, `__delete__`,
and `__set_name__` at specific attribute-access and class-creation points.
clovervm already classifies descriptor-shaped values so lookup precedence and
cache invalidation can account for them, but descriptor execution is not fully
wired through the interpreter.

Observable consequences include:

- loading or calling an attribute that requires descriptor `__get__` dispatch
  raises an internal unsupported-descriptor error instead of invoking `__get__`;
- class creation rejects values that require `__set_name__` notification instead
  of calling `descriptor.__set_name__(owner, name)`.

Reason: the lookup planner needs descriptor awareness for correct precedence and
cache guards, but invoking arbitrary Python code from those descriptor paths is a
separate interpreter-dispatch slice.

To close: route descriptor get/set/delete/name notifications through explicit
fallible interpreter paths with correct binding, exception propagation, and
cache behavior.

## Truthiness

### Conditional jumps do not yet call `__bool__`

Python truthiness supports arbitrary objects through `__bool__`, and then
`__len__` if `__bool__` is absent. clovervm's conditional jumps currently handle
immediate truthy/falsy values and use the generic slow path for pointer values.

Reason: object truthiness is a separate protocol slice.

To close: extend conditional truthiness to dispatch `__bool__`, then `__len__`,
with correct exception propagation.

## Builtins And Iteration

### `range()` returns a range iterator

Python's `range()` returns an immutable reusable range object. `iter(range(...))`
then creates a separate range iterator.

clovervm's public `range()` builtin currently returns `RangeIterator` directly.
This works for the current loop and `next(range(...))` slice, but it means the
result is itself a mutable, consumable iterator rather than a reusable range
object.

Reason: the first range slice optimized direct `range(...)` iteration before
adding a distinct Python-visible range object.

To close: introduce a range object, make `range()` return it, and have
`range.__iter__` create a fresh `RangeIterator`.

## Numeric Semantics

### `bool` is not an `int` subtype for numeric protocols

In Python, `bool` is a subclass of `int`, so APIs that require an integer often
accept `True` and `False` as `1` and `0`. clovervm represents booleans with a
separate inline tag and class, and integer checks currently require tagged SMI
values.

For example, Python accepts `range(False)` as `range(0)`, while clovervm reports
`TypeError: range() arguments must be integers`.

Reason: separate bool and integer tags keep the value representation and fast
type checks simple, but do not model Python's historical bool/int inheritance.

To close: decide whether `bool` should become an `int` subclass in the class
hierarchy, and update numeric argument conversion and arithmetic protocols to
match Python.

### Integers are fixed-width tagged values

Python integers have arbitrary precision. clovervm's Python-visible integers are
currently tagged SMI values, so arithmetic that would overflow the tagged range
raises an implementation overflow error instead of widening to a larger integer.

Reason: the tagged integer representation came first, and heap-allocated big
integers have not been added yet.

To close: add a big-integer representation and make arithmetic promote out of
the SMI range instead of failing on overflow.
