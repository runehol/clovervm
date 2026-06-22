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

### Class calls only partially support custom `__new__`

Python class construction calls `__new__` first, then conditionally calls
`__init__` on the returned object. clovervm's class-call path currently builds a
constructor thunk from either the ordinary allocate-then-`__init__` path or a
`__new__`-only path.

If a class supplies `__new__` and no `__init__`, clovervm calls `__new__` with
the class argument and preserves normal argument adaptation. If a class supplies
both custom `__new__` and `__init__`, clovervm still does not implement Python's
full constructor protocol.

Reason: the current constructor thunks cover the common allocate-then-init path
and the new-only path separately. They do not yet represent the more general
`__new__`/`__init__` interaction where `__init__` is called only when Python
would call it for the object returned by `__new__`.

To close: teach class calls to handle the combined custom `__new__` plus
`__init__` protocol, including non-instance returns and conditional `__init__`
dispatch.

## Methods And Attribute Access

### `obj.method` does not produce a bound method object

In Python, reading a function through an instance produces a bound method object:

```python
f = obj.method
f()
```

clovervm's `LoadAttr` currently loads the underlying attribute value. Binding is
handled by `CallMethodAttrPositional` and `CallSpecialMethod`, which internally split a
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

The same limitation applies to the `ToBool` and `ToBoolNot` opcodes used after
membership dispatch. For example, if `__contains__` returns an object whose
truth value should be determined by `__bool__` or `__len__`, clovervm currently
raises `TypeError: unsupported truthiness for object` instead of dispatching the
truthiness protocol.

Reason: object truthiness is a separate protocol slice.

To close: extend conditional and explicit truthiness conversion opcodes to
dispatch `__bool__`, then `__len__`, with correct exception propagation.

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

### Some integer consumers still require SMI-sized tagged integers

In Python, `bool` is a subclass of `int`, and arbitrary-precision integers are
accepted by APIs that use the integer protocol as long as the value is in range
for the operation. clovervm now models `bool` as an `int` subclass and supports
heap `BigInt` values for general integer arithmetic, but some runtime consumers
still require SMI-sized tagged integer arguments.

For example, Python accepts `range(False)` as `range(0)`, while clovervm reports
`TypeError: range() arguments must be integers`. Non-SMI `BigInt` range
arguments are also outside the current range slice.

Reason: those consumers are still implemented directly on tagged SMI values.
`range()` also currently returns a `RangeIterator` whose fields are SMI-backed.

To close: route SMI-sized argument conversion through the same intlike policy
used by numeric operations where appropriate, and decide per API whether
non-SMI `BigInt` values should be accepted, rejected with Python-compatible
overflow behavior, or require a wider backing representation.

### BigInt support is not yet complete Python `int` semantics

Python integers have arbitrary precision across parsing, arithmetic, indexing,
hashing, conversions, and protocol dispatch. clovervm has heap `BigInt` values
and promotes many arithmetic overflow paths out of the tagged SMI range, but the
BigInt surface is still partial.

Known remaining gaps include APIs that still require SMI-sized values, non-SMI
BigInt indexing and range support, and integer hashing once non-string
dictionary keys or `hash()` become Python-visible.

Reason: the first BigInt slice added the representation and core arithmetic
surface without converting every integer consumer in the runtime.

To close: complete the remaining integer-protocol consumers, add compatible
hashing before exposing integer hashing broadly, and remove SMI-only argument
paths where Python accepts wider integers.
