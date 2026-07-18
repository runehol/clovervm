# Descriptor Execution

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | Partial |
| Scope | Executing descriptor `__get__` during attribute reads |
| Owning layers | Runtime attribute semantics, inline caches, interpreter, and call adaptation |
| Validated against | `d25fe55` (2026-06-28) |
| Supersedes | N/A |

This document covers descriptor `__get__` execution for attribute reads.
`obj.method(...)` keyword handling, descriptor writes, descriptor deletes, and
`__set_name__` are separate design slices.

The design below applies to default attribute lookup. Custom
`__getattribute__` and `__getattr__` support is a separate slice; when those
hooks are present, the default descriptor plan must not pretend it describes the
whole attribute read.

The design goal is to let attribute-read inline caches describe an optional
descriptor call plan without turning ordinary attribute reads into generic call
dispatch. The descriptor result is the result of the attribute read. If
`__get__` returns another descriptor or another callable, that value is returned
as-is. Any later call belongs to a later bytecode operation.

## Python Semantics Boundary

Descriptor protocol lookup is a special method lookup on the descriptor's type:

```text
type(descriptor).__get__
type(descriptor).__set__
type(descriptor).__delete__
```

It is not an ordinary `descriptor.__get__` instance-attribute read. Installing a
`__get__` attribute on one descriptor instance does not make that object a
descriptor. Changing the descriptor class can make existing instances become
descriptors, which is why cacheable descriptor plans depend on immutable
descriptor classes or a class-mutation guard.

The `__get__` call always uses three positional arguments at the VM call-entry
level:

```text
type(descriptor).__get__(descriptor, obj_or_none, owner)
```

The user-visible bound method form is:

```python
descriptor.__get__(obj_or_none, owner)
```

For ordinary instance access, `obj_or_none` is the receiver and `owner` is
`type(receiver)`.

For class access to a descriptor found in the class namespace, `obj_or_none` is
`None` and `owner` is the class being read. Descriptors found on the metaclass
follow the same rule as ordinary instance access, with the class object as the
receiver and the metaclass as the owner.

Class-object lookup must still preserve Python's metaclass precedence rule. For
`C.x`, a data descriptor found on the metaclass wins before `C`'s own namespace
is considered. Only after metaclass data descriptors have been ruled out should
the lookup consider the class namespace, then metaclass non-data descriptors and
plain metaclass attributes.

For `super(B, obj).x`, when `obj` is an instance, `obj_or_none` is the original
receiver and `owner` is `type(obj)`. For `super(B, C).x`, when the second
argument is a class, `obj_or_none` is `None` and `owner` is that class. The
`None` binding mode must therefore be cacheable; it is not limited to direct
class-namespace lookup.

Data and non-data descriptor precedence stays in the object-model lookup layer:

- data descriptors beat receiver-owned attributes;
- receiver-owned attributes beat non-data descriptors;
- non-data descriptors still execute `__get__` when no receiver-owned attribute
  shadows them.

The receiver shape remains part of the outer attribute-read guard, so a cached
non-data descriptor read is invalid for receivers whose own shape contains a
shadowing attribute.

Function objects are non-data descriptors, but clovervm keeps a function-specific
fast path for them. `BindFunctionReceiver` remains the ordinary method-binding
plan for functions found on an instance class chain, because that path is much
hotter than general descriptor dispatch. The general descriptor plan covers
non-function descriptors and other function access modes; it must not regress
method-value lookup such as `obj.f`.

## Layer Ownership

`src/object_model/attr.cpp` owns lookup, descriptor classification, and the
question "does this attribute read need descriptor execution?" It should produce
an attribute-read plan that says whether the found value is a data descriptor
get, non-data descriptor get, or direct value read.

`src/runtime/interpreter.cpp` owns executing descriptor get plans from bytecode.
Descriptor execution may call Python code, allocate, raise, or enter a new
frame, so it should stay on an explicit opcode slow path rather than inside a
small inline attribute helper.

Builtin type files own trusted descriptor implementations and their trusted
handler resolvers. The attribute system should ask a `CodeObject` resolver for a
trusted ternary handler; it should not duplicate builtin descriptor semantics.

## Cache Policy

The outer attribute-read inline cache is still guided by the receiver shape and
lookup validity cell. For descriptor reads, that outer cache proves that the same
descriptor object is the attribute result candidate and that the same lookup
precedence decision still holds.

Descriptor execution is installable in `AttributeReadInlineCache` only when all
of the following hold:

- the descriptor type is immutable;
- the descriptor type has stable descriptor-protocol classification:
  `__get__`, `__set__`, and `__delete__` cannot appear, disappear, or change;
- special lookup of `__get__` on the descriptor type resolves to a callable
  value;
- that callable value is a `Function`;
- the function's `CodeObject` trusted handler resolver accepts
  `TrustedHandlerArity::Ternary` for normal operand order;
- the attribute-read plan itself has no existing cache blockers.

Mutable descriptor classes use the uncached slow path. This is intentional. The
descriptor protocol depends on the descriptor type's dunder methods, and the
runtime should not spend validity-cell budget trying to make uncommon mutable
descriptor classes fast.

No trusted handler means no trusted fast replay. It does not mean the descriptor
is semantically unsupported. A user-defined descriptor whose `__get__` has
defaults, varargs, extra optional parameters, or is implemented by a callable
object is still valid Python. Those cases use the uncached descriptor execution
path and ordinary call machinery. They are not installed as trusted descriptor
plans.

Generic callable-object optimization belongs to ordinary call sites such as
`obj()`, not to the descriptor inline cache. Callable-object `__get__` is a
semantic edge the descriptor slow path must support, but the cross-product of
descriptor lookup and arbitrary `__call__` should not shape the cached
descriptor plan.

The first implementation should therefore install descriptor plans in
`AttributeReadInlineCache` only when `descriptor_get.get_handler` is non-null.
Immutable descriptor classes without a trusted ternary handler still run with
correct descriptor semantics, but they stay on the uncached descriptor path.

## Attribute Read Plan Payload

Descriptor execution should be integrated into `AttributeReadPlan` and
`AttributeReadInlineCache`, not represented by a standalone cached descriptor
structure.

`AttributeReadPlanKind` already has the durable discriminator:

```cpp
enum class AttributeReadPlanKind : uint8_t
{
    ReceiverSlot,
    ConstantValue,
    BindFunctionReceiver,
    DataDescriptorGet,
    NonDataDescriptorGet,
};
```

`DataDescriptorGet` and `NonDataDescriptorGet` should remain the plan kinds that
mean "this attribute read must execute descriptor `__get__`." The additional
state needed to replay a cacheable descriptor get belongs on `AttributeReadPlan`
next to the existing storage and constant-value fields.

Conceptually, a descriptor-capable `AttributeReadPlan` needs a compact tagged
payload:

```cpp
enum class DescriptorSelfKind : uint8_t
{
    Receiver,
    None,
};

class AttributeReadPlan
{
public:
    AttributeReadPlanPath path;
    AttributeReadPlanKind kind;

    union
    {
        struct
        {
            const Object *storage_owner;
            StorageLocation storage_location;
        } storage;

        Value constant_value;

        struct
        {
            Member<Value> descriptor;
            const ClassObject *owner;
            DescriptorSelfKind self_kind;
            TrustedHandler get_handler; // ternary member only
        } descriptor_get;
    };
};
```

The important point is that `kind` remains the discriminator and the descriptor
fields do not widen every direct-read plan. Add `static_assert`s for
`AttributeReadPlan` and `AttributeReadInlineCache` when implementing this, so
descriptor support does not silently bloat every attribute read cache.

`descriptor_get.descriptor` is the object whose type supplied `__get__`. It must
be stored with retained/member-style ownership, not as an unowned raw `Value`.

`descriptor_get.owner` is the third argument to `__get__`. It is cached because
class access and `super` access need the owner selected by lookup, not a value
re-derived from the descriptor object.

`descriptor_get.self_kind` determines the second argument to `__get__`:

```cpp
Value obj_or_none =
    plan.descriptor_get.self_kind == DescriptorSelfKind::None
        ? Value::None()
        : receiver;
```

It is a semantic binding fact, not an execution-cache detail. Instance reads,
metaclass reads, and `super(..., obj)` use `Receiver`. Class-namespace
descriptor reads and `super(..., cls)` use `None`.

`descriptor_get.get_handler` is present only for cacheable fast descriptor gets.
It is the trusted ternary handler resolved from the `CodeObject` for
`type(descriptor).__get__`. A null handler means the plan cannot be replayed as
a cached fast descriptor get.

`obj_or_none` itself should not be stored in the plan for normal replay. The
plan stores how to compute it for the current receiver.

Storing `descriptor_get.descriptor` in an inline cache changes cache ownership.
`AttributeReadInlineCache::clear()`, overwrite/populate, and `CodeObject`
teardown must release any cached descriptor value they own, and the cache must
be visible to tracing/reclamation if required by the current GC/refcount model.
This is not optional; a descriptor stored in a class slot may be overwritten
after the cache is installed. `AttributeReadPlan` copies and assignments cannot
remain blind bitwise copies once a descriptor payload is active; they need a
retained-value copy/replace contract, or descriptor ownership must live in an
equivalent retained cache cell that follows the same rule.

`AttributeReadInlineCache` should continue to cache the receiver shape, lookup
validity cell, and `AttributeReadPlan`:

```cpp
class AttributeReadInlineCache
{
public:
    Shape *receiver_shape;
    ValidityCell *lookup_validity_cell;
    AttributeReadPlan plan;
};
```

The cache already has a kind-bearing plan. Cache replay should switch on
`plan.kind`: direct read kinds keep the existing slot/constant behavior, while
descriptor get kinds require `plan.descriptor_get.get_handler` to be non-null
and call it with
`plan.descriptor_get.descriptor`, the `obj_or_none` value computed from
`plan.descriptor_get.self_kind`, and `plan.descriptor_get.owner`.

If a trusted handler returns `Value::exception_marker()`, the opcode must route
through the same pending-exception resolution shape used by trusted operator
handlers. The exception marker is never a normal attribute value.

The uncached slow path may still build a temporary local object that says "call
this already-resolved callable with these three positional arguments." That is
not IC state. It is only a convenient way for the miss handler to bridge from
special `__get__` lookup to the one-shot resolved positional call helper.

## Descriptor Call Argument Staging

`LoadAttr` does not reserve bytecode argument registers for descriptor calls. The
slow path must borrow the dunder/operator call staging convention rather than
write descriptor arguments into arbitrary locals.

For an uncached descriptor `__get__` call, use the scratch area rooted at:

```cpp
int32_t prefix_reg = code_object->get_first_free_arg_encoded_reg();
int32_t first_arg_reg = prefix_reg - 6;
```

This matches the ternary dunder-method call layout. The descriptor call stages:

```cpp
fp[first_arg_reg] = descriptor;
fp[first_arg_reg - 1] = obj_or_none;
fp[first_arg_reg - 2] = Value::from_oop(owner);
```

Descriptor protocol lookup does not bind `__get__` as a method. The descriptor
object is always the explicit first positional argument. If the resolved
`__get__` value is a plain `Function`, the staged call window above is already
the final positional call window with `n_args = 3`.

Generic callable `__get__` uses the same staged positional window. The generic
callable entry point should receive `first_arg_reg`, `n_args = 3`, and
`instr_len = 4` just like the function entry path; it may then perform whatever
additional callable-object protocol is required, such as resolving `__call__`.
The descriptor path must not prepare a second argument layout for callable
objects.

Any future inline cache for callable objects should be designed for direct call
opcodes first. Descriptor slow paths can reuse that generic call machinery after
they have resolved `type(descriptor).__get__`, but descriptor IC entries should
continue to require the trusted `Function` handler shape above.

The call continuation is the next bytecode after `LoadAttr`: `pc + 4`. Entering
the frame should pass `instr_len = 4`, so normal return resumes after the
attribute read. This is a `LoadAttr`-specific single-call continuation; unlike
operator continuations, it does not need to resume a multi-row protocol.

## Slow Path

A descriptor-read miss should:

1. Resolve the normal attribute read and classify it as direct, data descriptor,
   or non-data descriptor.
2. For direct reads, return or install the existing direct read plan.
3. For descriptor reads, perform special lookup of `__get__` on
   `type(descriptor)`.
4. Prepare three positional arguments:
   `descriptor`, `obj_or_none`, and `owner`.
5. If the descriptor type is immutable and the resolved function has a trusted
   ternary handler, populate the descriptor fields on the cached
   `AttributeReadPlan` and call the handler. If the handler returns an
   exception marker, resolve the pending exception through the interpreter
   exceptional path.
6. Otherwise enter a one-shot resolved positional call using a local
   call plan, then discard that call plan.

The one-shot helper must not redo descriptor lookup or method binding. It is for
an already-resolved callable and the three prepared descriptor arguments. If the
callable is a plain `Function` or eligible constructor thunk, it can use the
existing resolved positional call helper. If it is another callable object, the
uncached path must use the generic callable protocol. If that protocol is not
available yet in the runtime, full descriptor semantics depend on implementing
it; the descriptor path must not silently treat the descriptor as absent or
unsupported.

Defaults are handled by the ordinary function call adaptation path. For example:

```python
class D:
    def __get__(self, obj, owner, extra=None):
        return 1
```

The descriptor fast path will not use a trusted ternary handler for this shape
unless one is explicitly provided. The slow path still calls it with the three
required descriptor arguments and lets the normal binder fill `extra`.

## Invalidation

Receiver shape and lookup validity already protect the outer attribute lookup.
They must continue to prove:

- which class-chain entry won;
- whether a receiver-owned attribute shadows a non-data descriptor;
- whether class or metaclass mutation could change the lookup result.

The descriptor fields on `AttributeReadPlan` add one more assumption: the
descriptor type's `__get__` lookup and trusted handler are stable. The first
implementation should only cache this when the descriptor type is immutable.
Mutable descriptor types fall back to the slow path every time.

A cacheable trusted descriptor read needs these validity ingredients:

- receiver shape guard, as today;
- a lookup validity cell for the winning owner namespace and any precedence
  dependencies, including metaclass data-descriptor precedence for class-object
  reads;
- the descriptor value retained in the plan, so replacing the class slot must
  invalidate the lookup cell before replay observes stale state;
- a stable immutable descriptor type shape, specifically one that proves the
  whole descriptor protocol classification cannot change: `__get__`, `__set__`,
  and `__delete__` cannot appear, disappear, or be replaced;
- a stable resolved `Function` and trusted ternary handler for that `__get__`.

The descriptor-type check should be centralized behind a helper such as
`descriptor_type_has_stable_protocol(cls)`. Do not scatter ad-hoc
`IsImmutableType` flag checks at call sites. Builtin descriptor types must only
become eligible after their class namespace is in its final immutable state.

If a descriptor `__get__` mutates the receiver class, descriptor class, or any
other class, that mutation affects later cache hits through the existing
validity-cell and shape invalidation machinery. The current call returns the
value produced by the already-entered `__get__`.

## Edge Cases

The descriptor result is terminal:

```python
class Inner:
    def __get__(self, obj, owner):
        return 2

class Outer:
    def __get__(self, obj, owner):
        return Inner()

class C:
    x = Outer()

C().x  # returns the Inner instance, not 2
```

An instance-level `__get__` is ignored:

```python
class D:
    pass

d = D()
d.__get__ = lambda obj, owner: 1

class C:
    x = d

C().x  # returns d
```

A callable returned by a descriptor is not called by the attribute read:

```python
class D:
    def __get__(self, obj, owner):
        return lambda: 1

class C:
    x = D()

C().x   # returns the function
C().x() # performs a separate call
```

Mutable descriptor classes are semantically supported but not fast-cacheable:

```python
class D:
    def __get__(self, obj, owner):
        return 1

class C:
    x = D()

D.__get__ = lambda self, obj, owner: 2
C().x  # observes 2 through the slow path
```

## Test Plan

Interpreter tests should cover:

- data descriptor beats a receiver-owned attribute;
- metaclass data descriptor beats a class namespace attribute for `C.x`;
- non-data descriptor is shadowed by a receiver-owned attribute;
- class access passes `None` as the object and the class as owner;
- `super(..., cls)` passes `None` as the object and the subclass as owner;
- instance access passes the receiver and `type(receiver)`;
- `super(..., obj)` passes the original receiver and `type(obj)`;
- functions remain on the `BindFunctionReceiver` fast path and produce bound
  methods for ordinary `obj.f`;
- descriptor result is not recursively treated as a descriptor;
- descriptor result is not automatically called;
- instance-level `__get__` on a descriptor object is ignored;
- mutable descriptor class changes are observed through the slow path;
- descriptors with optional parameters work on the slow path;
- callable-object `__get__` implementations work through uncached generic
  callable dispatch;
- non-callable `__get__` values and wrong-arity `__get__` functions fail through
  the ordinary call error path;
- descriptor `__get__` exceptions propagate on uncached and trusted cached paths;
- cached descriptor values are released/kept alive correctly across class-slot
  replacement and cache clearing.

Once trusted builtin descriptor handlers exist, add targeted tests or cache
probes that show immutable descriptor types can install and replay the cached
trusted ternary descriptor-get plan.
