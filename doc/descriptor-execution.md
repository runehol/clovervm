# Descriptor execution

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Partial |
| Scope | Plain-function descriptor reads, writes, and deletes, plus property shortcuts and cache guards |
| Owning layers | Object-model lookup and binding, interpreter dispatch, call adaptation, and inline caches |
| Validated against | `4671784d` (2026-11-23) |
| Supersedes | N/A |

`LoadAttr`, `StoreAttr`, and `DelAttr` execute descriptor protocol hooks
implemented as CloverVM functions. The object model resolves precedence and
returns a plan. The interpreter executes the selected hook through normal
function call machinery. The built-ins `getattr`, `hasattr`, `setattr`, and
`delattr` use uncached lookup and execution.

## Lookup and binding

Descriptor hooks are special methods on the descriptor's type. An instance
attribute named `__get__`, `__set__`, or `__delete__` on the descriptor itself
has no effect on dispatch.

- A data descriptor takes precedence over an instance slot. An instance slot
  takes precedence over a non-data descriptor.
- Either mutation hook gives the descriptor control of both assignment and
  deletion. The absence of the requested hook raises `AttributeError`.
- A getter's return value is the attribute value. Set and delete discard their
  hooks' return values. Exceptions propagate from all three operations.

The resolver chooses how to bind the getter using the winning lookup path.

| Lookup path | Getter arguments |
|---|---|
| Instance class chain | Descriptor, current instance, runtime class |
| Class namespace | Descriptor, `None`, class being read |
| Metaclass chain | Descriptor, class being read, metaclass |

Python gives metaclass data descriptors precedence over the class namespace.
The current resolver searches the class MRO first. This remains a latent
limitation while custom metaclasses are unavailable in Python. Class
namespace and metaclass reads can execute a plain-function getter with the
bindings above. Cached descriptor replay currently covers instance access.

## Read execution and caching

| Winning entry | `LoadAttr` action |
|---|---|
| Instance slot | Read the current instance's slot. |
| Plain class slot | Read the winning class's slot. |
| Function in the instance's class or metaclass lookup path | Execute the shared function getter to produce a bound method. |
| `staticmethod` in the class hierarchy | Execute its non-data descriptor getter to return the wrapped value without binding. |
| `classmethod` in the class hierarchy | Execute its non-data descriptor getter to bind the class supplied by lookup. |
| Exact built-in `property` on an instance | Load the current `fget` from the property and call it with the current instance. If absent, call the property's `__get__` to raise the normal error. |
| General descriptor | Call the selected `__get__` with the binding from lookup. |

On a miss, `AttributeReadDescriptor::lookup_value` supplies the descriptor
object and `AttributeReadPlan::constant_or_getter` supplies the resolved
plain-function hook. An uncached call stages the three arguments using
`prepare_descriptor_get_call`. The getter resumes at the instruction after
`LoadAttr` and returns into the accumulator.

A general instance descriptor hit reloads the descriptor from the guarded
class slot and obtains the instance and runtime class from the current
invocation. The plan caches the resolved hook and the positional adaptation
for the three arguments. Exact instance property hits likewise reload the
property from its class slot. They read the current accessor because calling
`property.__init__` can replace it in place. A read such as `C.x` goes through
the general class-namespace binding. The built-in property hook returns the
property itself in that case.

## Mutation execution and caching

A mutation plan records the descriptor and hook. The set call passes
`(descriptor, receiver, value)`. The delete call passes
`(descriptor, receiver)`. Cached hits use the receiver from the current
operation. Both use the shared descriptor mutation call entry, which preserves
caller state across reentry and propagates exceptions.

`StoreAttr` has a shortcut for exact built-in properties. It loads the current
`fset` and directly invokes a plain-function setter with the receiver and
assigned value. A missing setter or another callable goes through the
property's general `__set__` implementation. `DelAttr` uses the general
`__delete__` path, including for built-in properties.

## Guard and ownership rules

Instance descriptor caches guard the receiver shape and the MRO
shape-and-contents validity cell. The cell covers class-slot replacement and
deletion, including inherited entries. The receiver shape covers instance
shadowing. Eligibility also requires a stable descriptor protocol throughout
the descriptor type's MRO and a descriptor object whose `__class__` cannot be
reassigned. Lookups that depend on a mutable protocol resolve again on each
access, including a direct slot plan that could become a descriptor read after
a type mutation.

Read plans hold the class slot owner and hook. Mutation plans hold the
descriptor and hook. Cache references are weak and use code-object weak
repair. Call preparation passes the current receiver and selected values
through the normal rooted call entry. Reentrant changes affect later lookups
while the current call keeps its selected hook and arguments.

The [attribute benchmark fixture](../benchmark/descriptor_attributes.py)
compares slots, built-in properties, and mutable Python descriptors for read,
write, and reset-and-delete. The last workload includes writing the backing
slot before each deletion. [Shape transitions](object-model.md#transitions)
describe how eligible add/delete pairs reuse shapes and keep those cache
sites warm.

## Direct method lookup

[LoadMethod](call-argument-temporaries-and-lazy-tracebacks.md#direct-method-calls)
uses the same attribute resolution and descriptor precedence as `LoadAttr`.
It resolves the attribute before argument evaluation, then saves its result
as a callable and optional receiver.

| Winning lookup or binding | Callable | Optional receiver |
|---|---|---|
| Exact function in the instance class chain | Function | Current instance |
| Plain function in the accessed class namespace | Function | Absent |
| Plain function bound through the metaclass chain | Function | Accessed class |
| Instance or module slot | Stored value | Absent |
| Exact `staticmethod` selected as a descriptor | Wrapped value | Absent |
| General getter, including `classmethod` | Getter result | Absent |

An inherited classmethod in the class namespace binds the accessed class,
including access through an instance. A classmethod on the metaclass path
uses the metaclass as owner. Apply the wrapper shortcuts only when lookup
selects their descriptor behavior. An existing bound method or another
callable returned by lookup is handled as an ordinary callable by the call
stage. A value read such as `f = obj.m` still exposes the bound method when
the descriptor requires one.

Reuse attribute read plans and their descriptor cacheability rules.
Class-object `LoadMethod` entries use the same combined validity cell as
`LoadAttr`, covering shape and contents on both the accessed class's MRO and
its metaclass's MRO. This includes a cached plain-slot read. A
same-shape write that replaces an inherited plain function with a
`classmethod` must invalidate the plan before replay, so the next lookup
can run the getter with the accessed class as owner. A class-chain plain
slot also needs a stable descriptor protocol on the stored value's type.
Both opcodes check this before admitting the plan to the cache, since a type
can gain `__get__` without a write to the selected slot.

Reload the selected slot and, for exact instance properties, the current
accessor. The lookup cache carries the guarded plan. The pair carries the
result of this invocation. Getter execution uses the managed return
described in the call temporary design.

A present saved receiver implies an exact function, whose call target has
`ReceiverBinding::None`. With an absent saved receiver, the ordinary
call-target resolver may supply `PrependCallable` for a supported callable
object. Callability and signature checks happen after argument construction.
Reentrant lookup or argument code may change later lookups and the saved
object's own callability. The saved attribute value still belongs to the
current invocation.

## Follow-ups

- Extend descriptor dispatch to additional consumers, including `super`,
  descriptor-backed `__call__`, and operator special methods. Operators must
  preserve their operands and reflected-dispatch continuation across hook
  execution.
- Arbitrary callable descriptor hooks, class creation's `__set_name__`, and
  custom `__getattribute__` and `__getattr__` remain separate work.

## Open: attribute access initiated by native code

The checked attribute built-ins execute descriptor hooks. Internal native
callers that still use unchecked `load_attr` or `load_method` can reach the
fatal descriptor branch when Python supplies an override. Examples include
inherited native code accessing `closed` or `flush` on an IOBase subclass.
The native call boundary needs a fallible plan executor and a way to use the
getter's result for a subsequent call while preserving roots and exceptions.
