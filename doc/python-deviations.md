# Known Python Semantic Deviations

This document tracks intentional or temporary deviations from CPython/Python
semantics. The goal is to make each gap explicit enough that we can accept it
for a slice without forgetting to revisit it later.

## Context Managers

### `with` statement `__exit__` lookup timing

Python evaluates and preserves the context manager exit function before calling
`__enter__`. In specification form, it is roughly:

```python
manager = EXPR
exit = type(manager).__exit__
value = type(manager).__enter__(manager)
```

The initial clovervm `with` implementation is expected to call `__exit__` later
through `CallSpecialMethod` on the manager. That means mutations between
`__enter__` and block exit may affect which `__exit__` implementation runs.

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

## Truthiness

### Conditional jumps do not yet call `__bool__`

Python truthiness supports arbitrary objects through `__bool__`, and then
`__len__` if `__bool__` is absent. clovervm's conditional jumps currently handle
immediate truthy/falsy values and use the generic slow path for pointer values.

Reason: object truthiness is a separate protocol slice.

To close: extend conditional truthiness to dispatch `__bool__`, then `__len__`,
with correct exception propagation.

## Assignment Targets

### Tuple/list unpacking assignment is not implemented

Python supports unpacking targets in assignments and `with ... as target`.
clovervm currently accepts simple variable, attribute, and subscript assignment
targets.

Reason: destructuring assignment should be implemented generically and reused by
plain assignment, `for` targets, and `with` targets.

To close: implement generic assignment-target lowering for tuple/list
unpacking, including starred targets.
