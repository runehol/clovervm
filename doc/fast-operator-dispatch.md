# Fast Operator Dispatch

This document describes inline caching for Python special-method dispatch in
clovervm. The design has two targets:

- single-call protocols such as subscription, unary operators, length, and
  representation;
- `NotImplemented`-continued protocols such as binary arithmetic, in-place
  arithmetic, rich comparison, and ternary `pow`;
- table-driven fallback protocols such as membership.

The cache must preserve Python-visible dispatch semantics. A cache hit may
avoid repeated lookup and may jump to a trusted native handler, but only under
guards that prove the generic protocol would still select the same action.

## Core Invariant

A non-empty operator cache entry must prove:

```text
operand shape guards pass
dunder lookup validity cells are still valid
the cached action is still the first protocol action that should run
```

The cache may skip later protocol actions only when every skipped or consumed
action is trusted for the installed shortcut. Arbitrary Python method results
are never cached.

For `NotImplemented`-continued protocols, a cached Python call may only cache
one selected candidate. If that call returns a real result, the operation is
done. If it returns `NotImplemented`, a continuation opcode resumes the full
protocol after the candidate that already ran.

## Goals

- Avoid repeated dunder-method lookup at hot operator and protocol bytecodes.
- Let proven builtin protocol combinations jump directly to native handlers.
- Preserve arbitrary Python overload behavior, including `NotImplemented`
  fallback and class mutation during a dunder call.
- Make cache validity depend on dunder-method lookup dependencies, not merely
  the class where a method happened to be found.
- Keep interpreter hot paths explicit enough for `musttail` opcode handlers.

## Non-Goals

- Do not cache arbitrary Python method results.
- Do not skip arbitrary Python methods just because they previously returned
  `NotImplemented`.
- Do not add deoptimization machinery.
- Do not put the operator cache in front of existing direct inline-value fast
  paths such as SMI arithmetic.
- Do not build polymorphic inline caches in the first implementation. Cache
  entries should be shaped so bounded PICs can be added later.
- Do not solve class construction, class subscription, context managers, or
  arbitrary descriptor replay as part of the first operator-cache step.

## Hot-Path Boundary

Direct inline primitive paths stay ahead of operator caches. For example,
`AddSMI` should handle SMI-plus-SMI before probing any operator cache. The
operator cache starts once the opcode knows it needs heap-object or protocol
dispatch.

Trusted native handlers are refinements of Python dispatch. A native handler
may run only after guarded special-method lookup proves that the visible Python
protocol would select the trusted method. This avoids a global builtin ladder
such as "try list, then tuple, then str, then dict, then generic."

## Shape Keys

Cache guards use `ShapeKey`, an opaque comparable representation of operand
shape:

- pointer values use their object shape pointer;
- inline values use their value tag bits.

Shape keys give one profiling vocabulary for SMIs, bools, floats, heap ints,
builtin heap objects, class objects, and user instances. Cache entries should
store shape keys used by the protocol dispatcher, not an ad hoc mixture of raw
tag tests and native-layout checks. Exact builtin guards may still be encoded
as builtin shape keys when that is the semantic dependency.

## Dunder Lookup Dependencies

Special-method lookup is rooted in the operand type and its MRO. It does not
consult attributes stored directly on the operand object. It still has
descriptor and binding behavior for the class/MRO entry it finds.

Examples:

```text
subscription: lookup(type(container), "__getitem__")
binary add:   lookup(type(lhs), "__add__")
              lookup(type(rhs), "__radd__")
in-place add: lookup(type(lhs), "__iadd__")
              lookup(type(lhs), "__add__")
              lookup(type(rhs), "__radd__")
membership:   lookup(type(container), "__contains__")
```

Validity cells must cover positive and negative dunder lookup results. They
must be invalidated by mutations anywhere along the relevant MRO path, base/MRO
changes, and descriptor-relevant class-entry changes. The contents-sensitive
MRO validity machinery is the expected primitive. Unrelated class writes may
invalidate hot operator caches in the first design; code that mutates classes
on hot paths can pay the miss cost.

## Cache Shape

The concrete cache shape should generalize the current `OperatorInlineCache`,
not introduce a separate cache-kind enum. The cache arm is structural:

- an empty `method_read_cache` means the entry is empty or unusable;
- a non-null `handler` means the cache may run a trusted native handler after
  the lookup and shape guards match;
- a non-null `function` means the cache may replay the selected
  function-shaped dunder call.

Current shape:

```cpp
union OperatorMethodHandler
{
    UnaryHandler unary;
    BinaryHandler binary;
    TernaryHandler ternary;
};

struct OperatorInlineCache
{
    AttributeReadInlineCache method_read_cache;
    ShapeKey arg_shape_key;
    OperatorMethodHandler handler;
    Function *function;
    CodeObject *code_object;
    uint32_t n_args;
    FunctionCallAdaptation adaptation;
    bool has_self;
};
```

`method_read_cache` stores the special-method lookup dependency and currently
also carries the receiver shape through `AttributeReadInlineCache`. For
subscription that receiver is the container. For binary operators it is the
operand whose dunder method is being called, which may be the source left
operand or the source right operand.

`arg_shape_key` is the profiled shape of the other value argument in the cached
dunder call layout. For get-item this is the key. For ordinary binary
operators it is the non-receiver operand. For a reflected binary call, that
means the receiver is the source right operand and `arg_shape_key` is the
source left operand.

This is the only value-shape-key area that should change for the next operator
step. If embedding the receiver shape inside `method_read_cache` is too
indirect for binary operator validation, add an explicit receiver/protocol-owner
`ShapeKey`; do not introduce a broad cache-kind enum just to recover that
information.

For cached Python operator calls, the cache also needs immutable orientation
metadata for the primary opcode path, such as a `reflected_call` bit. That bit
does not encode Python fallback state. It only tells the primary opcode how to
map source operands into the cached call layout:

```text
normal binary call:    receiver = lhs, arg = rhs
reflected binary call: receiver = rhs, arg = lhs
```

The same orientation bit lets the opcode validate the right source operand
shapes and write the semantic operands into the caller-frame continuation
prefix in source/protocol order. Live continuation state still does not live in
the cache. The opcode derives the next candidate index from immutable replay
metadata and writes it, along with the saved operands, into the caller frame
prefix immediately before entering the Python function.

The intended execution tiers are:

```text
direct inline primitive path
trusted native handler cache
cached single Python dunder candidate with continuation
generic slow dispatch
```

## Dunder Lookup And Call Plans

The miss path records special-method lookup facts, not ordinary attribute
values. Split miss-time discovery from the replayable cache payload:

```text
DunderMethodLookupDescriptor = what lookup discovered on this miss
DunderMethodDispatchPlan     = the cacheable lookup/call replay payload
```

A descriptor should represent:

- found, missing, uncacheable, or errored lookup status;
- the receiver class whose MRO was searched;
- the resolved value or callable identity when found;
- the lookup validity cell;
- binding shape for function-shaped calls;
- cache blockers when the result cannot be replayed.

The first cacheable Python-call implementation should require a normal
function-shaped call plan: fixed positional arity, predictable receiver binding,
and no descriptor or call adaptation that changes argument layout in a
candidate-specific way. Odd cases such as staticmethod-shaped operator methods,
varargs/default adaptation, or descriptor dispatch stay on the generic slow
path until explicitly designed.

Before entering a cached Python function, the opcode lays out a normal
contiguous argument span. Cached dunder-call replay cannot depend on transient
opcode register conventions such as "receiver in a register and rhs in the
accumulator."

## Trusted Handlers

A trusted handler is a VM-selected native shortcut for a fully proven protocol
case:

```text
(operation, operand shape keys, selected method identity) -> typed handler
```

Trust is a cache permission, not an implementation-language property. A C++
method is not automatically trusted, and a Python method is not automatically
untrusted. The relevant question is whether the VM may rely on the method's
dispatch-visible behavior under the installed guards.

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

The selected builtin code object or VM method metadata may carry a resolver.
The miss path calls the resolver only after special-method lookup and ordinary
call validation have selected the method. The resolver must prove the native
handler's preconditions from the selected method plus operand shape keys.

## Continuation Opcode

`NotImplemented`-continued protocols need a way to call one Python dunder
method, inspect whether it returned `NotImplemented`, and then continue the
protocol without calling that candidate again. This applies to binary
arithmetic, in-place arithmetic, ternary `pow`, and rich comparison. It does
not apply to membership: `__contains__` returning `NotImplemented` is a result
to truth-test, not a signal to try iteration fallback.

The bytecode shape is a paired operator opcode and a single-byte check opcode:

```text
AddSmi #5, cache_idx ...
CheckOperatorNotImplemented
```

Direct inline and trusted-handler paths skip both the Python call and the check
opcode. They produce the result in the accumulator and advance past the full
logical instruction.

For static bytecode layout, the operator opcode owns the following
`CheckOperatorNotImplemented` byte. The assembler should account for that byte
as part of the operator instruction length, and the disassembler should advance
past it when printing the operator instruction. In ordinary disassembly, the
check byte is therefore not printed as a separate instruction:

```text
AddSmi #5, operator_ic[0]
```

`CheckOperatorNotImplemented` is printed only by runtime instruction tracing
when the interpreter actually encounters it as a return target from a Python
dunder call. Ordinary control flow must not target the check byte directly.

The code object builder should enforce this shape through operator-specific
emit helpers. For example, `emit_add_smi(...)` should allocate or accept the
operator cache index, emit the `AddSmi` operands, and then immediately append
the `CheckOperatorNotImplemented` byte:

```text
emit_add_smi(source_offset, imm, cache_idx):
    AddSmi imm, cache_idx
    CheckOperatorNotImplemented
```

Callers should not emit `CheckOperatorNotImplemented` directly in normal
codegen. The paired byte is part of the logical `AddSmi` instruction for code
object construction, source-offset accounting, static bytecode walking, and
ordinary disassembly.

If an operator opcode enters a cached Python candidate, it stores a hidden
continuation prefix in the caller frame before the callee-visible arguments:

```text
hidden prefix:
    operator dispatch table pointer or SMI table id
    SMI next_candidate_index
    lhs Value
    rhs Value
    optional third Value for ternary tables
    padding/alignment if needed

actual Python call arguments:
    candidate-specific argument layout
```

The hidden prefix is not visible to the callee. The operand slots are
root-scanned. The table field is immutable VM metadata or an SMI id, not an
ordinary heap object. Call argument slots must not be reused as continuation
operands because call setup or the callee may mutate them.

`CheckOperatorNotImplemented` has no immediates. It is only a valid return
target after an operator-candidate call that populated the hidden prefix:

```text
if accumulator is not NotImplemented:
    finish the logical operator instruction
else:
    read table, next index, and saved operands from the prefix
    continue table-driven dispatch from next index
```

Debug builds should assert the frame contract hard: valid table metadata, SMI
candidate index, in-range candidate index, and root-scannable saved operands.

The repeated `NotImplemented` continuation path is cold. It may rebuild call
arguments and re-enter later Python candidates with the same return address.
Correctness and current-state recomputation matter more there than caching.
Once execution has reached `CheckOperatorNotImplemented`, it must not update
the inline cache. It runs the remaining protocol to conclusion using the saved
frame prefix and current lookups only.

## Dispatch Tables

The continuation prefix stores a semantic dispatch table, not an opcode. Tables
are immutable VM metadata. They may contain immortal interned strings for dunder
names; do not store ordinary collectable heap values in them.

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

`OperatorStepAction` describes what frame setup or fallback operation to run.
It should be call-layout oriented, not operator-family oriented. If arithmetic,
comparison, and in-place candidates use the same lookup root and argument
layout, they should use the same action.

Useful initial actions:

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

`OperatorStepApplicability` answers whether the step is currently eligible.
This keeps method elision and subtype-priority checks out of frame setup.

Useful initial applicability cases:

```text
Always                  fallback step; no method lookup
IfMethodFound           call only if the named method resolves
IfMethodFoundDistinct   call only if the named method resolves and is not a
                        duplicate of a candidate the protocol has already tried
```

Table operands are semantic protocol operands, not necessarily source operands.
For example, `a in b` should compile to `Contains` with `lhs = b` and
`rhs = a`, because the primary dunder name is `__contains__` and the protocol
receiver is the container.

## Table Inventory

The inventory below covers protocols that need multiple candidates or protocol
fallback rows. Single-call protocols such as unary operators, subscription,
length, representation, and ordinary conversions do not need dispatch tables
until they grow fallback behavior that cannot be represented as one cached
dunder call.

### Binary Arithmetic And Bitwise Operators

Each ordinary binary operator has two table orderings. The normal-first table
tries the left/normal dunder before the reflected dunder. The reflected-first
table is selected when right-subclass priority says the reflected candidate
must be attempted first.

Template:

```text
Binary<Verb>NormalFirst
    0: CallBinary("<normal>")
    1: CallBinaryReflected("<reflected>")
    2: RaiseUnsupported

Binary<Verb>ReflectedFirst
    0: CallBinaryReflected("<reflected>")
    1: CallBinary("<normal>")
    2: RaiseUnsupported
```

Rows:

| Verb | Normal dunder | Reflected dunder |
| --- | --- | --- |
| `Add` | `__add__` | `__radd__` |
| `Sub` | `__sub__` | `__rsub__` |
| `Mul` | `__mul__` | `__rmul__` |
| `Matmul` | `__matmul__` | `__rmatmul__` |
| `TrueDiv` | `__truediv__` | `__rtruediv__` |
| `FloorDiv` | `__floordiv__` | `__rfloordiv__` |
| `Mod` | `__mod__` | `__rmod__` |
| `Divmod` | `__divmod__` | `__rdivmod__` |
| `Pow` | `__pow__` | `__rpow__` |
| `LShift` | `__lshift__` | `__rlshift__` |
| `RShift` | `__rshift__` | `__rrshift__` |
| `And` | `__and__` | `__rand__` |
| `Xor` | `__xor__` | `__rxor__` |
| `Or` | `__or__` | `__ror__` |

The reflected second candidate must still check distinct implementation at
execution time. The first Python call may have mutated classes or MROs before
returning `NotImplemented`.

### In-Place Operators

In-place operators prepend the in-place dunder, then fall back to the ordinary
binary ordering.

Template:

```text
InPlace<Verb>NormalFirstFallback
    0: CallBinary("<inplace>")
    1: CallBinary("<normal>")
    2: CallBinaryReflected("<reflected>")
    3: RaiseUnsupported

InPlace<Verb>ReflectedFirstFallback
    0: CallBinary("<inplace>")
    1: CallBinaryReflected("<reflected>")
    2: CallBinary("<normal>")
    3: RaiseUnsupported
```

Rows:

| Verb | In-place dunder | Normal dunder | Reflected dunder |
| --- | --- | --- | --- |
| `Add` | `__iadd__` | `__add__` | `__radd__` |
| `Sub` | `__isub__` | `__sub__` | `__rsub__` |
| `Mul` | `__imul__` | `__mul__` | `__rmul__` |
| `Matmul` | `__imatmul__` | `__matmul__` | `__rmatmul__` |
| `TrueDiv` | `__itruediv__` | `__truediv__` | `__rtruediv__` |
| `FloorDiv` | `__ifloordiv__` | `__floordiv__` | `__rfloordiv__` |
| `Mod` | `__imod__` | `__mod__` | `__rmod__` |
| `Pow` | `__ipow__` | `__pow__` | `__rpow__` |
| `LShift` | `__ilshift__` | `__lshift__` | `__rlshift__` |
| `RShift` | `__irshift__` | `__rshift__` | `__rrshift__` |
| `And` | `__iand__` | `__and__` | `__rand__` |
| `Xor` | `__ixor__` | `__xor__` | `__rxor__` |
| `Or` | `__ior__` | `__or__` | `__ror__` |

The in-place candidate is attempted before right-subclass reflected priority.
If it is missing or returns `NotImplemented`, the ordinary binary protocol is
run with the normal/reflected order selected for the current operand types.

### Ternary Pow

Ternary `pow(lhs, rhs, mod)` uses ternary call actions for the same dunder
names:

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

There is no in-place ternary table. `**=` is a binary augmented assignment and
uses `__ipow__(rhs)` before falling back to binary `__pow__`/`__rpow__`.

### Rich Comparisons

Rich comparisons use binary call actions but comparison-specific reflected
names and fallbacks.

Template:

```text
Compare<Verb>NormalFirst
    0: CallBinary("<normal>")
    1: CallBinaryReflected("<reflected>")
    2: <fallback>

Compare<Verb>ReflectedFirst
    0: CallBinaryReflected("<reflected>")
    1: CallBinary("<normal>")
    2: <fallback>
```

Rows:

| Verb | Normal dunder | Reflected dunder | Fallback |
| --- | --- | --- | --- |
| `Lt` | `__lt__` | `__gt__` | `RaiseOrdering` |
| `Le` | `__le__` | `__ge__` | `RaiseOrdering` |
| `Eq` | `__eq__` | `__eq__` | `IdentityEq` |
| `Ne` | `__ne__` | `__ne__` | `IdentityNe` |
| `Gt` | `__gt__` | `__lt__` | `RaiseOrdering` |
| `Ge` | `__ge__` | `__le__` | `RaiseOrdering` |

Rich comparison methods may return any value. Boolean contexts apply normal
truthiness later. If both candidates return `NotImplemented`, equality falls
back to identity, inequality falls back to non-identity, and ordering
comparisons raise `TypeError`.

### Membership

Membership has protocol fallback even though it has only one named dunder
candidate. The fallback is selected when `__contains__` is missing, not when it
returns `NotImplemented`. If `__contains__` exists, its result is truth-tested
as the membership result. The opcode should be named for the primary dunder,
such as `Contains`, rather than for the surface syntax spelling `in`. `not in`
uses the same operation and negates the final truth value.

```text
Contains
    0: CallBinary("__contains__")
    1: ContainsFallback
```

For this table, `lhs` is the container and `rhs` is the searched item.
`ContainsFallback` runs the rest of the membership protocol for those saved
operands:

1. Try iteration and compare the searched item against each yielded value using
   equality semantics.
2. If iteration is unavailable, try sequence indexing from zero until the
   sequence protocol terminates the search.

This fallback is a native continuation/thunk boundary rather than an ordinary
Python dunder-call row. It may create an iterator, call `next`, run equality
comparisons, handle sentinel exceptions, and fall back to indexed subscription.
It is not driven by `CheckOperatorNotImplemented`.

## Protocol Semantics Summary

### Binary Operators

For an ordinary binary operator `lhs op rhs`, let:

- `L = type(lhs)`
- `R = type(rhs)`
- `F = dunder_lookup(L, normal_name)`
- `G = dunder_lookup(R, reflected_name)`

Candidate order:

1. If `L != R`, `R` is a strict subclass of `L`, and `G` exists with a
   different implementation than `F`, call `G(rhs, lhs)` first.
2. Call `F(lhs, rhs)` if it exists and was not already skipped as the same
   implementation as the reflected candidate already tried.
3. If the reflected candidate was not tried first, and `L != R`, and `G`
   exists with a different implementation than `F`, call `G(rhs, lhs)`.
4. If every called candidate returns `NotImplemented`, raise the
   operator-specific unsupported-operand `TypeError`.

Returning the singleton `NotImplemented` is the only protocol-level signal to
continue to the next candidate. Any other returned value is the result. Raising
or returning `Value::exception_marker()` exits through pending-exception
propagation.

### Unary Operators

Unary numeric operators are single-call protocols:

| Operation | Call |
| --- | --- |
| `-value` | `value.__neg__()` |
| `+value` | `value.__pos__()` |
| `~value` | `value.__invert__()` |
| `abs(value)` | `value.__abs__()` |

They do not need table-driven `NotImplemented` continuation.

### Subscription And Slicing

Subscription is a single-owner protocol:

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

`__getslice__`, `__setslice__`, and `__delslice__` are Python 2 protocols and
should not be part of this Python 3 dispatch design.

Class-object subscription is separate. `Class[key]` may involve metaclass
`__getitem__` and `__class_getitem__`; do not fold that protocol into the
ordinary get-item cache.

### Attribute And Descriptor Protocols

Attribute access is not ordinary method lookup on the object. It is rooted in
the type and descriptor behavior along the MRO:

| Operation | Protocol entry |
| --- | --- |
| `obj.attr` | `type(obj).__getattribute__(obj, "attr")` |
| missing-attribute fallback | `type(obj).__getattr__(obj, "attr")` |
| `obj.attr = value` | `type(obj).__setattr__(obj, "attr", value)` |
| `del obj.attr` | `type(obj).__delattr__(obj, "attr")` |

Descriptor methods participate in attribute dispatch:

| Descriptor operation | Protocol call |
| --- | --- |
| read descriptor | `descriptor.__get__(obj, owner)` |
| write descriptor | `descriptor.__set__(obj, value)` |
| delete descriptor | `descriptor.__delete__(obj)` |

Attribute ICs and dunder-method dispatch caches should share validity
machinery where possible, but attribute dispatch is its own cache family.

### Numeric Conversion Protocols

Numeric conversion and index coercion are dispatch sites, but most are
single-call from the operator-cache perspective:

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
operator fallback rules.

### Truth, Length, Call, Iteration, And Representation

These protocols need their own cache shapes when optimized:

| Operation | Protocol call |
| --- | --- |
| truthiness | `type(obj).__bool__(obj)`, then `type(obj).__len__(obj)` fallback |
| `len(obj)` | `type(obj).__len__(obj)` |
| length hint | `type(obj).__length_hint__(obj)` where applicable |
| `obj(*args, **kwargs)` | `type(obj).__call__(obj, *args, **kwargs)` |
| `iter(obj)` | `type(obj).__iter__(obj)`, with sequence fallback where implemented |
| `next(iterator)` | `type(iterator).__next__(iterator)` |
| `reversed(obj)` | `type(obj).__reversed__(obj)`, with sequence fallback where implemented |
| `repr(obj)` | `type(obj).__repr__(obj)` |
| `str(obj)` | `type(obj).__str__(obj)`, with fallback behavior |
| `format(obj, spec)` | `type(obj).__format__(obj, spec)` |
| `bytes(obj)` | `type(obj).__bytes__(obj)` |

Jump opcodes should preserve direct inline truthiness for values that do not
need protocol dispatch. Iteration has sentinel exceptions and fallback rules.
Call dispatch probably belongs with the existing function-call IC machinery.

## Installation Rule

Only the primary opcode miss path owns cache installation. A primary opcode
miss means the opcode's direct guards failed and it is about to run the generic
or table-driven protocol from the beginning. That path may replace the IC entry
for the opcode's cache index.

Continuation opcodes do not install or update IC entries. After a cached Python
candidate has returned to `CheckOperatorNotImplemented`, the continuation path
executes the remaining protocol to completion. It may recompute lookups, call
later candidates, raise, or return a result, but it must leave the cache state
unchanged.

For single-call protocols:

1. Run dunder lookup.
2. Validate callable shape and call adaptation.
3. Ask a trusted-handler resolver, if the selected method has one.
4. Install either a trusted handler or a dunder-method-call replay plan.

For multi-candidate and table-driven fallback protocols:

1. Walk the relevant dispatch table from the selected start index.
2. Recompute applicability and lookup results at each step.
3. Prefer a complete trusted native handler when the current table state can be
   collapsed under the operand shape guards.
4. Otherwise install at most one cacheable Python candidate, together with its
   table reference and candidate index. When the cached candidate is entered,
   the opcode writes the per-call continuation prefix into the caller frame.
5. For `NotImplemented`-continued tables, do not leave a Python-candidate entry
   installed for a candidate that returned `NotImplemented` on the miss. That
   would make the common hit immediately enter the cold continuation path.

Exception paths must not install direct handlers. If lookup, binding, call
validation, or execution raises, propagate the pending exception and leave cache
state unchanged unless the operation has an explicit negative-cache design.

## Open Questions

- Should trusted handlers require exact builtin type guards, or can some use
  shape guards that imply the same dunder-method lookup semantics?
- How much of binary, in-place, comparison, and membership cache validation can
  share one table walker without obscuring hot opcode paths?
- Should the continuation prefix store a direct metadata pointer or a small SMI
  table id?
- Which odd callable shapes, if any, should be supported by cached Python
  dunder-call replay after the fixed-arity function-shaped path lands?
