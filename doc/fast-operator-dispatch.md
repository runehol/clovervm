# Fast Operator Dispatch

This note sketches inline caching for Python operator and implicit protocol
dispatch. The initial target is get-item subscription dispatch: `obj[key]`.
Get-item subscription exercises the same dunder-method lookup and
validity-cell machinery as overloaded operators, but it has one protocol owner
and no reflected or in-place candidate ordering. The same cache model should
later extend to store/delete subscription, binary and in-place operators,
attribute access, numeric conversion, and truthiness. The goal is to speed up
overloaded protocol dispatch without caching arbitrary Python method behavior.

Python presents these protocols as special-method calls, but the VM should
separate two questions:

- Which dispatch action would Python's operator or protocol semantics select?
- What operation does the selected action perform?

Inline caches may specialize the first question. They may only specialize the
second question when every dispatch-affecting method skipped by the cache is
trusted for that shortcut.

## Goals

- Avoid repeated dunder-method lookup at hot operator and protocol bytecodes.
- Let builtin protocol combinations jump directly to native handlers after the
  generic dispatcher proves the combination.
- Preserve Python semantics for arbitrary `__getitem__`, `__setitem__`,
  `__delitem__`, and later `__add__`, `__radd__`, `__iadd__`, and related
  methods.
- Make cache validity depend on dunder-method lookup dependencies, not merely
  on the class where a method happened to be found.
- Keep the cache model simple enough that the interpreter hot path can validate
  it without reimplementing the generic operator protocol.

## Non-Goals

- Do not cache arbitrary Python method results.
- Do not skip an arbitrary Python method just because it returned
  `NotImplemented` on an earlier execution.
- Do not add deoptimization machinery as part of the first design.
- Do not store per-object operator metadata when the operation can be selected
  from the opcode, operand shape keys/types, and dispatch result.
- Do not replace the existing direct inline-value arithmetic fast paths. SMI
  and SMI-or-bool cases that are already handled by the opcode stay ahead of
  this dispatch layer.
- Do not build a polymorphic inline cache in the first implementation. The code
  object cache should be structured so PIC entries can be added later, but a
  monomorphic entry plus miss replacement is enough for the first slice.
- Do not solve class construction or class-creation protocols in this design.
  `__new__`, `__init__`, metaclass `__call__`, `__init_subclass__`,
  `__mro_entries__`, `__class_getitem__`, `__instancecheck__`, and
  `__subclasscheck__` have their own protocol and cache-shape concerns.
- Do not implement class subscription through `__class_getitem__` in the
  ordinary get-item cache. Class-object receivers should stay on the
  existing slow/error behavior or an explicitly separate class-subscription path
  until that protocol is designed.
- Do not implement arbitrary descriptor dispatch as part of the get-item cache
  shape. The cacheable dunder-call path is function-shaped; descriptor
  cacheability and replay need an explicit extension to this plan.
- Do not treat context managers as solved by this design. Lowering `with` by
  calling `__enter__` and `__exit__` as ordinary methods is not the same as a
  designed dunder-method dispatch cache for context manager syntax.

## Hot-Path Boundary

Fast operator dispatch starts after an opcode's direct inline-value path has
failed or decided that a Python protocol dispatch is required. Exact
native-layout subscription shortcuts for builtin containers belong behind
protocol-selected lookup once the builtin exposes the corresponding visible
dunder method. A bytecode site's first cacheable execution runs the generic
miss path, proves which dunder method Python dispatch selected, and installs an
inline-cache entry for later hits. Trusted native handlers are refinements of
that guarded lookup result, not a parallel pre-lookup builtin ladder.

This migration avoids a fixed global opcode ladder such as "try list, then
tuple, then string, then dict, then generic." It also keeps the trusted-handler
contract meaningful: a native list/tuple/string/dict handler runs from the IC
only after a guarded dunder lookup has selected the trusted builtin method.

When binary arithmetic moves onto this cache model, `+`, `-`, and `*` should
continue to check and handle SMI-plus-SMI inline before probing an operator
cache. Operations whose current fast path treats booleans as integers should
keep that direct SMI-or-bool behavior ahead of the cache as well.

The first failed tag/shape test is cheap enough to pay on every execution. For
arithmetic, the operator cache should therefore live on the non-SMI or otherwise
non-direct continuation, not in front of the primary arithmetic path. This keeps
the common integer path as a tag check plus checked arithmetic while still
giving heap objects, floats, heap ints, strings, lists, and overloaded user
objects a cacheable dispatch route.

`Div` and other operators that do not have a pure SMI-result fast path may enter
the operator-cache continuation earlier, but only after preserving any existing
operator-specific numeric shortcuts that are intentionally faster than generic
dunder-method dispatch.

## Operand Shape Keys

The cache guard vocabulary is the VM's shape model, but hot cache entries should
store opaque comparable shape keys rather than forcing every guard to carry a
materialized `Shape *`. `ShapeKey` is the representation for this: pointer
values use their object shape pointer as the key, and inline values use their
value tag bits as the key.

Shape keys let operator dispatch use one profiling vocabulary across:

- SMI
- bool
- float
- immutable builtin heap values such as string, tuple, and list where relevant
- user instance shapes
- class object shapes
- heap ints

The cache should store the operand shape keys that the generic operator
dispatcher used for dispatch. It should not store an ad hoc mixture of raw tag
checks, native-layout tests, and class checks unless those tests are wrapped as
stable shape-key identities. When later lookup or class information is needed,
the VM can convert a shape key back to the corresponding real shape. Exact
builtin-type guards can still be represented by builtin shape keys when that is
the semantic dependency.

## Dunder Lookup Dependencies

Operator dunder methods use Python's dunder-method lookup path. This is more
conservative than ordinary attribute lookup: it looks through the operand's type
and MRO and does not consult attributes stored directly on the operand object.
That is useful for caching because the dispatch dependency is a type/MRO lookup
dependency, not an object-attribute dependency. Dunder-method lookup still has
to preserve the descriptor behavior of the class/MRO entry it finds; it should
not be treated as a raw dictionary lookup unless clovervm intentionally chooses
and documents a Python deviation.

For subscription there is normally one lookup dependency:

```text
lookup(type(container), "__getitem__")
```

Store and delete subscription use the same shape with `__setitem__` and
`__delitem__`.

For ordinary binary operators, a later extension has two lookup dependencies:

```text
lookup(type(lhs), "__add__")
lookup(type(rhs), "__radd__")
```

For in-place operators there may be three:

```text
lookup(type(lhs), "__iadd__")
lookup(type(lhs), "__add__")
lookup(type(rhs), "__radd__")
```

The validity cells must cover the full dunder-method lookup dependency,
including negative lookups. They must be invalidated by mutations anywhere along
the relevant MRO path, base/MRO changes, and descriptor-relevant class-entry
changes.

The contents-sensitive MRO validity machinery is the expected primitive for
this. The lookup dependency must protect lookups rooted in a class's
materialized MRO and must be invalidated when the root class or any class on the
MRO path changes shape or class-member contents. That is broader than a
per-name lookup cell, but it covers positive and negative dunder-method lookup
results for operator dispatch. A weaker method-call cache validity cell that
survives class contents writes is not enough to guard callable identity for
special-method operator caches. Unrelated class-member writes may invalidate
operator caches; that is an acceptable tradeoff for the first design because
code that mutates classes on a hot operator path can pay the cache-miss cost.

## Cache Kinds

Subscription caches should use two cache actions with a shared validation
shape:

```cpp
enum class SubscriptionCacheKind : uint8_t
{
    Empty,
    TrustedHandler,
    DunderMethodCall,
};
```

Both non-empty subscription cache kinds should guard:

- container operand shape key
- key operand shape key
- container protocol lookup validity

This intentionally over-guards some cases. The useful property is that a cache
hit proves the generic dispatcher would select the same first dispatch action
under the current operand relationship and lookup result.

The key `ShapeKey` guard is required even for a dunder-method-call cache. The
dunder lookup itself depends only on the container type, but
the observed key `ShapeKey` is part of the site's profile and keeps trusted
handlers from quietly skipping key-side behavior they have not proved.

Operator inline caches live on the code object, like the existing attribute,
module-global, and function-call caches. `LoadSubscript` forms that can invoke
`__getitem__` should carry a get-item cache index operand. Store/delete
subscription should have their own cache shapes; they should not be folded into
the get-item entry just because the lookup pattern is similar.

On a miss, the generic dispatcher runs and the opcode miss path installs a new
entry for that cache index. The entry may represent a successful trusted handler,
a successful dunder-method-call plan, or a miss/unhandled plan when that is
useful for profiling and replacement. A monomorphic cache may replace the single
entry on every miss. Bounded polymorphic caches and megamorphic state are later
policy extensions.

Binary, in-place, and comparison caches should use the same broad action
categories, but they need wider guards and a small continuation shape for
reflected candidates, `NotImplemented`, and in-place fallback. The first
implementation should split these protocols into three tiers:

```text
direct inline primitive path
trusted native handler cache
cached single Python dunder candidate with NotImplemented continuation
generic slow dispatch
```

The cached Python-candidate tier is important for ordinary overloaded Python
classes. A design that only caches trusted native handlers would leave common
custom cases such as dataclass equality or simple `Vector.__add__` permanently
on the slow helper path. The middle tier may cache a single selected Python
dunder call, then check whether that call returned `NotImplemented`. If it did
not, the returned value is the operator result. If it did, the interpreter
continues the operator protocol after the candidate that was already called.

## Dunder Method Lookup Descriptor And Plan

The miss path must record a dunder-method lookup descriptor, not an ordinary
attribute value. Dunder-method lookup bypasses the instance dictionary but still
has descriptor and binding semantics for the class/MRO entry it finds.

As with the existing attribute and module-global cache machinery, split the
miss-time discovery from the replayable cache payload:

```text
DunderMethodLookupDescriptor = what lookup discovered on this miss
DunderMethodDispatchPlan     = the cacheable dispatch fact and replay payload
```

A cacheable descriptor should represent at least:

- whether the lookup was found, negative, uncacheable, or errored
- the class whose MRO was searched
- the defining class where the method was found, when found
- the resolved value or callable identity, when found
- the dispatch plan produced by the lookup
- optional cache blockers explaining why the lookup cannot be cached
- optional VM-owned slot identity or trusted-method metadata for recognizers

The plan is the part that may be stored in an inline cache:

```cpp
enum class DunderMethodLookupStatus : uint8_t
{
    Found,
    Missing,
    Uncacheable,
    Error,
};

enum class DunderMethodBindingKind : uint8_t
{
    NoBinding,
    BindReceiver,
    RequiresDescriptorDispatch,
};

struct DunderMethodDispatchPlan
{
    ValidityCell *lookup_validity_cell = nullptr;

    // Dunder-method-call replay payload is family-specific. Cache entries
    // should store a replayable call plan, not an arbitrary callable Value.
    DunderMethodBindingKind binding = DunderMethodBindingKind::NoBinding;
};

struct DunderMethodLookupDescriptor
{
    DunderMethodLookupStatus status = DunderMethodLookupStatus::Missing;
    ClassObject *receiver_class = nullptr;
    Value resolved_value = Value::not_present();
    DunderMethodDispatchPlan plan;
};
```

Putting the lookup validity cell in `DunderMethodDispatchPlan` is slightly
impure for trusted-handler entries: the trusted handler does not replay the
dunder method. It is still useful because both trusted-handler and
dunder-method-call cache entries depend on the same dunder-method lookup fact.
The shared lookup dependency also guards cached callable identity for
function-shaped replay: if class contents change and a different method would
now be selected, the contents-sensitive lookup cell invalidates the entry. That
lets operator caches avoid embedding a separate function-call validity cell for
the selected callable.

The invariant is:

```text
non-empty cache entry:
    shape guards pass
    method_plan.lookup_validity_cell is valid
    method_plan is valid for either trusted handler or dunder-method-call replay
```

The exact C++ type is an implementation detail. Cache hits must be able to
recreate the same first dunder-method call the generic dispatcher selected
without falling back to ordinary attribute lookup, and they should store only
the replay facts needed for the supported cacheable call shape. For
function-shaped dunder methods this means storing function identity, code object,
argument count, binding shape, and any fixed/default/varargs adaptation state;
it does not mean storing a heterogeneous callable `Value`.

Before entering a cached Python function, the opcode must already have the call
arguments laid out in frame registers in the normal function-call shape. Cached
dunder-call replay cannot depend on "receiver in a register, key or rhs in the
accumulator" layouts, because function entry consumes a contiguous prepared
argument span. Get-item uses `[container, key]`, with the container at the
bytecode's first argument register and the key in the preceding register.

## Trusted Handler Cache

A trusted handler cache is a direct protocol shortcut. It may replace the rest
of the selected protocol dispatch with a handler only when every
dispatch-affecting method execution skipped by the cache is trusted for that
handler.

Trust is a cache permission, not an implementation-language property. A C++
method is not automatically trusted, and a Python-implemented method is not
automatically untrusted. The relevant question is whether the VM may rely on
that method's dispatch-visible behavior being stable under the installed guards.

```cpp
using UnaryHandler = Value (*)(ThreadState *, Value);
using BinaryHandler = Value (*)(ThreadState *, Value, Value);
using TernaryHandler = Value (*)(ThreadState *, Value, Value, Value);

enum class TrustedHandlerArity : uint8_t
{
    None,
    Unary,
    Binary,
    Ternary,
};

struct TrustedHandlerResolution
{
    TrustedHandlerArity arity = TrustedHandlerArity::None;

    union
    {
        UnaryHandler unary;
        BinaryHandler binary;
        TernaryHandler ternary;
    };
};

using TrustedHandlerResolver = TrustedHandlerResolution (*)(
    VirtualMachine *, ShapeKey, ShapeKey, ShapeKey);
```

The cached handler is not the Python method object. It is a VM-selected native
implementation for a fully proven protocol case:

```text
(op, operand shape keys, selected method/code object) -> typed handler
```

Examples:

```text
list[smi]       -> list_getitem_smi_fast
tuple[smi]      -> tuple_getitem_smi_fast
str[slice]      -> str_getitem_slice_fast
dict[str]       -> dict_getitem_str_fast
```

Handlers are arity-typed because the hit path should call the exact native
signature it cached. Resolvers are universal because they run on the miss and
installation path, where one code-object field is less error-prone than a union
of resolver pointers. The resolver takes `VirtualMachine *`, not
`ThreadState *`: handler selection should depend on VM-owned metadata, the
selected code object, and operand `ShapeKey`s, not on frame state, pending
exceptions, or the current thread-local execution context. Different
operator-shaped methods provide different resolver functions rather than sharing
an operator-family tag.

A resolver may return `TrustedHandlerArity::None` when the selected method is
trusted but has no direct handler for the observed operand shape keys. The miss
path can still install a dunder-method-call entry when the lookup and call plan
are cacheable.

A later binary-operator cache can use the same trusted-handler and
dunder-method-call ideas with table state:

```cpp
enum class BinaryOperatorCacheKind : uint8_t
{
    Empty,
    TrustedHandler,
    DunderMethodCall,
};

struct BinaryDispatchCacheCandidate
{
    BinaryOperatorCacheKind kind;
    BinaryHandler handler;
    OperatorDispatchTable *dispatch_table;
    uint8_t candidate_index;
    DunderMethodLookupDescriptor lookup;
    LookupValidity left_validity;
    LookupValidity right_validity;
};
```

For a trusted handler entry, the handler is a complete replacement for the
operator protocol under the cache guards. For a dunder-method-call entry,
`dispatch_table` and `candidate_index` identify the single Python candidate to
call and the continuation state to store before entering it.

Trusted-handler selection should not be a parallel `__trusted_add__`-style
protocol. That would duplicate Python's operator precedence rules and create a
second path that has to stay semantically aligned with generic dispatch.

Instead, the miss path should walk the same operator dispatch table that the
continuation path uses. At each table step it performs the actual lookup,
applicability check, trusted-handler check, Python-call setup, or fallback. If a
step can be collapsed to a complete trusted native handler for the current
operand shapes, the miss path installs that handler and executes it. If the next
step is an ordinary cacheable Python candidate, the miss path installs a
dunder-method-call entry for that candidate and enters it with the hidden
continuation prefix prepared. There is no separate recognizer that reviews a
completed Python-handler execution and then decides whether to install a trusted
handler.

## Slot Metadata

CPython-style dunder-method slot tables are a natural place to declare whether
a resolved dunder method is trusted for operator-cache purposes. The slot table
can answer questions such as:

```text
type(lhs).__add__ resolved to slot nb_add for str
that slot is trusted for binary-add dispatch
```

This should not mean that the slot table alone selects the cached handler. A
single trusted slot may participate in multiple optimized cases:

```text
float.__add__ with float -> float_add_float_fast
float.__add__ with smi   -> float_add_smi_fast
```

and a complete trusted handler decision may also depend on skipped or
`NotImplemented` candidates earlier in the table. The slot table should declare
trust and provide stable slot identity. The table walker should combine:

```text
(operator table step, operand shape keys/types, trusted slot identities,
 skipped trusted candidates)
    -> TrustedHandler or no handler
```

This avoids turning slot lookup into a second operator protocol while still
giving the table walker a compact, VM-owned representation of the methods that
operator dispatch actually found.

In clovervm, trust should probably attach to the resolved method value or to
metadata reached from that value, not purely to the shape descriptor slot. Shape
descriptors describe where and how a property is stored. A trusted operator
promise describes the behavior of the current value stored there. If class
contents replace a trusted method with an untrusted one in the same slot, the
MRO shape-and-contents validity cell will invalidate existing caches, but a
descriptor-only trust bit would still describe the slot rather than the new
value.

A descriptor flag may still be useful as a cheap structural hint for fixed VM
slots, especially read-only stable slots installed by the runtime. It should not
be the only authority for trust unless the slot's contents are immutable by
construction.

## Trusted Handler Resolver

Trust is represented by a resolver attached to the selected method's code
object. If the selected method has no `TrustedHandlerResolver`, the dispatcher
may still cache the dunder-method call, but it has no permission to replace that
method with a direct native handler.

Resolver presence and handler availability are separate facts:

```text
resolver == nullptr:
    no permission to replace the method behavior

resolver != nullptr && resolution.arity == TrustedHandlerArity::None:
    the method is trusted, but this operand-shape combination has no direct
    handler

resolver != nullptr && resolution.arity != TrustedHandlerArity::None:
    a direct handler may be installed after the operation completes
```

The resolver is called by the miss path with the operand shape keys observed by
the generic dispatcher. It must not execute Python behavior or depend on
`ThreadState`; it only maps trusted method identity plus shape keys to a typed
native handler. If it returns `None`, the miss path may still install a
`DunderMethodCall` entry when the lookup and call plan are cacheable. That entry
skips future lookup but still executes the selected dunder method.

A trusted handler must satisfy a strict contract:

- it does not skip arbitrary Python behavior that the generic outer operator
  dispatch would have executed
- it does not skip user-visible descriptor behavior from the outer operator
  dispatch
- its result depends only on the operation, operand representations, and guarded
  dispatch facts
- its pending-exception behavior matches the generic operation
- any `NotImplemented` behavior is impossible or explicitly represented in the
  dispatch plan
- every dispatch-affecting candidate whose execution is skipped by the handler
  is trusted
- it may allocate or raise, but it must not run Python bytecode or stop at a
  safepoint before returning to the opcode handler
- it may run internal operator dispatch if that dispatch is part of the trusted
  operation's normal semantics; those inner dispatches keep their own cache
  behavior

## Dunder Method Call Cache

For arbitrary dunder methods, the cache may skip lookup but not method behavior.
It may call only the method that the family-specific dispatcher selected under
the guarded dispatch facts. The method may be implemented in Python or C++; the
cache kind is about preserving the dunder method call, not the implementation
language.

For get-item subscription, the action is:

```text
call cached selected __getitem__ method
if it raises or returns exception_marker:
    propagate the exception
otherwise:
    return the method result
```

There is no `NotImplemented` continuation for get-item subscription. Returning
`NotImplemented` from `__getitem__` returns that singleton as the subscription
result.

Later continuation protocols, such as binary operators and in-place operators,
need a family-specific result policy:

```text
call cached first dunder method
if result is not NotImplemented:
    return result
otherwise:
    resume generic operator dispatch after the candidate just executed
```

For those later continuation protocols, the fallback must not call the first
candidate again. The cache has already executed that dunder method, and running
the full dispatcher from the beginning would duplicate its side effects.
Instead, the cache entry must contain enough resume information to continue the
operator protocol from the next candidate: for example, whether the cached first
call was the lhs forward method, rhs reflected method, or lhs in-place method.

The resumed dispatcher must re-check any dispatch facts that may have been
invalidated by the first dunder method call before it executes later candidates.
The first arbitrary dunder method may have mutated globals, object state,
classes, descriptors, bases, or MROs before returning `NotImplemented`. After
such a mutation, the cached first-candidate fact may no longer imply that the
second candidate found on the original miss is still the correct next action. The
resume path therefore continues the protocol semantically from "the first
candidate has already returned `NotImplemented`" rather than blindly invoking a
cached second method.

The cache should store a precise dunder-method lookup result, not just an
ordinary attribute value. Dunder-method lookup bypasses instance dictionaries
and has descriptor/binding behavior tied to the type lookup. A cacheable result
therefore needs enough information to reproduce the first call exactly through a
`DunderMethodDispatchPlan`.

## Operator Call Entry And NotImplemented Continuations

Any cache entry that replays a Python dunder method needs a way to enter Python
bytecode with normal function-call argument layout. Protocols with no post-call
continuation can enter the selected callee directly from the opcode after
arranging the dunder-call arguments in frame registers. The callee's accumulator
return value is the operation result, and a raised exception propagates
normally. No separate continuation opcode is needed for get-item subscription
because returning `NotImplemented` is not a protocol continuation.

That direct-entry shape is not enough for protocols that may need to resume
after the first dunder call returns. Binary, in-place, and comparison caches may
call a cached first candidate, inspect `NotImplemented`, and then continue with
later protocol candidates without calling the first candidate again. Those
families need an interpreter-level continuation shape.

The preferred continuation shape is a paired operator opcode and a single-byte
check opcode:

```text
BinaryAdd cache_idx ...
CheckOperatorNotImplemented
```

The operator opcode owns the direct inline fast path, trusted-handler cache hit,
cache miss, and cached Python-candidate call. If a direct inline path or trusted
native handler succeeds, the opcode produces the result in the accumulator and
advances past both itself and the following `CheckOperatorNotImplemented` byte.
Those common paths do not allocate continuation state, do not spill accumulator
operands, and do not dispatch the check opcode.

If the operator opcode enters a cached Python dunder candidate, it first stores
a hidden continuation prefix in the caller frame, then builds the actual Python
call arguments after that prefix:

```text
hidden continuation prefix:
    operator dispatch table pointer or id
    SMI next_candidate_index
    lhs Value
    rhs Value
    optional third Value for ternary operator tables
    padding/alignment if needed

actual Python call arguments:
    candidate-specific argument layout
```

The hidden prefix is not part of the callee-visible argument span. It is
ordinary frame storage for root scanning: the operator dispatch table field is
non-heap VM metadata, the next index is an SMI, and the remaining slots are the
semantic operands required by that table's arity. Binary tables save `lhs` and
`rhs`; ternary tables additionally save the third operand. The table field may
be a direct pointer if the frame scanner can identify it as non-scanned
metadata, or a small id if that is easier for the frame representation. The call
argument slots are disposable and must not be reused as continuation operands,
because the callee or call setup may mutate them. This separation also handles
reflected candidates:

```text
lhs.__add__(rhs):
    saved lhs/rhs = semantic lhs/rhs
    call args = lhs, rhs

rhs.__radd__(lhs):
    saved lhs/rhs = semantic lhs/rhs
    call args = rhs, lhs
```

The first cached Python-candidate implementation should require a normal
function-shaped call plan: fixed positional arity, predictable `self` binding,
and no descriptor or call adaptation that changes the argument layout in a
candidate-specific way. Odd cases such as staticmethod-shaped operator methods,
varargs/default adaptation, missing self, or descriptor dispatch should execute
through the generic slow dispatcher without installing this cache kind. That
keeps the continuation frame layout independent of pathological dunder method
shapes while still accelerating ordinary Python operator overloads.

The Python candidate returns to `CheckOperatorNotImplemented`. That opcode has
no immediates. It has an implicit frame-shape contract: it is only a valid
return target after an operator-candidate call that populated the hidden prefix.
The opcode checks the accumulator:

```text
if accumulator is not NotImplemented:
    finish the logical operator instruction
else:
    table = prefix.dispatch_table
    next = prefix.next_candidate_index
    lhs = prefix.lhs
    rhs = prefix.rhs
    continue table-driven operator dispatch from next
```

Debug builds should assert the frame contract hard: the table field identifies
valid immutable VM metadata, the candidate index slot is an SMI, the candidate
index is valid for that table, and the saved operand slots are available to the
root scanner.

For protocols with multiple possible calls, such as binary and in-place
operators, the same check opcode can be re-entered repeatedly. The cold
continuation dispatcher can update the same hidden prefix with the next
candidate index, rebuild call arguments for the next candidate, and enter that
candidate with the same return address. This repeated `NotImplemented` path is
not expected to be fast; it exists to preserve Python semantics without
building a general VM continuation system.

This keeps the direct and trusted-handler fast paths to one opcode dispatch and
avoids tagging return PCs or checking continuation bits on every function
return. It does impose bytecode invariants:

- the operator/check pair is one logical instruction for bytecode scanning,
  printing, source offsets, and JIT decoding;
- ordinary control flow must target the operator byte, never the check byte;
- the operator handler must skip the check byte on every direct, trusted, or
  non-continued completion;
- the check handler should assert that valid hidden continuation state is
  present.

This design is interpreter-friendly but may be awkward for a JIT to reproduce
directly. A first JIT strategy can compile only the enter fast path and side-exit
to the interpreter on cache miss or arbitrary dunder call. The interpreter then
handles lookup, the Python call, the `pc + 1` continuation return, and any cache
installation. Under that model, the JIT only has to understand the compound
instruction's full length and treat the continue byte as an internal landing pad,
not as a normal trace entry.

## Table-Driven Operator Continuation

The continuation prefix should store a semantic dispatch table, not an opcode.
The table itself is immutable VM metadata. It may be referenced by pointer if
the continuation prefix has a non-scanned metadata slot, or by SMI id if keeping
every prefix field `Value`-shaped is simpler:

```text
prefix[0] = operator_dispatch_table pointer or Value::from_smi(table_id)
prefix[1] = Value::from_smi(next_candidate_index)
prefix[2] = lhs
prefix[3] = rhs
prefix[4] = optional third operand for ternary tables
```

The tables are not ordinary heap objects and should not be exposed through
ordinary object inspection. They may contain immortal interned strings for
dunder names: those strings are deliberately not GC-scanned or collected, so
storing them in immutable VM metadata is safe. Do not store ordinary heap
strings or other collectable values in these tables.

A table is a compact protocol program. Each step describes the operation the
dispatcher should perform directly: how to find the method, how to lay out the
call arguments, or which fallback to apply. It should not require a separate
large switch over an `OperatorKind` to recover the dunder name or argument
orientation.

```cpp
struct OperatorDispatchTable
{
    OperatorStep steps[MaxSteps];
};

struct OperatorStep
{
    OperatorStepAction action;
    OperatorStepApplicability applicability;
    ImmortalInternedString *dunder_name;  // null for fallback steps
};
```

`OperatorStepAction` should be call-layout oriented, not operator-family
oriented. If ordinary binary, in-place binary, and comparison candidates use the
same lookup root and argument layout, they should use the same action. The
dunder name stored in the step differentiates `__add__`, `__iadd__`, and
`__lt__`. Different arities need different actions because they build different
call frames.

`OperatorStepApplicability` owns the separate question of whether the step is
currently eligible. This keeps method elision and subtype-priority checks out of
the frame-setup action. Useful applicability cases are:

```text
Always                  fallback step; no method lookup
IfMethodFound           call only if the named method resolves
IfMethodFoundDistinct   call only if the named method resolves and is not a
                        duplicate of the candidate that the protocol would
                        otherwise already have tried
```

The exact representation of the distinctness check can be table-local metadata
or a small protocol-specific helper, but it should not create separate call
actions for arithmetic, comparison, and in-place candidates when their call
frames are identical.

Useful initial actions are:

```text
CallUnary              lookup type(lhs).dunder_name; call args: lhs
CallBinary             lookup type(lhs).dunder_name; call args: lhs, rhs
CallBinaryReflected    lookup type(rhs).dunder_name; call args: rhs, lhs
CallTernary            lookup type(lhs).dunder_name; call args: lhs, rhs, third
CallTernaryReflected   lookup type(rhs).dunder_name; call args: rhs, lhs, third
IdentityEq             fallback: lhs is rhs
IdentityNe             fallback: lhs is not rhs
ContainsFallback       fallback: membership via iteration or sequence indexing
RaiseUnsupported       fallback: operator-specific unsupported TypeError
RaiseOrdering          fallback: ordering TypeError
```

For an operation whose candidate order can differ by operand relationship, use
separate tables rather than making one table dynamically reorder itself. Table
operands are semantic protocol operands, not necessarily source-expression
operands. For example, `a in b` should compile to a `Contains` operation with
`lhs = b` and `rhs = a`, because the primary dunder name is `__contains__` and
the protocol receiver is the container.

The complete initial multi-candidate and resumable-fallback table inventory is
below. Single-call protocols such as unary operators, subscription, length,
representation, and ordinary conversions do not need these continuation tables
until they grow fallback behavior that must resume after a Python call.

Ordinary binary arithmetic and bitwise operators each need two table orderings:
in the examples below, call rows use `IfMethodFound` unless marked as a
distinctness-checked candidate by the table builder, and fallback rows use
`Always`. A `*ReflectedFirst` table is selected only when the reflected
candidate has already passed the right-subclass and distinct-implementation
applicability checks. A reflected second candidate still needs the
distinct-implementation check at execution time, because the first Python call
may have mutated the relevant classes before returning `NotImplemented`.

```text
BinaryAddNormalFirst
    0: CallBinary("__add__")
    1: CallBinaryReflected("__radd__")
    2: RaiseUnsupported

BinaryAddReflectedFirst
    0: CallBinaryReflected("__radd__")
    1: CallBinary("__add__")
    2: RaiseUnsupported

BinarySubNormalFirst
    0: CallBinary("__sub__")
    1: CallBinaryReflected("__rsub__")
    2: RaiseUnsupported

BinarySubReflectedFirst
    0: CallBinaryReflected("__rsub__")
    1: CallBinary("__sub__")
    2: RaiseUnsupported

BinaryMulNormalFirst
    0: CallBinary("__mul__")
    1: CallBinaryReflected("__rmul__")
    2: RaiseUnsupported

BinaryMulReflectedFirst
    0: CallBinaryReflected("__rmul__")
    1: CallBinary("__mul__")
    2: RaiseUnsupported

BinaryMatmulNormalFirst
    0: CallBinary("__matmul__")
    1: CallBinaryReflected("__rmatmul__")
    2: RaiseUnsupported

BinaryMatmulReflectedFirst
    0: CallBinaryReflected("__rmatmul__")
    1: CallBinary("__matmul__")
    2: RaiseUnsupported

BinaryTrueDivNormalFirst
    0: CallBinary("__truediv__")
    1: CallBinaryReflected("__rtruediv__")
    2: RaiseUnsupported

BinaryTrueDivReflectedFirst
    0: CallBinaryReflected("__rtruediv__")
    1: CallBinary("__truediv__")
    2: RaiseUnsupported

BinaryFloorDivNormalFirst
    0: CallBinary("__floordiv__")
    1: CallBinaryReflected("__rfloordiv__")
    2: RaiseUnsupported

BinaryFloorDivReflectedFirst
    0: CallBinaryReflected("__rfloordiv__")
    1: CallBinary("__floordiv__")
    2: RaiseUnsupported

BinaryModNormalFirst
    0: CallBinary("__mod__")
    1: CallBinaryReflected("__rmod__")
    2: RaiseUnsupported

BinaryModReflectedFirst
    0: CallBinaryReflected("__rmod__")
    1: CallBinary("__mod__")
    2: RaiseUnsupported

BinaryDivmodNormalFirst
    0: CallBinary("__divmod__")
    1: CallBinaryReflected("__rdivmod__")
    2: RaiseUnsupported

BinaryDivmodReflectedFirst
    0: CallBinaryReflected("__rdivmod__")
    1: CallBinary("__divmod__")
    2: RaiseUnsupported

BinaryPowNormalFirst
    0: CallBinary("__pow__")
    1: CallBinaryReflected("__rpow__")
    2: RaiseUnsupported

BinaryPowReflectedFirst
    0: CallBinaryReflected("__rpow__")
    1: CallBinary("__pow__")
    2: RaiseUnsupported

BinaryLShiftNormalFirst
    0: CallBinary("__lshift__")
    1: CallBinaryReflected("__rlshift__")
    2: RaiseUnsupported

BinaryLShiftReflectedFirst
    0: CallBinaryReflected("__rlshift__")
    1: CallBinary("__lshift__")
    2: RaiseUnsupported

BinaryRShiftNormalFirst
    0: CallBinary("__rshift__")
    1: CallBinaryReflected("__rrshift__")
    2: RaiseUnsupported

BinaryRShiftReflectedFirst
    0: CallBinaryReflected("__rrshift__")
    1: CallBinary("__rshift__")
    2: RaiseUnsupported

BinaryAndNormalFirst
    0: CallBinary("__and__")
    1: CallBinaryReflected("__rand__")
    2: RaiseUnsupported

BinaryAndReflectedFirst
    0: CallBinaryReflected("__rand__")
    1: CallBinary("__and__")
    2: RaiseUnsupported

BinaryXorNormalFirst
    0: CallBinary("__xor__")
    1: CallBinaryReflected("__rxor__")
    2: RaiseUnsupported

BinaryXorReflectedFirst
    0: CallBinaryReflected("__rxor__")
    1: CallBinary("__xor__")
    2: RaiseUnsupported

BinaryOrNormalFirst
    0: CallBinary("__or__")
    1: CallBinaryReflected("__ror__")
    2: RaiseUnsupported

BinaryOrReflectedFirst
    0: CallBinaryReflected("__ror__")
    1: CallBinary("__or__")
    2: RaiseUnsupported
```

In-place arithmetic and bitwise operators prepend the in-place candidate, then
fall back to the same ordinary binary ordering:

```text
InPlaceAddNormalFirstFallback
    0: CallBinary("__iadd__")
    1: CallBinary("__add__")
    2: CallBinaryReflected("__radd__")
    3: RaiseUnsupported

InPlaceAddReflectedFirstFallback
    0: CallBinary("__iadd__")
    1: CallBinaryReflected("__radd__")
    2: CallBinary("__add__")
    3: RaiseUnsupported

InPlaceSubNormalFirstFallback
    0: CallBinary("__isub__")
    1: CallBinary("__sub__")
    2: CallBinaryReflected("__rsub__")
    3: RaiseUnsupported

InPlaceSubReflectedFirstFallback
    0: CallBinary("__isub__")
    1: CallBinaryReflected("__rsub__")
    2: CallBinary("__sub__")
    3: RaiseUnsupported

InPlaceMulNormalFirstFallback
    0: CallBinary("__imul__")
    1: CallBinary("__mul__")
    2: CallBinaryReflected("__rmul__")
    3: RaiseUnsupported

InPlaceMulReflectedFirstFallback
    0: CallBinary("__imul__")
    1: CallBinaryReflected("__rmul__")
    2: CallBinary("__mul__")
    3: RaiseUnsupported

InPlaceMatmulNormalFirstFallback
    0: CallBinary("__imatmul__")
    1: CallBinary("__matmul__")
    2: CallBinaryReflected("__rmatmul__")
    3: RaiseUnsupported

InPlaceMatmulReflectedFirstFallback
    0: CallBinary("__imatmul__")
    1: CallBinaryReflected("__rmatmul__")
    2: CallBinary("__matmul__")
    3: RaiseUnsupported

InPlaceTrueDivNormalFirstFallback
    0: CallBinary("__itruediv__")
    1: CallBinary("__truediv__")
    2: CallBinaryReflected("__rtruediv__")
    3: RaiseUnsupported

InPlaceTrueDivReflectedFirstFallback
    0: CallBinary("__itruediv__")
    1: CallBinaryReflected("__rtruediv__")
    2: CallBinary("__truediv__")
    3: RaiseUnsupported

InPlaceFloorDivNormalFirstFallback
    0: CallBinary("__ifloordiv__")
    1: CallBinary("__floordiv__")
    2: CallBinaryReflected("__rfloordiv__")
    3: RaiseUnsupported

InPlaceFloorDivReflectedFirstFallback
    0: CallBinary("__ifloordiv__")
    1: CallBinaryReflected("__rfloordiv__")
    2: CallBinary("__floordiv__")
    3: RaiseUnsupported

InPlaceModNormalFirstFallback
    0: CallBinary("__imod__")
    1: CallBinary("__mod__")
    2: CallBinaryReflected("__rmod__")
    3: RaiseUnsupported

InPlaceModReflectedFirstFallback
    0: CallBinary("__imod__")
    1: CallBinaryReflected("__rmod__")
    2: CallBinary("__mod__")
    3: RaiseUnsupported

InPlacePowNormalFirstFallback
    0: CallBinary("__ipow__")
    1: CallBinary("__pow__")
    2: CallBinaryReflected("__rpow__")
    3: RaiseUnsupported

InPlacePowReflectedFirstFallback
    0: CallBinary("__ipow__")
    1: CallBinaryReflected("__rpow__")
    2: CallBinary("__pow__")
    3: RaiseUnsupported

InPlaceLShiftNormalFirstFallback
    0: CallBinary("__ilshift__")
    1: CallBinary("__lshift__")
    2: CallBinaryReflected("__rlshift__")
    3: RaiseUnsupported

InPlaceLShiftReflectedFirstFallback
    0: CallBinary("__ilshift__")
    1: CallBinaryReflected("__rlshift__")
    2: CallBinary("__lshift__")
    3: RaiseUnsupported

InPlaceRShiftNormalFirstFallback
    0: CallBinary("__irshift__")
    1: CallBinary("__rshift__")
    2: CallBinaryReflected("__rrshift__")
    3: RaiseUnsupported

InPlaceRShiftReflectedFirstFallback
    0: CallBinary("__irshift__")
    1: CallBinaryReflected("__rrshift__")
    2: CallBinary("__rshift__")
    3: RaiseUnsupported

InPlaceAndNormalFirstFallback
    0: CallBinary("__iand__")
    1: CallBinary("__and__")
    2: CallBinaryReflected("__rand__")
    3: RaiseUnsupported

InPlaceAndReflectedFirstFallback
    0: CallBinary("__iand__")
    1: CallBinaryReflected("__rand__")
    2: CallBinary("__and__")
    3: RaiseUnsupported

InPlaceXorNormalFirstFallback
    0: CallBinary("__ixor__")
    1: CallBinary("__xor__")
    2: CallBinaryReflected("__rxor__")
    3: RaiseUnsupported

InPlaceXorReflectedFirstFallback
    0: CallBinary("__ixor__")
    1: CallBinaryReflected("__rxor__")
    2: CallBinary("__xor__")
    3: RaiseUnsupported

InPlaceOrNormalFirstFallback
    0: CallBinary("__ior__")
    1: CallBinary("__or__")
    2: CallBinaryReflected("__ror__")
    3: RaiseUnsupported

InPlaceOrReflectedFirstFallback
    0: CallBinary("__ior__")
    1: CallBinaryReflected("__ror__")
    2: CallBinary("__or__")
    3: RaiseUnsupported
```

Ternary `pow(lhs, rhs, mod)` uses ternary call actions for the same dunder
names. It does not have an in-place table:

```text
TernaryPowNormalFirst
    0: CallTernary("__pow__")
    1: CallTernaryReflected("__rpow__")
    2: RaiseUnsupported

TernaryPowReflectedFirst
    0: CallTernaryReflected("__rpow__")
    1: CallTernary("__pow__")
    2: RaiseUnsupported
```

Rich comparisons use binary call actions, but with comparison-specific
reflected names and fallbacks:

```text
CompareLtNormalFirst
    0: CallBinary("__lt__")
    1: CallBinaryReflected("__gt__")
    2: RaiseOrdering

CompareLtReflectedFirst
    0: CallBinaryReflected("__gt__")
    1: CallBinary("__lt__")
    2: RaiseOrdering

CompareLeNormalFirst
    0: CallBinary("__le__")
    1: CallBinaryReflected("__ge__")
    2: RaiseOrdering

CompareLeReflectedFirst
    0: CallBinaryReflected("__ge__")
    1: CallBinary("__le__")
    2: RaiseOrdering

CompareEqNormalFirst
    0: CallBinary("__eq__")
    1: CallBinaryReflected("__eq__")
    2: IdentityEq

CompareEqReflectedFirst
    0: CallBinaryReflected("__eq__")
    1: CallBinary("__eq__")
    2: IdentityEq

CompareNeNormalFirst
    0: CallBinary("__ne__")
    1: CallBinaryReflected("__ne__")
    2: IdentityNe

CompareNeReflectedFirst
    0: CallBinaryReflected("__ne__")
    1: CallBinary("__ne__")
    2: IdentityNe

CompareGtNormalFirst
    0: CallBinary("__gt__")
    1: CallBinaryReflected("__lt__")
    2: RaiseOrdering

CompareGtReflectedFirst
    0: CallBinaryReflected("__lt__")
    1: CallBinary("__gt__")
    2: RaiseOrdering

CompareGeNormalFirst
    0: CallBinary("__ge__")
    1: CallBinaryReflected("__le__")
    2: RaiseOrdering

CompareGeReflectedFirst
    0: CallBinaryReflected("__le__")
    1: CallBinary("__ge__")
    2: RaiseOrdering
```

Membership uses a table because missing `__contains__` has fallback behavior
that cannot be represented as another dunder-call candidate. The bytecode should
be named for the primary dunder, such as `Contains`, rather than for the surface
syntax spelling `in`; `not in` runs the same operation and negates the final
truth value.

```text
Contains
    0: CallBinary("__contains__")
    1: ContainsFallback
```

For this table, `lhs` is the container and `rhs` is the searched item.
`ContainsFallback` runs the rest of the membership protocol for those saved
operands: first iteration and equality checks, then sequence indexing fallback
where implemented. This fallback is a native continuation/thunk boundary rather
than an ordinary Python dunder-call row, because it may need to create an
iterator, repeatedly call `next`, run equality comparisons, handle sentinel
exceptions, and possibly fall back to indexed subscription.

The table records dunder names, call argument orientation, and fallback
behavior. It does not encode broad operator-family semantics beyond the steps
the dispatcher must execute. The continuation dispatcher should still recompute
current applicability and lookup results before calling later candidates,
because an arbitrary Python dunder method may have mutated classes, bases,
descriptors, or MROs before returning `NotImplemented`.

The `next_candidate_index` means "continue at this table candidate." When a
cached Python candidate at index `i` is entered, the hidden prefix stores
`i + 1`. If that candidate returns a real result, the check opcode finishes the
operation. If it returns `NotImplemented`, the cold continuation dispatcher
resumes from `i + 1`, updates the same prefix before any later Python candidate,
and returns to `CheckOperatorNotImplemented` again.

Once a Python dunder candidate has run, the continuation path is untrusted and
cold. It does not need to install cache entries or aggressively recognize
trusted native handlers while continuing. The pre-Python dispatch/cache path is
where trusted native shortcuts matter. The post-`NotImplemented` path should
prioritize correctness, current-state recomputation, and not calling an
already-executed candidate again.

## Operator Families

The shared cache structure should not hide operator-family differences behind
one broad generic operation. The generic dispatcher owns the Python protocol for
each operator family; the cache records and replays the dispatch action selected
by that family-specific protocol.

### Binary Operators

For an ordinary binary operator `lhs op rhs`, let:

- `L = type(lhs)`
- `R = type(rhs)`
- `F = dunder_lookup(L, forward_name)`
- `G = dunder_lookup(R, reflected_name)`

The candidate order is:

1. If `L != R`, `R` is a strict subclass of `L`, and `G` exists with a different
   implementation than `F`, call the reflected candidate first:
   `G(rhs, lhs)`.
2. Call the forward candidate `F(lhs, rhs)` if it exists and was not already
   skipped because it is the same implementation as the reflected candidate
   already tried.
3. If the reflected candidate was not tried first, and `L != R`, and `G` exists
   with a different implementation than `F`, call `G(rhs, lhs)`.
4. If every called candidate returns `NotImplemented`, raise the
   operator-specific unsupported-operand `TypeError`.

Returning the singleton `NotImplemented` is the only protocol-level signal to
continue to the next candidate. Any other returned value is the result. Raising
or returning `Value::exception_marker()` exits the protocol immediately through
pending-exception propagation.

The "different implementation" check is important. It prevents the dispatcher
from calling the same inherited method twice when the right operand type inherits
the reflected method from the left operand's hierarchy. The selected dispatch
table and next candidate index must ensure that a cached dunder-method-call
fallback resumes after the candidate that was actually attempted.

Ordinary binary arithmetic and bitwise operators use this table. The in-place
column applies only to augmented assignment forms such as `+=`; it is a binary
operator subcase, not a separate top-level operator family.

| Operator | Normal call | Reflected call | In-place first call |
| --- | --- | --- | --- |
| `lhs + rhs` | `lhs.__add__(rhs)` | `rhs.__radd__(lhs)` | `lhs.__iadd__(rhs)` |
| `lhs - rhs` | `lhs.__sub__(rhs)` | `rhs.__rsub__(lhs)` | `lhs.__isub__(rhs)` |
| `lhs * rhs` | `lhs.__mul__(rhs)` | `rhs.__rmul__(lhs)` | `lhs.__imul__(rhs)` |
| `lhs @ rhs` | `lhs.__matmul__(rhs)` | `rhs.__rmatmul__(lhs)` | `lhs.__imatmul__(rhs)` |
| `lhs / rhs` | `lhs.__truediv__(rhs)` | `rhs.__rtruediv__(lhs)` | `lhs.__itruediv__(rhs)` |
| `lhs // rhs` | `lhs.__floordiv__(rhs)` | `rhs.__rfloordiv__(lhs)` | `lhs.__ifloordiv__(rhs)` |
| `lhs % rhs` | `lhs.__mod__(rhs)` | `rhs.__rmod__(lhs)` | `lhs.__imod__(rhs)` |
| `divmod(lhs, rhs)` | `lhs.__divmod__(rhs)` | `rhs.__rdivmod__(lhs)` | none |
| `lhs ** rhs` | `lhs.__pow__(rhs)` | `rhs.__rpow__(lhs)` | `lhs.__ipow__(rhs)` |
| `pow(lhs, rhs, mod)` | `lhs.__pow__(rhs, mod)` | `rhs.__rpow__(lhs, mod)` | none |
| `lhs << rhs` | `lhs.__lshift__(rhs)` | `rhs.__rlshift__(lhs)` | `lhs.__ilshift__(rhs)` |
| `lhs >> rhs` | `lhs.__rshift__(rhs)` | `rhs.__rrshift__(lhs)` | `lhs.__irshift__(rhs)` |
| `lhs & rhs` | `lhs.__and__(rhs)` | `rhs.__rand__(lhs)` | `lhs.__iand__(rhs)` |
| `lhs ^ rhs` | `lhs.__xor__(rhs)` | `rhs.__rxor__(lhs)` | `lhs.__ixor__(rhs)` |
| `lhs | rhs` | `lhs.__or__(rhs)` | `rhs.__ror__(lhs)` | `lhs.__ior__(rhs)` |

In-place operators use a leading in-place candidate before the ordinary binary
protocol. This first call is unconditional with respect to the `L`/`R`
subclass-ordering rule: if the left operand type defines the relevant in-place
method, the dispatcher calls it before considering a right-hand reflected
method, even when `R` is a strict subclass of `L`.

1. Look up and call the in-place method on `lhs`, such as
   `lhs.__isub__(rhs)`.
2. If the in-place method is missing or returns `NotImplemented`, run the
   ordinary binary protocol for `lhs op rhs`, including the right-subclass
   reflected-method priority rule.
3. If the in-place method returns any other value, that value is the operation
   result. It may be `lhs`, but does not have to be.

### Ternary Operators

Ternary `pow(lhs, rhs, mod)` is special: it calls
`lhs.__pow__(rhs, mod)` with the third argument, then tries
`rhs.__rpow__(lhs, mod)` if the forward method is missing or returns
`NotImplemented`.

The ternary form does not have an in-place form in Python syntax; augmented
`**=` is a binary augmented assignment and calls `lhs.__ipow__(rhs)` before
falling back to the binary `lhs ** rhs` protocol.

### Comparison Operators

Rich comparisons use a related but separate reflected-method table. There are
no `__rlt__`-style names; each comparison method is reflected by another rich
comparison method:

| Operator | Left candidate | Right reflected candidate |
| --- | --- | --- |
| `lhs < rhs` | `lhs.__lt__(rhs)` | `rhs.__gt__(lhs)` |
| `lhs <= rhs` | `lhs.__le__(rhs)` | `rhs.__ge__(lhs)` |
| `lhs == rhs` | `lhs.__eq__(rhs)` | `rhs.__eq__(lhs)` |
| `lhs != rhs` | `lhs.__ne__(rhs)` | `rhs.__ne__(lhs)` |
| `lhs > rhs` | `lhs.__gt__(rhs)` | `rhs.__lt__(lhs)` |
| `lhs >= rhs` | `lhs.__ge__(rhs)` | `rhs.__le__(lhs)` |

Comparison candidate ordering uses the same right-subclass priority rule: if
`R` is a strict subclass of `L` and the right reflected comparison exists with a
different implementation, try it before the left comparison. Otherwise try the
left comparison first, then the right reflected comparison if it is applicable
and different. A rich comparison method may return any value; the comparison
expression returns that value directly. Boolean contexts apply normal truthiness
to the comparison result later.

If both comparison candidates return `NotImplemented`, `==` falls back to object
identity, `!=` falls back to the negation of object identity, and ordering
comparisons (`<`, `<=`, `>`, `>=`) raise `TypeError`.

### Unary Operators

Unary operators have no reflected candidate:

| Operator | Call |
| --- | --- |
| `-value` | `value.__neg__()` |
| `+value` | `value.__pos__()` |
| `~value` | `value.__invert__()` |
| `abs(value)` | `value.__abs__()` |

### Container Operators

Membership is a container protocol, not a reflected binary operator. It should
not share arithmetic reflected-method semantics, because after `__contains__`
misses it can run iteration or sequence-index fallback rather than another
binary dunder method. It can still use the table-driven continuation machinery
with protocol operands ordered as `Contains(container, item)`. For
`item in container`:

1. If `container.__contains__` exists, call
   `container.__contains__(item)` and truth-test its result.
2. Otherwise, try iteration via `container.__iter__()`. Compare `item` against
   each yielded value using equality semantics.
3. Otherwise, try the old sequence protocol via
   `container.__getitem__(index)`, starting at index `0` and incrementing until
   an index error terminates the search. Compare `item` against each returned
   value using equality semantics.

`item not in container` is the boolean negation of `item in container`; it does
not have a separate dunder method or a separate dispatch table.

## Non-Arithmetic Protocol Families

Several bytecodes and builtins invoke dunder methods without being arithmetic
operators. They should not be forced through the binary/reflected operator cache,
but the same two-level principle applies:

- cache dunder-method lookup and protocol selection only under the right
  validity guards
- skip Python-visible behavior only when a trusted handler proves that every
  skipped protocol step is stable and semantically equivalent

These families need separate dispatcher and cache shapes because their fallback
rules, exception sensitivity, and binding conventions differ from binary
operators.

### Attribute Protocol

Attribute access is not ordinary method lookup on the object. It is a protocol
rooted in the type of the object and descriptor behavior along the MRO.

| Operation | Protocol entry |
| --- | --- |
| `obj.attr` | `type(obj).__getattribute__(obj, "attr")` |
| missing-attribute fallback | `type(obj).__getattr__(obj, "attr")` |
| `obj.attr = value` | `type(obj).__setattr__(obj, "attr", value)` |
| `del obj.attr` | `type(obj).__delattr__(obj, "attr")` |

`__getattr__` is only a fallback after `__getattribute__` raises
`AttributeError`; a missing `__getattribute__` result is not the signal. Any
cache that skips from an attribute IC to a direct load must prove that the
descriptor, instance-dictionary, class-dictionary, and fallback behavior selected
by the full attribute protocol are still valid.

Descriptor methods are part of the attribute protocol and should be treated as
dispatch-affecting calls when they participate:

| Descriptor operation | Protocol call |
| --- | --- |
| read descriptor | `descriptor.__get__(obj, owner)` |
| write descriptor | `descriptor.__set__(obj, value)` |
| delete descriptor | `descriptor.__delete__(obj)` |

This means attribute ICs and dunder-method dispatch caches should share lookup
validity machinery where possible, but attribute dispatch is its own family. In
particular, caching `obj.attr` is not the same as caching
`type(obj).__getattribute__`.

### Subscription And Slicing Protocol

Subscription is a single-object protocol with no reflected candidate:

| Operation | Protocol call |
| --- | --- |
| `obj[key]` | `type(obj).__getitem__(obj, key)` |
| `obj[key] = value` | `type(obj).__setitem__(obj, key, value)` |
| `del obj[key]` | `type(obj).__delitem__(obj, key)` |

Python 3 slicing uses the same protocol with a `slice` object as the key:

| Operation | Protocol call |
| --- | --- |
| `obj[a:b:c]` | `type(obj).__getitem__(obj, slice(a, b, c))` |
| `obj[a:b:c] = value` | `type(obj).__setitem__(obj, slice(a, b, c), value)` |
| `del obj[a:b:c]` | `type(obj).__delitem__(obj, slice(a, b, c))` |

`__getslice__`, `__setslice__`, and `__delslice__` are Python 2 legacy
protocols. They should not be part of the Python 3 operator-dispatch design
unless clovervm intentionally adds a compatibility deviation.

Class-object subscription is intentionally excluded from the first slice.
Python's `Class[key]` protocol may try metaclass `__getitem__` and then
`__class_getitem__`; that is a separate class-subscription design. The first
`GetItemIC` should handle ordinary instance/container subscription only.

Get-item subscription is a good trial family for dunder-method dispatch
caching. It is high impact, but its protocol is simpler than binary arithmetic:

- there is only one protocol owner, the container
- there is no reflected candidate
- there is no right-subclass priority rule
- slicing arrives as an ordinary `slice` key

The useful cache shape is still binary:

```text
guard:
    container shape key == cached container shape key
    key shape key == cached key shape key

validity:
    method_read_cache validity proves
    dunder_lookup(type(container), "__getitem__") is still valid

action:
    trusted handler(container, key)
    or cached selected __getitem__ function call
```

For store and delete subscription, the lookup validity changes to
`dunder_lookup(type(container), "__setitem__")` or
`dunder_lookup(type(container), "__delitem__")`.

The key `ShapeKey` is a specialization guard, not a second dunder-method lookup
dependency. It lets a hot bytecode site learn whether this is `list[smi]`,
`list[slice]`, `tuple[smi]`, `str[slice]`, `dict[str]`, `dict[smi]`, or some
other local pattern. The lookup validity cell protects the container protocol
only, because subscription dispatch does not ask the key type for a reflected
dunder method.

This is better than a fixed opcode ladder such as "try list, then tuple, then
str, then dict, then generic." A global builtin order is necessarily wrong for
some hot call sites; an inline cache lets each `BINARY_SUBSCR` site specialize
to the container/key pair it actually sees.

A cache that skips the dunder method must still preserve any key-side behavior
that the trusted handler assumes away. For example, list indexing may need
`__index__`, dict lookup may need hashing and equality, and slicing requires
correct slice-object behavior. Those are trusted-handler dependencies, not
reflected subscription dispatch dependencies. Trusted get-item handlers should
therefore start with exact key shape keys where the handler does not skip
arbitrary Python key behavior, such as SMI index keys and exact `slice` keys
whose components have already been constructed.

A monomorphic get-item cache entry can therefore stay simple and scan-friendly.
The lookup arm and key guard are shared by both dunder-call replay and trusted
native handler replay:

```cpp
struct GetItemIC
{
    AttributeReadInlineCache method_read_cache;
    ShapeKey key_shape_key;

    // Trusted-handler arm. Non-null means the entry may skip the dunder method
    // after method_read_cache and key_shape_key validate.
    BinaryHandler handler = nullptr;

    // Dunder-call replay arm. This is intentionally not a full
    // FunctionCallInlineCache: method_read_cache carries the contents-sensitive
    // validity dependency that guards callable identity.
    Function *function = nullptr;
    CodeObject *code_object = nullptr;
    uint32_t n_args = 0;
    FunctionCallAdaptation adaptation = FunctionCallAdaptation::FixedArity;
    bool has_self = false;
};
```

The hit path always validates the method-read cache before deciding whether to
call the trusted handler or the cached dunder method:

```text
if !method_read_cache.matches(container):
    miss
if ShapeKey::from_value(key) != key_shape_key:
    miss
if handler != nullptr:
    return handler(thread, container, key)
if function == nullptr:
    miss

enter cached function/code object with prepared [container, key] args
```

The handler is not independent dispatch authority. It is a refinement of the
same validated dunder lookup:

```text
miss:
    dunder_lookup(type(container), "__getitem__")
    -> AttributeReadDescriptor / method-call target

    validate callable, call arity, and call adaptation

    if selected code object has a trusted handler resolver:
        ask resolver(container_shape_key, key_shape_key)

    if resolver returns handler H:
        install handler H
        execute handler H for the current operation
    else:
        prepare [container, key] call argument span
        enter selected function-shaped method
        -> real Python-visible method behavior happens

install:
    method_read_cache
    key_shape_key
    function/code/adaptation/has_self
    handler = H, if a trusted resolver returned one

hit:
    method_read_cache still valid?
        yes, the same dunder method would still be selected

    handler present?
        yes, skip the dunder call and run H
        no, replay the selected dunder method with prepared call args
```

This avoids treating a shape-key pair such as `list[smi]` as enough authority to
run `list_getitem_smi`. The cache may run that handler only because the guarded
dunder lookup still selects the same trusted method and the trusted-handler
recognizer has mapped that selected method plus the observed shape keys to an
equivalent native shortcut.

The selected builtin `__getitem__` code object may carry a
`TrustedHandlerResolver`. The miss path calls that resolver after dunder lookup
has selected the function-shaped method and ordinary call validation has
succeeded:

```text
resolution = code_object->trusted_handler_resolver(
    vm, container_shape_key, key_shape_key, ShapeKey{})

if resolution.arity == TrustedHandlerArity::Binary:
    install resolution.binary as GetItemIC::handler
    run resolution.binary for the current operation
```

The resolver must prove the direct handler's preconditions. For the current
trusted get-item handlers, that means exact builtin receiver class and exact SMI
key shape before returning a no-type-check handler. The arity check belongs on
the miss path. The hit path stores and calls only the typed `BinaryHandler`, so
it does not need a resolver, arity switch, or union access.

Current get-item status:

- `list[smi]`, `tuple[smi]`, and `str[smi]` use trusted handlers after guarded
  `__getitem__` lookup selects the corresponding builtin method.
- user-defined `__getitem__` and unsupported builtin/key combinations use the
  cached dunder-method-call replay path.
- dict, slice, store-subscript, and delete-subscript trusted handlers remain
  later extensions.

The monomorphic get-item cache should treat the container and key `ShapeKey`s as
an all-or-nothing guard. A key-shape mismatch should miss and retrace even
though the dunder lookup itself depends only on the container shape. That is
deliberate: it keeps the cache contract simple and makes replacement policy
obvious.

A later polymorphic refinement can split the two tiers:

```text
container shape key + method_plan validity:
    lookup-plan hit; the selected dunder method is still reusable

container shape key + key shape key + method_plan validity + handler:
    trusted-handler hit; the dunder call can be skipped
```

In that later design, a key `ShapeKey` miss could fall back to
`DunderMethodCall` through the still-valid `method_plan`, then update or add a
handler specialization for the newly observed key `ShapeKey`. That refinement
should wait until the monomorphic get-item cache is correct and measured.

Store and delete subscription are later extensions. `SetItemIC` and `DelItemIC`
should use the same shape and plan pattern but with their own handler typedefs
and cache arrays. `__setitem__` is a ternary call boundary (`container`, `key`,
`value`), but its initial dispatch profile can still be binary-shaped when the
trusted handler proves it does not depend on value shape key for dispatch. If a
trusted set-item handler assumes value-side behavior away, it needs either an
additional value guard or a narrower recognized case.

Multi-method protocols such as binary arithmetic, in-place arithmetic, and rich
comparison are more complicated because they may have multiple candidate methods,
`NotImplemented` continuation, reflected-method ordering, and resume state after
a cached first call. They should use the table-driven continuation shape
described above rather than trying to replay a completed multi-candidate
execution on the hot path.

### Numeric Conversion Protocols

Numeric conversion and index coercion are protocol dispatch sites, and they are
often on hot paths even when no arithmetic operator is being overloaded.

| Operation | Protocol calls |
| --- | --- |
| `int(obj)` | `type(obj).__int__(obj)`, with operation-specific fallback rules |
| `float(obj)` | `type(obj).__float__(obj)`, with operation-specific fallback rules |
| `complex(obj)` | `type(obj).__complex__(obj)`, with operation-specific fallback rules |
| index coercion | `type(obj).__index__(obj)` |
| `round(obj, ndigits)` | `type(obj).__round__(obj, ndigits)` |
| `trunc(obj)` | `type(obj).__trunc__(obj)` |
| `floor(obj)` | `type(obj).__floor__(obj)` |
| `ceil(obj)` | `type(obj).__ceil__(obj)` |

The exact fallback order for `int`, `float`, and `complex` should be specified
from the Python data model before implementation. Do not infer it from binary
operator fallback rules: returning `NotImplemented` is not generally the
multi-candidate protocol signal for conversions, and conversion methods have
type restrictions on successful results.

`__index__` deserves particular attention because it is used implicitly by
indexing, slicing, ranges, repetition counts, and some builtin numeric
operations. A fast path that accepts SMI directly should remain ahead of this
dispatch layer, but heap ints, bools, and user objects that define `__index__`
need a cacheable protocol path.

### Truth, Length, And Size Protocols

Truthiness and length are also dunder-method dispatch:

| Operation | Protocol calls |
| --- | --- |
| `if obj`, `while obj`, `not obj` | `type(obj).__bool__(obj)`, then `type(obj).__len__(obj)` fallback |
| `len(obj)` | `type(obj).__len__(obj)` |
| `operator.length_hint(obj)` | `type(obj).__length_hint__(obj)` where applicable |

Jump opcodes should preserve their existing direct inline-value truthiness path.
The cacheable continuation is for heap objects and other values that require
protocol dispatch. The `__bool__`/`__len__` fallback is exception-sensitive and
result-type-sensitive: `__bool__` must produce a boolean-compatible result, and
`__len__` must produce a non-negative integer length.

### Call, Iteration, And Representation Protocols

These protocols are not part of the first subscription target, but they should
be called out so they are not accidentally treated as ordinary attribute calls:

| Operation | Protocol call |
| --- | --- |
| `obj(*args, **kwargs)` | `type(obj).__call__(obj, *args, **kwargs)` |
| `iter(obj)` | `type(obj).__iter__(obj)`, with sequence fallback where implemented |
| `next(iterator)` | `type(iterator).__next__(iterator)` |
| `reversed(obj)` | `type(obj).__reversed__(obj)`, with sequence fallback where implemented |
| `repr(obj)` | `type(obj).__repr__(obj)` |
| `str(obj)` | `type(obj).__str__(obj)`, with fallback behavior |
| `format(obj, spec)` | `type(obj).__format__(obj, spec)` |
| `bytes(obj)` | `type(obj).__bytes__(obj)` |

Call dispatch probably belongs with the existing function-call IC machinery, not
with binary operator caches. Iteration has sentinel exceptions and fallback
protocols. Representation and formatting are normal builtins most of the time,
but their dunder-method lookup should still use the same dependency and
binding discipline when optimized.

## Installation Rule

The opcode miss path owns cache installation. For single-candidate protocols it
uses the selected method's resolver and operand shape keys. For
multi-candidate protocols it walks the relevant operator dispatch table and may
install either a complete trusted handler for the current table state or a
single cacheable Python-candidate call with continuation metadata.

Dunder-method-call cache state may be installed once lookup, binding, callable
validation, call-argument layout, and call adaptation have produced an
executable call plan. The cache stores how to replay the selected call, not the
method result. If the method body later raises, the cache may still be valid:
the next hit must execute the method again and propagate whatever happens then.

For multi-candidate operators, the replayable Python-call entry should be
limited to a single candidate. The table walker may enter a cacheable Python
candidate with enough pending cache state to replay that same call later. The
entry should remain installed only if the candidate returns a
non-`NotImplemented` result. The entry records the semantic dispatch table
reference and the next candidate index to store in the hidden continuation
prefix before entering the Python function. If a later cache hit calls the same
candidate and it returns `NotImplemented`, the check opcode uses that table
reference and next index to continue the protocol without calling the candidate
again.

Trusted-handler installation is stricter. For single-candidate protocols such
as get-item subscription, a resolver may return a handler before calling the
dunder method, but only if the resolver fully proves the handler's preconditions
from the selected method identity and operand shape keys. The miss path then
installs and executes that handler immediately. For continuation protocols such
as binary or in-place operators, a handler that skips later candidate behavior
may be installed only when the table walker proves that all skipped or consumed
steps are trusted for the current operand shape keys. Exception paths must not
accidentally install a direct handler.

The installation order for multi-candidate operators should prefer complete
trusted native handlers when the current table position can be collapsed to a
trusted and stable result for the operand shape keys.
If no trusted handler is valid, the miss path may install the single Python
candidate that produced the result, together with its table reference and next
candidate index. It should not leave a Python-candidate entry installed for a
candidate that returned `NotImplemented` on the miss, because that would make
the common hit immediately enter the cold continuation path.

The first successful installation choice for a recognized get-item operation is:

```text
if resolver != nullptr &&
   resolution.arity != TrustedHandlerArity::None:
    install TrustedHandler
    execute TrustedHandler for the current miss
else if lookup descriptor has a cacheable DunderMethodDispatchPlan:
    install DunderMethodCall
    enter selected dunder method for the current miss
else:
    leave empty or install an explicit miss/unhandled entry
```

`resolver != nullptr` with `TrustedHandlerArity::None` is not a failed
dispatch. It means the selected method is trusted, but the VM did not get, or
did not want, a direct replacement for this operand shape key. In that case the
`DunderMethodCall` entry is still useful because it skips dunder-method lookup
while preserving the method call.

On every miss, the opcode miss path may install a replacement entry in the code
object IC for that bytecode's cache index. Replacement can be simple in the
monomorphic design: one entry per cache index, overwritten by the latest
cacheable miss result. A miss or uncacheable entry is allowed when useful to
record that the site has been observed and to keep future profiling behavior
explicit. Entry limits, polymorphic chains, and megamorphic fallback are later
policy work.

## Core Invariant

If the operand guards and all dispatch lookup validity cells still match, the
generic dispatcher would select the same first dispatch action. The cache may
execute that first action directly.

The cache may skip later dispatch actions only when every skipped
dispatch-affecting action is trusted and operand-shape-stable.

Equivalently:

> Inline caches may specialize operator dispatch facts. They must not specialize
> arbitrary Python method behavior unless the VM has deoptimization machinery or
> intentionally chooses non-Python semantics.

## Open Questions

- Should trusted handlers require exact builtin type guards, or can
  some use shape guards that imply the same dunder-method lookup semantics?
- How much of the binary cache validation path can be shared with in-place
  operator caches without obscuring the interpreter hot path?
