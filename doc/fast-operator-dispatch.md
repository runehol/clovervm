# Fast Operator Dispatch

This note sketches inline caching for Python binary and in-place operator
dispatch. The goal is to speed up overloaded operators without caching arbitrary
Python method behavior.

Python presents binary operators as method calls, but the VM should separate two
questions:

- Which dispatch action would Python's operator protocol select?
- What operation does the selected action perform?

Inline caches may specialize the first question. They may only specialize the
second question when every dispatch-affecting method skipped by the cache is
trusted for that shortcut.

## Goals

- Avoid repeated special-method lookup at hot operator bytecodes.
- Let builtin operator combinations jump directly to native handlers after the
  generic dispatcher proves the combination.
- Preserve Python semantics for arbitrary `__add__`, `__radd__`, `__iadd__`,
  and related methods.
- Make cache validity depend on special-method lookup dependencies, not merely
  on the class where a method happened to be found.
- Keep the cache model simple enough that the interpreter hot path can validate
  it without reimplementing the generic operator protocol.

## Non-Goals

- Do not cache arbitrary Python method results.
- Do not skip an arbitrary Python method just because it returned
  `NotImplemented` on an earlier execution.
- Do not add deoptimization machinery as part of the first design.
- Do not store per-object operator metadata when the operation can be selected
  from the opcode, operand shapes/types, and dispatch result.
- Do not replace the existing direct inline-value arithmetic fast paths. SMI
  and SMI-or-bool cases that are already handled by the opcode stay ahead of
  this dispatch layer.
- Do not build a polymorphic inline cache in the first implementation. The code
  object cache should be structured so PIC entries can be added later, but a
  monomorphic entry plus miss replacement is enough for the first slice.

## Hot-Path Boundary

Fast operator dispatch starts after the opcode's direct inline-value path has
failed. For `+`, `-`, and `*`, the interpreter should continue to check and
handle SMI-plus-SMI inline before probing an operator cache. Operations whose
current fast path treats booleans as integers should keep that direct
SMI-or-bool behavior ahead of the cache as well.

The first failed tag/shape test is cheap enough to pay on every execution. The
operator cache should therefore live on the non-SMI or otherwise non-direct
continuation, not in front of the primary arithmetic path. This keeps the common
integer path as a tag check plus checked arithmetic while still giving heap
objects, floats, heap ints, strings, lists, and overloaded user objects a
cacheable dispatch route.

`Div` and other operators that do not have a pure SMI-result fast path may enter
the operator-cache continuation earlier, but only after preserving any existing
operator-specific numeric shortcuts that are intentionally faster than generic
special-method dispatch.

## Operand Shapes

The cache guard vocabulary is the VM's shape model, including shapes for inline
values. Inline-value shapes may be more expensive to compute than heap-object
shape pointers, but they let operator dispatch use one profiling vocabulary
across:

- SMI
- bool
- float
- immutable builtin heap values such as string, tuple, and list where relevant
- user instance shapes
- class object shapes
- heap ints

The cache should store the operand shapes that the generic operator dispatcher
used for dispatch. It should not store an ad hoc mixture of raw tag checks,
native-layout tests, and class checks unless those tests are wrapped as stable
shape identities. Exact builtin-type guards can still be represented by builtin
shape identities when that is the semantic dependency.

## Dispatch Trace

On a cache miss, the opcode runs the normal generic operator dispatcher. While
doing so, the dispatcher records the dispatch facts it discovered:

- the operation being performed
- the operand shapes used by the dispatch decision
- the forward special-method lookup result
- the reflected special-method lookup result
- the actual candidate order chosen by the protocol
- which candidate was first called
- whether skipped candidates are trusted for a direct handler shortcut
- the lookup validity cells that guard each special-method lookup result

Operator dunder methods use Python's special-method lookup path. This is more
conservative than ordinary attribute lookup: it looks through the operand's type
and MRO and does not consult attributes stored directly on the operand object.
That is useful for caching because the dispatch dependency is a type/MRO lookup
dependency, not an object-attribute dependency. Special-method lookup still has
to preserve the descriptor behavior of the class/MRO entry it finds; it should
not be treated as a raw dictionary lookup unless clovervm intentionally chooses
and documents a Python deviation.

For ordinary binary operators there are normally two lookup dependencies:

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

The validity cells must cover the full special-method lookup dependency,
including negative lookups. They must be invalidated by mutations anywhere along
the relevant MRO path, base/MRO changes, and descriptor-relevant class-entry
changes.

The existing `ClassObject::current_mro_shape_and_contents_validity_cell()` /
`get_or_create_mro_shape_and_contents_validity_cell()` machinery is the expected
primitive for this. It protects lookups rooted in a class's materialized MRO and
is invalidated when the root class or any class on the MRO path changes shape or
class-member contents. That is broader than a per-name lookup cell, but it covers
positive and negative special-method lookup results for operator dispatch.
Unrelated class-member writes may invalidate operator caches; that is an
acceptable tradeoff for the first design because code that mutates classes on a
hot operator path can pay the cache-miss cost.

## Cache Kinds

The first design should use two binary cache actions with a shared validation
shape.

```cpp
enum class BinaryOperatorCacheKind : uint8_t
{
    Empty,
    TrustedHandler,
    PythonFirstCall,
};
```

Both binary cache kinds should guard:

- lhs operand shape
- rhs operand shape
- lhs forward-op lookup validity
- rhs reflected-op lookup validity

This intentionally over-guards some cases. The useful property is that a cache
hit proves the generic dispatcher would select the same first dispatch action
under the current operand relationship and lookup results.

The two-operand guard is required even for a Python-call cache. Reflected-method
ordering can depend on both operands, especially when the right operand type is
a proper subclass of the left operand type and defines a distinct reflected
method.

Operator inline caches live on the code object, like the existing attribute,
module-global, and function-call caches. Every binary opcode form that can
leave the direct inline-value fast path and invoke Python special-method
dispatch should carry a cache index operand. Pure direct opcodes or specialized
opcodes that cannot call dunder methods do not need an operator-cache index.

On a miss, the generic dispatcher runs and the opcode miss path installs a new
entry for that cache index. The entry may represent a successful trusted handler,
a successful Python first-call plan, or a miss/unhandled plan when that is useful
for profiling and replacement. The first implementation may replace the single
entry on every miss. A bounded polymorphic cache and megamorphic state are later
extensions, not part of this design slice.

## Special Method Lookup Result

The cache must store a special-method lookup result, not an ordinary attribute
value. Special-method lookup bypasses the instance dictionary but still has
descriptor and binding semantics for the class/MRO entry it finds.

A cacheable `SpecialMethodLookupResult` should represent at least:

- whether the lookup was found or negative
- the resolved value or callable identity, when found
- the owner/type and candidate role this result belongs to
- the binding convention needed to reproduce the call exactly
- the lookup validity cell covering the positive or negative result
- optional VM-owned slot identity or trusted-method metadata

The exact C++ type is an implementation detail, but cache hits must be able to
recreate the same first special-method call the generic dispatcher selected
without falling back to ordinary attribute lookup.

## Trusted Handler Cache

A trusted handler cache is a direct operator shortcut. It may replace the rest
of the operator protocol with a handler only when every dispatch-affecting
method execution skipped by the cache is trusted for that handler.

Trust is a cache permission, not an implementation-language property. A C++
method is not automatically trusted, and a Python-implemented method is not
automatically untrusted. The relevant question is whether the VM may rely on
that method's dispatch-visible behavior being stable under the installed guards.

```cpp
using BinaryHandler = Value (*)(ThreadState *, Value lhs, Value rhs);
```

The cached handler is not the Python method object. It is a VM-selected native
implementation for a fully proven operator case:

```text
(op, lhs operand shape, rhs operand shape, winner) -> BinaryHandler
```

Examples:

```text
str + str       -> string_concat_fast
float + float   -> float_add_float_fast
float + smi     -> float_add_smi_fast
smi + float     -> smi_add_float_fast
```

A recognized cache candidate can have a shape such as:

```cpp
enum class OperatorCandidateRole : uint8_t
{
    None,
    LhsForward,
    RhsReflected,
    LhsInPlace,
};

struct BinaryDispatchCacheCandidate
{
    BinaryOperatorCacheKind kind;
    OperatorCandidateRole first_candidate;
    BinaryHandler handler;
    SpecialMethodLookupResult first_lookup;
    LookupValidity left_validity;
    LookupValidity right_validity;
};
```

The opcode miss path installs a candidate only if the operation completed
successfully and the completed trace is recognized as cacheable.

Trusted-handler selection should not be a parallel `__trusted_add__`-style
protocol. That would duplicate Python's operator precedence rules and create a
second path that has to stay semantically aligned with generic dispatch.

Instead, the generic dispatcher should run the normal protocol and record the
trace. After a successful operation, a separate recognizer can inspect the trace:

- operand shapes/types
- lookup results and validity cells
- candidate order chosen by the generic dispatcher
- which candidates were called
- whether called candidates returned `NotImplemented` or produced the result
- callable or slot identities for each dispatch-affecting candidate

Only then should the recognizer map the completed trace to a `TrustedHandler`.
This keeps precedence in one place: the generic dispatcher. The trusted-handler
table merely says which already-observed traces may be replaced by a direct
handler on later executions under the same guards.

## Slot Metadata

CPython-style special-method slot tables are a natural place to declare whether
a resolved special method is trusted for operator-cache purposes. The slot table
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
`NotImplemented` candidates earlier in the trace. The slot table should declare
trust and provide stable slot identity. A separate trace recognizer should map:

```text
(op, operand shapes/types, candidate order, trusted slot identities, winner)
    -> TrustedHandler
```

This avoids turning slot lookup into a second operator protocol while still
giving the recognizer a compact, VM-owned representation of the methods that
generic dispatch actually found.

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

## Trusted Handler Publication

Some trusted method implementations may only know the best direct handler after
they have executed their normal operation. They still have to return a single
`Value`, so the trusted publication can be communicated through a
`ThreadState` side channel if the side channel is treated as a one-call return
slot, not as ambient dispatch state.

The publication record should represent both trust and any direct handler found
by the call:

```cpp
struct TrustedOperatorPublication
{
    bool trusted;
    BinaryHandler handler;
};
```

The `ThreadState` API should make the one-shot nature explicit. Candidate names:

```cpp
void publish_operator_trustedness(TrustedOperatorPublication publication);
TrustedOperatorPublication consume_operator_trustedness();
void clear_operator_trustedness();
```

`consume_operator_trustedness()` is a destructive read: it returns the current
publication and clears the thread-local slot. There should be no ordinary `get`
or `peek` API for this state; callers must consume it into the current dispatch
trace step or discard it. `clear_operator_trustedness()` exists for paths that
intentionally discard the publication, such as exception paths where no cache
candidate can be installed.

The same record shape is used whether the method returns a real result or
`NotImplemented`. A trusted method that returns `NotImplemented` should publish
`trusted = true` with no handler, so the dispatcher can record that this
dispatch-affecting step was trusted even though the protocol must continue. A
trusted method that handles the operation may also publish the direct handler
that can be used by a later cache hit.

The publication does not need to identify which special method was called. The
operator dispatcher already knows whether the current candidate is the lhs
forward method, rhs reflected method, or lhs in-place method, and it records the
lookup result and validity cell in the trace. The publication only describes
the call that just returned.

The required protocol is:

- clear the thread-local trusted publication before invoking an operator
  method that may publish one
- the method may publish a trusted handler only after it has successfully
  handled the operation
- immediately after the call returns, the operator dispatcher reads and clears
  the publication
- no operator dispatch may run between the method return and that read/clear
- the dispatcher installs the candidate only if the overall operator dispatch
  completed successfully and the completed trace is trusted-handler-cacheable
- no code may leave a publication in `ThreadState` for a later, unrelated
  dispatch to discover

This consume-and-clear step is required after every attempted internal operator
dunder call, not only after calls expected to be trusted and not only after
calls that produce the final result. A partially trusted chain may publish a
record before returning `NotImplemented`, raising, or letting the protocol
continue. The dispatcher must consume that record into the current trace step or
discard it before any later candidate is considered.

Trusted implementations may perform nested operator dispatch internally. The
publication rule is only about the outgoing edge after the publication is made:
a trusted publication must not remain live across nested operator dispatch.
Implementations that need to dispatch internally should do that work first, let
the nested dispatchers consume their own publications, and then publish the
outer trusted record immediately before returning to the outer operator
dispatcher. The entry edge and the body before publication do not need to be
operator-dispatch-free.

If an implementation represents `NotImplemented` propagation with an internal
exception-like path, the trusted publication still has to be installed before
that path leaves the method and consumed by the dispatcher that catches it. For
normal Python binary-operator semantics, `NotImplemented` is a return value, not
`NotImplementedError`.

This handles reentrancy because nested operator dispatches use the same
clear-call-read-clear protocol. A nested dispatch may publish, consume, and
clear its own publication while the outer method is running. When control returns
to the outer dispatcher, the only publication it may consume is one published by
the outer method call after any nested dispatch completed.

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
- it may run internal operator dispatch if that dispatch is part of the trusted
  operation's normal semantics; those inner dispatches keep their own cache and
  publication behavior

## Python First-Call Cache

For arbitrary Python-defined special methods, the cache may skip lookup but not
method behavior. It may call only the first method that the generic dispatcher
would call under the guarded dispatch facts.

The action is:

```text
call cached first special method
if result is not NotImplemented:
    return result
otherwise:
    resume generic operator dispatch after the candidate just executed
```

The fallback must not call the first candidate again. The cache has already
executed that Python method, and running the full dispatcher from the beginning
would duplicate its side effects. Instead, the cache entry must contain enough
resume information to continue the operator protocol from the next candidate:
for example, whether the cached first call was the lhs forward method, rhs
reflected method, or lhs in-place method.

The resumed dispatcher must re-check any dispatch facts that may have been
invalidated by the first Python call before it executes later candidates. The
first arbitrary Python method may have mutated globals, object state, classes,
descriptors, bases, or MROs before returning `NotImplemented`. After such a
mutation, the cached first-candidate fact may no longer imply that the second
candidate found on the original miss is still the correct next action. The
resume path therefore continues the protocol semantically from "the first
candidate has already returned `NotImplemented`" rather than blindly invoking a
cached second method.

The cache should store a precise special-method lookup result, not just an
ordinary attribute value. Special-method lookup bypasses instance dictionaries
and has descriptor/binding behavior tied to the type lookup. A cacheable result
therefore needs enough information to reproduce the first call exactly, such as
the callable, binding convention, owner/type information, or a dedicated
`SpecialMethodLookupResult` object.

## Operator Families

The shared cache structure should not hide operator-family differences behind
one broad generic operation. The generic dispatcher owns the Python protocol for
each operator family; the cache records and replays the dispatch action selected
by that family-specific protocol.

### Binary Operators

For an ordinary binary operator `lhs op rhs`, let:

- `L = type(lhs)`
- `R = type(rhs)`
- `F = special_lookup(L, forward_name)`
- `G = special_lookup(R, reflected_name)`

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
the reflected method from the left operand's hierarchy. The dispatch trace and
cache resume state must record which candidate role was actually attempted so a
cached Python first-call fallback resumes at the correct next candidate.

Ordinary binary arithmetic and bitwise operators use this table. The in-place
column applies only to augmented assignment forms such as `+=`; it is a binary
operator subcase, not a separate top-level operator family.

| Operator | Forward call | Reflected call | In-place first call |
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
| `pow(lhs, rhs, mod)` | `lhs.__pow__(rhs, mod)` | none | none |
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
`lhs.__pow__(rhs, mod)` with the third argument but does not try `__rpow__` if
the forward method is missing or returns `NotImplemented`.

The ternary form therefore does not use the ordinary binary reflected-candidate
order. It has no in-place form in Python syntax; augmented `**=` is a binary
augmented assignment and calls `lhs.__ipow__(rhs)` before falling back to the
binary `lhs ** rhs` protocol.

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
not share the binary/reflected operator cache directly, because after
`__contains__` misses it can run iteration or sequence-index fallback rather
than another binary special method. For `item in container`:

1. If `container.__contains__` exists, call
   `container.__contains__(item)` and truth-test its result.
2. Otherwise, try iteration via `container.__iter__()`. Compare `item` against
   each yielded value using equality semantics.
3. Otherwise, try the old sequence protocol via
   `container.__getitem__(index)`, starting at index `0` and incrementing until
   an index error terminates the search. Compare `item` against each returned
   value using equality semantics.

`item not in container` is the boolean negation of `item in container`; it does
not have a separate special method.

## Installation Rule

The generic dispatcher records a dispatch trace and may receive per-call trusted
publications through `ThreadState`. The opcode miss path owns cache
installation.

Cache installation should occur only after successful semantic completion of
the operation. Exception-path caching is a separate design and should not happen
as an accidental side effect of a partial dispatch trace.

On every miss, the opcode miss path may install a replacement entry in the code
object IC for that bytecode's cache index. Replacement can be simple in the
first implementation: one entry per cache index, overwritten by the latest
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
  some use shape guards that imply the same special-method lookup semantics?
- What exact shape should the trace recognizer and trusted-handler table use?
- How much of the binary cache validation path can be shared with in-place
  operator caches without obscuring the interpreter hot path?
- How should the `ThreadState` trusted-publication side channel be hardened?
  The likely answer is an RAII/scoped publication guard or debug-only invariant
  checks that prove no publication survives past the call edge that produced it.
