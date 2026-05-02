# Constructor Thunks

This note documents the first constructor-call implementation for `Class(...)`.
The design keeps the common case fast without turning `CallSimple` into a large
constructor protocol engine.

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

For the common case, the VM attaches an internal constructor routine to each
eligible class on demand. Conceptually:

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
EnterPreparedFunction const[Class.__init__], init_p0, argc + 1
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

The thunk injects the resolved `__init__` function as a constant. The thunk is
already specialized on that implementation's signature, so the resolved function
identity is part of the specialization rather than something the thunk should
look up again.

## Eligibility

The implemented tier-1 thunk applies only to ordinary construction:

- the class uses the standard `type` metaclass call behavior;
- the class does not define a custom `__new__`;
- `__init__` is absent or resolves to a supported `Function`;
- descriptor behavior does not require generic dispatch;
- the allocation path is the standard instance allocation for the class.

The eligibility test is intentionally conservative. If anything looks unusual,
the VM currently treats the class call as unsupported. A later generic
construction path should handle those cases correctly.

## Call-Site Cache Shape

The original call site remains a normal call:

```text
CallSimple callable, first_arg, argc, call_ic
```

When `callable` is an eligible class, the call IC specializes to the class's
attached constructor thunk:

```text
guard ClassObject / existing MRO shape+contents validity cell
callee = Class.create_instance
enter ordinary function-call path
```

The call-site IC does not cache the full constructor protocol. It only stores
enough state to prove that the class still maps to the same constructor routine.
The class owns the heavier constructor plan.

## Class-Owned Constructor State

Each eligible class owns the lazily created thunk as hidden VM metadata, not as
a Python-visible class attribute:

```cpp
ClassObject::constructor_thunk
```

This slot is addressed directly by VM code. It is not part of the class shape,
does not participate in attribute lookup, and cannot be observed or overwritten
from Python code as `Class.create_instance`.

`ClassObject::get_or_create_constructor_thunk()` owns the cache policy. Its
inline fast path checks whether `constructor_thunk` exists and whether the
class's existing `mro_shape_and_contents_validity_cell` is still valid. The slow
path creates that validity cell if needed, resolves the current `__new__` and
`__init__` assumptions, builds the thunk, and stores it back on the class.

The class-owned constructor state may eventually include:

```text
constructor_thunk function
cached __init__ function / code object for assertions or debugging
allocation assumptions
```

The implementation deliberately does not add a dedicated constructor validity
cell. Existing class lookup invalidation is broad enough for the first tier:

- class MRO or contents changes affecting `__init__` or `__new__`;
- allocation layout changes that make the thunk's allocation primitive invalid.

For this first tier, custom metaclasses are simply ineligible. When custom
metaclass construction becomes supported, this design will need a broader
revisioned guard for metaclass `__call__` behavior.

On demand, VM code asks the class for the current constructor thunk. It can
reuse the hidden thunk only while the class-owned MRO shape+contents validity
cell remains valid. If the class or one of its bases changes in a way that could
affect `__init__` or `__new__`, existing invalidation clears the hidden thunk
and invalidates the cell; the next constructor call resolves `__init__` again
and builds a new thunk.

## Implemented Scope

The current implementation is conservative:

- support standard `type` metaclass behavior only;
- reject custom `__new__`;
- support classes with no `__init__`;
- require present `__init__` to resolve to a plain `Function`;
- specialize the thunk on the resolved `__init__` function and its signature
  when one is present;
- support fixed positional parameters, defaults, and varargs through
  `EnterPreparedFunction`;
- leave keyword adaptation to the outer keyword-call opcode, so the thunk body
  can still enter a fully prepared initializer frame.

## Generic Fallback

Beyond tier 1, do not try to copy the normalized-call trick.

For custom `__new__`, custom metaclass `__call__`, descriptor-heavy paths, or
other unusual cases, add a generic construction path that may materialize
`*args` and `**kwargs` and run the full object-model protocol.

That path is allowed to be slower:

```text
generic_construct(cls, args_tuple, kwargs_dict)
```

The important split is:

- ordinary classes get a site-local, signature-aware constructor thunk;
- unusual construction remains correct and contained in a generic protocol path.

This keeps the hot path engineered and the slow path honest.
