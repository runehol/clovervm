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

Its body then allocates the instance and enters the resolved `__init__`
implementation with an already-prepared frame layout:

```text
init_p0 = inst
init_p1 = p0
init_p2 = p1
init_p3 = p2
init_p4 = p3
EnterPreparedFunction const[Class.__init__]
return inst
```

`EnterPreparedFunction` is an internal primitive: it enters a known `Function`
using an already-prepared parameter layout, skipping public call adaptation.
The name is intentional: `Call...` bytecodes perform callable protocol and
argument adaptation, while `Enter...` bytecodes trust that the callee and frame
slots are already prepared. It does not recursively enter the interpreter; it
sets up the next Python frame on the existing CloverVM stack like ordinary calls
do.

This spends one extra generated function/frame to keep constructor semantics out
of `CallSimple` and out of a shared megamorphic `type.__call__` body.

The thunk should inject the resolved `__init__` function as a constant. The
thunk is already specialized on that implementation's signature, so the resolved
function identity is part of the specialization rather than something the thunk
should look up again.

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
guard ClassObject / existing class+metaclass lookup validity cell
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
ClassObject::constructor_thunk_lookup_cell
```

These slots are addressed directly by VM code. They are not part of the class
shape, do not participate in attribute lookup, and cannot be observed or
overwritten from Python code as `Class.create_instance`.

`constructor_thunk_lookup_cell` is not a constructor-specific validity cell. It
remembers the existing combined
`mro_shape_and_metaclass_mro_shape_and_contents_validity_cell` used to build the
cached thunk, so VM code can tell whether the hidden thunk still corresponds to
the current class/metaclass lookup assumptions.

The class-owned constructor state may eventually include:

```text
constructor_thunk function
constructor_thunk_lookup_cell borrowed from existing lookup validity machinery
cached __init__ function / code object for assertions or debugging
allocation assumptions
```

The initial design deliberately does not add a dedicated constructor validity
cell. Existing class/metaclass lookup invalidation is broad enough for the first
tier:

- class MRO or contents changes affecting `__init__` or `__new__`;
- metaclass MRO or contents changes affecting `__call__`;
- allocation layout changes that make the thunk's allocation primitive invalid.

On demand, VM code gets the current combined lookup cell. It can reuse the
hidden thunk only when `constructor_thunk_lookup_cell` is that same valid cell.
If the class or metaclass changed, existing invalidation will invalidate/drop the
old combined cell; the next constructor call resolves `__init__` again and
builds a new thunk.

## Staging

The first implementation can be conservative:

- support standard `type` metaclass behavior only;
- reject custom `__new__`;
- require `__init__` to resolve to a plain `Function`;
- specialize the thunk on the resolved `__init__` function and its signature;
- support fixed positional parameters and defaults first;
- add varargs/keyword support after `EnterPreparedFunction` can enter a fully
  normalized callee frame.

Before `EnterPreparedFunction` exists, an intermediate thunk may use the normal
function call machinery for the internal `__init__` call, but the target shape is
the prepared enter primitive above.

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
