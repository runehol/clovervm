# Descriptor Execution

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | Lookup and classification only |
| Scope | Executing descriptor `__get__` during attribute reads |
| Owning layers | Runtime attribute semantics, inline caches, interpreter, and call adaptation |
| Validated against | `083d3313` (2026-11-22) |
| Supersedes | N/A |

This document starts with executing descriptor `__get__` for explicit
attribute reads. It also records how later method loads and other descriptor
consumers can share that execution path.

The design below applies to default attribute lookup. Custom
`__getattribute__` and `__getattr__` support is a separate slice; when those
hooks are present, the default descriptor plan must not pretend it describes the
whole attribute read.

The design goal is to let attribute-read inline caches describe an optional
descriptor call plan without turning ordinary attribute reads into generic call
dispatch. The descriptor result is the result of the attribute read. If
`__get__` returns another descriptor or another callable, that value is returned
as-is. Any later call belongs to a later bytecode operation.

## First Implementation Slice

Support explicit instance reads through `LoadAttr` when descriptor protocol
lookup selects a plain `Function` for `__get__`. Keep the existing
`DataDescriptorGet` and `NonDataDescriptorGet` precedence decisions. Execute
the getter without installing a descriptor-execution inline-cache entry in
this slice. Ordinary slot and constant plans keep their existing replay paths.

The first step records the selected getter in the existing read plan.
`AttributeReadDescriptor::lookup_value` is the descriptor object.
`AttributeReadPlan::constant_or_getter` holds the plain `Function` already
found during protocol lookup. The plan kind determines how to read that
field.

```cpp
Value descriptor = read.lookup_value;
Value getter = read.plan.constant_or_getter;
```

Descriptor plans stay uncached. The existing error path handles them until
the call is wired in the second step. The plan fields are borrowed handles.
During call setup the interpreter keeps the getter and arguments live until
the callee frame owns its inputs. A reentrant getter may change the class
attribute or descriptor type. The next lookup observes the change.

Step 2 handles the descriptor branch in `op_load_attr_cache_miss`. An
instance-class-chain read with a selected plain getter stages three positional
arguments at the aligned scratch call window: the descriptor, the current
instance, and its runtime class. It calls
`enter_uncached_resolved_positional_call` with `n_args = 3` and
`instr_len = 4`, using a local call plan and the
[existing argument adapter](function-call-adaptation.md).

Use the normal call-entry dispatch after the helper commits the callee frame.
The callee's saved return address is the instruction after `LoadAttr`. A
normal getter return delivers its value in the accumulator. An entry error
or a getter exception follows the interpreter's pending-exception path.
`LoadAttr`'s direct cache hit keeps its existing dispatch.

Class-namespace, metaclass, and `super` binding are described below for later
slices. Arbitrary callable getter targets, trusted cached getter replay,
first-class bound methods, and native-originated attribute reads also follow
separately.

## Attribute and Method Loads

`LoadAttr` produces one Python-visible value. These are its replay cases
after ordinary lookup has applied descriptor precedence and instance
shadowing.

| Winning entry | Read action | Cached payload |
|---|---|---|
| Instance attribute | Read the current instance slot. | Slot location. |
| Plain class attribute | Read the current value from the winning class slot. | Storage owner and slot location. |
| Exact built-in `property` with instance binding | Load the property's current getter and call it with the current instance. | Property object. |
| General descriptor | Invoke `__get__` with the selected object and owner, then return its result. | Descriptor, hook, and binding plan when the descriptor protocol and lookup are stable. Mutable descriptor types stay uncached. |

The proposed `LoadMethod` runs the same attribute lookup before evaluating
arguments. It adds two binding shortcuts when the winning class entry
qualifies. In every other case it uses the ordinary attribute-read result.

| Winning entry | Value slot | Receiver slot |
|---|---|---|
| Exact `Function` with instance binding | Function, including a thunk backed by native code. | Current instance. |
| Exact built-in `staticmethod` | Current wrapped value. | Empty. |
| Any other lookup result | The value produced by `LoadAttr` semantics. | Empty. |

A function stored in an instance slot follows the ordinary slot read.
An exact built-in `classmethod` follows the cached general descriptor path.
Its getter produces the bound method, which goes in the value slot. The
empty receiver slot uses the VM's absence sentinel.

Both output slots stay live across argument evaluation. The call consumes
the values selected by `LoadMethod` and checks callability after evaluating
the arguments. A lookup or getter exception stops before argument evaluation.

## Built-in Property Reads

For an instance read, lookup can recognize an exact built-in `property` and
cache the property object under the receiver shape and class lookup guards.
The built-in type has an immutable descriptor protocol, but reinitializing a
property instance can replace its getter in place. Each hit loads the current
getter from the cached property and calls it with the current lookup receiver.
The class lookup guard covers replacement of the property in its class slot.

For a read through the class namespace, such as `C.x`, use the general
descriptor path even when the selected entry is an exact built-in
`property`. The hook receives `None` as the object and `C` as the owner.
The hook itself returns the property object. The exact built-in property's
immutable protocol makes this general descriptor plan cacheable under the
usual lookup guards.

A call plan distinguishes the current lookup receiver, a fixed receiver
proved by the guards, and an absent receiver. The instance property plan uses
the first case. The explicit `None` passed by the general descriptor path is
a Python argument.

## Later Consumers

- Descriptor-backed `__call__` still starts with type-MRO lookup. After a
  getter runs, call entry uses its result with the original arguments. A
  receiver supplied by a method load is distinct from any receiver added
  when resolving that value's own `__call__`.
- Operators can reuse descriptor execution and generic call entry. The
  operator keeps the operands and its position in the walk, including
  reflected dispatch after a method returns `NotImplemented`.
- Writes and deletes keep precedence in the object model and execute the
  selected hook in the interpreter. A descriptor with either mutation hook
  controls both operations. A missing requested hook raises `AttributeError`.
  Store and delete discard the hook's return value.

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

## Open: attribute access initiated by native code

Adversarial review of PR #79 at `e49d172a` (2026-10-26) exposed a boundary not
settled by the bytecode execution design above: native implementations can read
attributes or call methods that a Python subclass replaces with descriptors.
A native receiver layout does not prove that Python-visible lookup is a direct
slot read or function load.

Two concrete examples are an IOBase subclass supplying a data descriptor for
`closed`, then calling `_checkClosed()`, and a subclass supplying a descriptor
for `flush`, then calling inherited `close()`. The current native paths use
`load_attr` or `ThreadState::call_clovervm_method_with_args`, which can reach
`load_attr_from_plan`'s fatal descriptor branch. Ordinary bytecode attribute
access instead raises the current unsupported-descriptor `TypeError`.

This is a general native-to-Python attribute/call boundary issue, not an IOBase
special case. Audit other native callers of the unchecked lookup helpers when
addressing it. Keep the solution open until a shared design settles:

- how native-originated attribute reads execute descriptor plans;
- how a descriptor-produced value participates in subsequent method/call binding;
- how Python callbacks, exceptions, reentrancy, and retained receiver/result
  lifetimes cross that boundary;
- which callers can prove a direct read and legitimately use unchecked helpers;
- behavior while descriptor execution remains unsupported, including avoiding
  process termination from Python-controlled descriptor overrides.

No per-class workaround or new native dispatch API is selected by this note.
Regression coverage should include descriptor overrides reached from inherited
native methods, not only descriptor reads issued by bytecode.

## Cache Policy

The outer attribute-read inline cache is still guided by the receiver shape and
lookup validity cell. For descriptor reads, that outer cache proves that the same
descriptor object is the attribute result candidate and that the same lookup
precedence decision still holds.

An immutable descriptor type with stable protocol lookup can use a cached
general descriptor plan. Cache the selected descriptor and hook, plus the binding
information needed for the current read. Replay invokes the hook through
ordinary call entry and uses its result for that read. Class-namespace
`property` reads and built-in `classmethod` use this path.

Mutable descriptor types resolve the plan on each access. A cache hit on
the general path depends on the descriptor protocol and lookup precedence
remaining stable.

### Trusted Handler Replay

A trusted handler is installable in `AttributeReadInlineCache` only when all
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

The trusted-replay slice installs a trusted-handler plan when
`descriptor_get.get_handler` is non-null. The general descriptor plan and the
direct instance-property plan use ordinary call entry.

## Attribute Read Plan Payload for Trusted Getter Replay

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

The cache already has a kind-bearing plan. Direct read kinds keep the existing
slot and constant behavior. Trusted descriptor replay requires a non-null
`plan.descriptor_get.get_handler` and calls it with
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

Direct call opcodes now cache ordinary instance `__call__` backed by a plain
function. The descriptor slow path can reuse the same one-shot call machinery
after it has resolved the getter. Support for arbitrary getter targets is a
later slice. Trusted getter replay uses the handler shape described above.

The call continuation is the next bytecode after `LoadAttr`: `pc + 4`. Entering
the frame should pass `instr_len = 4`, so normal return resumes after the
attribute read. This is a `LoadAttr`-specific single-call continuation; unlike
operator continuations, it does not need to resume a multi-row protocol.

## Slow Path with Trusted Replay

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

Instance class-chain `LoadAttr` plans depend on both shape and contents along the
receiver's class MRO. A same-slot replacement can change a direct read into a
descriptor call, so it must invalidate the previous plan even when no shape
changes. The direct class-slot reads on instances already use this dependency.
For future cached class-object descriptor reads, the guards also cover
metaclass precedence.

The class MRO guard covers which value the namespace holds. The selected
value's type can independently gain or lose descriptor hooks. A direct-read
plan that depends on the absence of a hook requires a stable descriptor
protocol too. Keep that lookup uncached when the selected value's type can
change that protocol.

The descriptor fields on `AttributeReadPlan` add one more assumption: the
descriptor type's `__get__` lookup and trusted handler are stable. The
trusted-replay slice should only cache this when the descriptor type is immutable.
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
`IsImmutable` flag checks at call sites. Builtin descriptor types must only
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

value = C().x  # returns the function
value()        # performs a separate call
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

The first slice should use interpreter tests for:

- data-descriptor precedence and instance shadowing of a non-data descriptor;
- a warmed direct class-slot read dispatches a getter after a descriptor
  replaces the plain value in the same slot;
- a repeated class-slot read dispatches a getter after the existing value's
  type gains `__get__`;
- instance binding with the current receiver and its runtime class;
- returning descriptor and callable values unchanged;
- ignoring an instance-level `__get__` on the descriptor object;
- getter replacement on a mutable descriptor class at a repeated read site;
- defaults, variadic parameters, wrong arity, and getter exceptions;
- reentry that replaces the selected class attribute, with the current read
  returning the original getter's result and the next read observing the
  replacement;
- forced reclamation around frame entry and during the getter, including a
  caller expression with another live temporary.

Check the Release opcode frames and compare the generated direct-hit
`LoadAttr` path against the current version when implementing the slow path.

Follow-up tests should cover:

- a warmed instance-property read uses a new getter after reinitializing the
  same property object in place;
- metaclass data descriptor beats a class namespace attribute for `C.x`;
- class access passes `None` as the object and the class as owner;
- an exact built-in property read through a class namespace replays the cached
  general descriptor plan and returns the property via its hook;
- an inherited built-in class method replays the cached general descriptor
  plan and binds the class through which it was accessed;
- `super(..., cls)` passes `None` as the object and the subclass as owner;
- `super(..., obj)` passes the original receiver and `type(obj)`;
- functions remain on the `BindFunctionReceiver` fast path and produce bound
  methods for ordinary `obj.f`;
- split method loads run getters before argument evaluation and preserve the
  selected target across argument side effects;
- split method loads distinguish a class-chain function from one stored in an
  instance slot, and unwrap an exact built-in static method with an empty
  receiver slot;
- callable-object `__get__` implementations work through uncached generic
  callable dispatch;
- non-callable `__get__` values fail through the ordinary call error path;
- descriptor `__get__` exceptions propagate on trusted cached paths;
- cached descriptor values are released/kept alive correctly across class-slot
  replacement and cache clearing.

Once trusted builtin descriptor handlers exist, add targeted tests or cache
probes that show immutable descriptor types can install and replay the cached
trusted ternary descriptor-get plan.
