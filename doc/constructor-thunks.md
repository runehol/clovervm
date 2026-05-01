# Constructor Thunks

This note sketches a constructor-call design for `Class(...)` that keeps the
common case fast without turning `CallSimple` into a large constructor protocol
engine.

## Problem

At a call site such as:

```python
Class(a, b)
```

code generation does not know whether the callee is a function, class, callable
instance, or something else. Explicit constructor bytecode is therefore not a
good primary semantic representation.

The naive object-model implementation is to route construction through
`type.__call__`, which roughly does:

```python
inst = cls.__new__(cls, *args, **kwargs)
if isinstance(inst, cls):
    inst.__init__(*args, **kwargs)
return inst
```

That is correct, but it is a poor hot path for ordinary constructors:

- every constructor goes through shared `type.__call__` call sites, making the
  internal `__init__` call megamorphic;
- `*args` and `**kwargs` materialization becomes necessary immediately;
- the original call site loses the locality that would make inline caches useful.

Putting the full construction protocol directly in `CallSimple` also gets bulky:
the opcode would need constructor dispatch, `__init__` method-load caching,
initializer call caching, argument preservation, and special return behavior.

## Tier 1: Standard Constructor Thunk

For the common case, attach an internal constructor routine to each eligible
class. Conceptually:

```python
def Class_create_instance(...):
    inst = <allocate instance of Class>
    Class.__init__(inst, ...)
    return inst
```

The key trick is that the thunk's public call convention mirrors
`Class.__init__` with `self` removed. Normal call machinery performs all
argument normalization before the thunk body runs.

For example:

```python
class Class:
    def __init__(self, a, b=1, *args, **kwargs):
        ...
```

The constructor thunk receives normalized parameter slots equivalent to:

```text
p0 = a
p1 = b
p2 = args_tuple
p3 = kwargs_dict
```

Its body then allocates the instance and performs an internal straight call to
`__init__` with the already-normalized frame layout:

```text
init_p0 = inst
init_p1 = p0
init_p2 = p1
init_p3 = p2
init_p4 = p3
StraightCallNormalized Class.__init__
return inst
```

`StraightCallNormalized` is an internal primitive: it enters a known function
using an already-normalized parameter layout, skipping public call adaptation.
It does not recursively enter the interpreter; it sets up the next Python frame
on the existing CloverVM stack like ordinary calls do.

This spends one extra generated function/frame to keep constructor semantics out
of `CallSimple` and out of a shared megamorphic `type.__call__` body.

## Eligibility

The tier-1 thunk applies only to ordinary construction:

- the class uses the standard `type` metaclass call behavior;
- the class does not define a custom `__new__`;
- `__init__` resolves to a supported `Function`;
- descriptor behavior does not require generic dispatch;
- the allocation path is the standard instance allocation for the class.

The exact eligibility test can be conservative. If anything looks unusual, use
the generic path instead.

## Call-Site Cache Shape

The original call site remains a normal call:

```text
CallSimple callable, first_arg, argc, call_ic
```

When `callable` is an eligible class, the call IC can specialize to the class's
attached constructor thunk:

```text
guard ClassObject / constructor validity
callee = Class.create_instance
enter ordinary function-call path
```

The call-site IC does not need to cache the full constructor protocol. It only
needs enough state to prove that the class still maps to the same constructor
routine. The class owns the heavier constructor plan.

## Class-Owned Constructor State

Each eligible class can own or lazily create the thunk as hidden VM metadata,
not as a Python-visible class attribute:

```cpp
ClassObject::constructor_thunk
```

This slot is addressed directly by VM code. It is not part of the class shape,
does not participate in attribute lookup, and cannot be observed or overwritten
from Python code as `Class.create_instance`.

The class-owned constructor state may eventually include:

```text
constructor_thunk function
constructor validity cell, if a dedicated cell proves useful
cached __init__ function / code object
allocation assumptions
```

Invalidating the constructor validity should happen when any assumption that can
change construction semantics changes, including:

- class MRO or contents changes affecting `__init__` or `__new__`;
- metaclass MRO or contents changes affecting `__call__`;
- allocation layout changes that make the thunk's allocation primitive invalid.

Initially this can over-invalidate using broad class/metaclass lookup validity
cells or simply discard/rebuild the hidden thunk when relevant class state
changes. A dedicated constructor validity cell is optional and can come later if
it proves useful.

## Generic Fallback

Beyond tier 1, do not try to copy the normalized-call trick.

For custom `__new__`, custom metaclass `__call__`, descriptor-heavy paths, or
other unusual cases, use a generic construction path that may materialize
`*args` and `**kwargs` and run the full object-model protocol.

That path is allowed to be slower:

```text
generic_construct(cls, args_tuple, kwargs_dict)
```

The important split is:

- ordinary classes get a site-local, signature-aware constructor thunk;
- unusual construction remains correct and contained in a generic protocol path.

This keeps the hot path engineered and the slow path honest.
