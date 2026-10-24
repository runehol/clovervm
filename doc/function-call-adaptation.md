# Function Call Adaptation

| Field | Value |
|---|---|
| Document type | Architecture contract |
| Status | Implemented |
| Scope | Python signature binding, argument expansion, defaults, collectors, and call-site caches |
| Owning layers | Parser/AST, codegen, function metadata, runtime adaptation, and interpreter |
| Validated against | `fc13f0ee` (2026-10-24) |

CloverVM has separate positional, explicit-keyword, and expanded-call paths for
Functions, eligible constructor thunks, and attribute method calls. They all
enter the same managed frame convention. Calls without expansion keep their
existing opcodes and avoid constructing expansion transport.

[Function Calling Convention](function-calling-convention.md) owns frame layout,
register encoding, alignment, and entry/return. [Call Argument Temporaries and
Lazy Tracebacks](call-argument-temporaries-and-lazy-tracebacks.md) owns call-span
lifetime and traceback preservation. This document describes the implemented
adaptation above those contracts.

## Ownership by layer

| Layer | Responsibility |
|---|---|
| Parser/AST | Signature groups and ordered positional, starred, keyword, and double-starred arguments |
| Codegen | Evaluation order, rooted temporaries, container-building precode, call opcode selection |
| Function metadata | Parameter layout, keyword-bindable names, default locations and values |
| Runtime adaptation | Binding validation, native plans, defaults, varargs and keyword collectors |
| Interpreter | Callable/method resolution, IC guards, helper entry, frame publication and dispatch |

The native binder receives an already-resolved Function. It does not perform
attribute lookup or duplicate callable resolution. Arbitrary object `__call__`
and descriptor dispatch beyond the existing method-call support are unsupported.

## Signature and default metadata

Parameter slots follow these groups:

```text
positional-only | positional-or-keyword | *args | keyword-only | **kwargs
```

Absent collectors occupy no slot. `FunctionSignature` records the group counts,
collector flags, `first_default_slot`, and `default_presence_mask`. `CodeObject`
owns the signature and `FunctionKeywordRemap`; `Function` keeps a hot signature
copy and positional arity bounds. Function code/signature is currently immutable
from Python.

`FunctionKeywordRemap` contains interned parameter names and logical parameter
indices for positional-or-keyword and keyword-only parameters, in declaration
order. Positional-only parameters and the `**kwargs` collector are excluded.
An incoming keyword matching a positional-only name is therefore unmatched: it
is collected by `**kwargs` when present, or rejected otherwise.

Default expressions are evaluated when the Function is created. Its defaults
tuple is a slot-indexed suffix beginning at `first_default_slot`:

```python
def f(a, b=5, *args, c=6, d=7): ...
```

```text
parameter slots:       a, b, args, c, d
first_default_slot:    1
defaults tuple:        (5, None, 6, 7)
default_presence_mask: 0b1101    # Bit zero describes the first tuple entry.
```

`None` placeholders keep ordinary tuples free of VM sentinels. The mask
separates placeholders from real defaults, including an actual `None` default.
Default values are copied by reference from the current Function; caches do not
freeze them.

## Positional and explicit-keyword calls

Calls without expansion use `CallPositional` or `CallKeyword`, with fused
attribute-method counterparts. Positional values are staged in an aligned
outgoing argument span. Explicit keyword values occupy a separate temporary
span so reordering cannot overwrite unread values. Names are a constant tuple
of interned strings fixed by the instruction.

For example, this caller:

```python
def caller(f):
    return f(10, c=30, b=20)
```

generates the following body (`p0` is `f`):

```text
Mov r0, p0
LdaSmi 10
Star r4
LdaSmi 30
Star r1
LdaSmi 20
Star r2
CallKeyword r0, {r4:1}, kw_values={r1..r2}, kw=("c", "b"), keyword_call_ic[0]
Return
```

Here `r4` starts the positional span; `r1` and `r2` hold the keyword values.
The target signature determines where `c` and `b` are written. For a call with
only positional arguments, codegen stages the outgoing values and emits
`CallPositional`, with no keyword span or names tuple.

These examples are taken from the bytecode printer, with offsets and unused
epilogues omitted, compact `StarN` moves written as `Star rN`, and string
constants shown by value where helpful. Register numbers include alignment
padding and can differ with surrounding expressions.

`FunctionCallAdaptation` selects the ordinary entry work:

| Tier | Work after successful validation |
|---|---|
| `FixedArity` | Enter with prepared positional arguments |
| `Defaultable` | Fill missing defaults before entry |
| `Full` | Fill defaults and initialize `*args` and/or `**kwargs` |

A `**kwargs` collector never absorbs excess positional arguments. Positional
calls to such a signature allocate a fresh empty keyword dict. The full path
packs extra positional arguments into a tuple.

`FunctionCallInlineCache` guards callable identity, positional count, and any
constructor lookup validity dependency. It stores the resolved Function,
CodeObject, and adaptation tier.

`KeywordCallInlineCache` adds an encoded destination for each explicit keyword,
`default_fill_start_slot`, and keyword count. Names are fixed by the call-site
constant, so there is no dynamic keyword-name guard. Encoded destination zero
means insert into the keyword collector; real destinations cannot be zero
because `fp[0]` is frame-header storage.

On a keyword miss, the runtime validates a local candidate before replacing the
live cache. It rejects excess positional arguments, resolves names through the
remap, rejects duplicate formal fills and unexpected keywords without a
collector, then checks required/defaulted slots. Entry copies the accepted
contiguous default suffix, initializes collectors, and places keyword values.
Placeholder defaults are overwritten before the callee can observe its frame.

## Expanded-call transport

Codegen lowers mixed and repeated `*iterable`/`**mapping` sources into:

```text
argument expressions and expansion callbacks
    -> exact positional tuple + fresh exact keyword dict
    -> native binding plan or cached plan
    -> normalized parameter window
    -> ordinary frame entry
```

Both containers are always present, including a fresh empty keyword dict for
`f(*args)`. Exact dict means the builtin dict type; either string-keyed or general
storage is valid. String-subclass key objects are preserved, and non-string
keys survive construction until call-entry validation.

Two adjacent logical registers hold the tuple and dict. With the current
register encoding they are `fp[containers_reg]` and `fp[containers_reg - 1]`.
The callable/receiver and both inputs stay outside the outgoing window. The
expanded-call instructions encode the first register of the pair and a separate
outgoing anchor; runtime container sizes provide the argument counts. The call
result returns in the accumulator.

### Evaluation and positional construction

For a straight call, codegen evaluates and roots the callable before arguments.
For an attribute call it evaluates and roots the receiver; the fused opcode
performs lookup after argument construction. This accepted late lookup also
applies to ordinary method calls and differs from CPython.

Positional expressions, including starred sources, are evaluated before keyword
expressions even when a starred argument appears textually after a keyword.
Scope analysis follows that same order. Conversion timing has two paths:

- **Exactly one starred positional source and no ordinary positional values:**
  evaluate/root the source, construct all keywords, then convert it to a tuple.
- **Other expanded calls:** append ordinary positional values and extend starred
  sources into a list builder, then convert it to a tuple before keywords. With
  no positional sources, create an empty tuple instead.

Thus `f(*source, x=value())` runs `value()` before iterating `source`, whereas
`f(0, *source, x=value())` iterates first. The source expression runs before
`value()` in both cases.

#### Lone positional expansion with keywords

```python
def caller(f, args, kwargs):
    return f(*args, **kwargs)
```

```text
Mov r0, p0                       # f
Mov r1, p1                       # Save args without iterating yet.
Ldar p2                          # kwargs
BuildCallKeywords r0, r2         # Install a fresh keyword dict in r2.
ToExactTuple r1                  # Reuse an exact tuple, otherwise iterate.
Star r1                          # Conversion returns in the accumulator.
CallExpanded r0, containers=r1, first_arg=r4, expanded_call_cache[0]
Return
```

`r1` and `r2` are the adjacent transport pair; `r4` is a separate aligned
outgoing anchor. Keyword construction precedes tuple conversion. For
`f(*args)` without keywords, the `Ldar`/`BuildCallKeywords` pair is replaced by
`CreateDict {r2:0}` and `Star r2`; a fresh empty dict is still supplied.

#### Mixed positional and keyword construction

```python
def caller(f, items, value, extra):
    return f(1, *items, flag=value, **extra)
```

```text
Mov r0, p0
CreateList {r1:0}
Star r1
LdaSmi 1
ListAppend r1                    # Append the accumulator value.
Ldar p1                          # items
ListExtend r1
ListToTuple r1                   # Replace the builder register in place.
Mov r3, p2                       # Evaluate the explicit keyword value.
CreateDict {r2:0}
Star r2
AddCallKeyword r0, r2, "flag", r3
Ldar p3                          # extra
MergeCallKeywords r0, r2
CallExpanded r0, containers=r1, first_arg=r4, expanded_call_cache[0]
Return
```

Here iteration and list-to-tuple conversion finish before keyword construction.
`AddCallKeyword` and `MergeCallKeywords` write directly into `r2`; their `None`
accumulator result does not need storing. A duplicate `flag` in `extra` fails
during the merge, before call entry.

`ListToTuple` copies element references without mutating the source list.
`ToExactTuple` reuses an exact tuple by identity; `ToExactList` reuses an exact
list by identity. Their guards use root shapes cached on `ThreadState`.
Every other input, including container subclasses, goes through managed
iteration so iterator overrides run. Tuple conversion collects through a list
builder before allocating the fixed-size tuple.

`ListExtend` has native exact-tuple/list paths. Self-extension uses the original
list extent. Other sources enter the VM-owned `_clover_list_extend` helper.
Iteration failures propagate with already appended elements retained.
The conversion helpers are `_clover_iterable_to_exact_tuple` and
`_clover_iterable_to_exact_list`, compiled from trusted Python source. Their
append/conversion intrinsics lower directly to bytecode.

### Keyword construction

All keyword construction maintains one fresh, call-owned exact dict. The first
mapping uses `BuildCallKeywords`; later mappings use `MergeCallKeywords`.
Explicit values use `AddCallKeyword`. Build and Merge read their source from
the accumulator; Add reads its value from a register. They leave the destination
dict in its register and return `None` in the accumulator.

Merges and additions reject an equal existing key without overwriting its value.
The context register supplies duplicate-keyword and mapping-error context
without signature resolution. In expanded method precode this is the receiver,
because lookup has not happened yet.

#### Multiple mappings and an explicit keyword run

```python
def caller(f, first, left, right, last):
    return f(**first, a=left(), b=right(), **last)
```

The generated body evaluates both explicit values before inserting either:

```text
Mov r0, p0
CreateTuple {r1:0}
Star r1
Ldar p1                          # first
BuildCallKeywords r0, r2
Mov r5, p2                       # left
CallPositional r5, {r6:0}, call_ic[0]
Star r3
Mov r5, p3                       # right
CallPositional r5, {r6:0}, call_ic[1]
Star r4
AddCallKeyword r0, r2, "a", r3
AddCallKeyword r0, r2, "b", r4
Ldar p4                          # last
MergeCallKeywords r0, r2
CallExpanded r0, containers=r1, first_arg=r4, expanded_call_cache[0]
Return
```

If `first` already contains `a`, both `left()` and `right()` have run when the
first Add fails; `last` has not been processed. The final outgoing anchor can
reuse `r4` because the explicit value stored there is dead after insertion.
The callable and transport pair remain outside that window. Positional-versus-
keyword parameter conflicts are detected later by binding.

**Exact dict sources.** The first mapping uses `Dict::copy()`, preserving key
identity, order, values, and stored hashes without callbacks. String-subclass
and non-string keys use general dict storage. A later exact-dict merge uses
native string-keyed uniqueness operations when possible; general storage uses
managed known-hash insertion with rich equality and restartable probes.

General-storage merge retains the current key/value/hash in managed registers
and traverses native entries by numeric index, never holding an entry pointer
across callbacks. After each successful insertion it compares the source entry
count with its initial count and raises
`RuntimeError("dict mutated during update")` on a change. Callback or duplicate
errors take precedence. Value replacement and same-size mutations are accepted;
source generation changes are not an extra rejection condition. Destination
probe-generation checks restart invalidated probes.

**Generic mappings, including dict subclasses.** The managed
`_clover_merge_call_keyword_mapping` helper:

1. Calls `source.keys()`, translating `AttributeError` around that call,
   including its body, into a contextual mapping `TypeError`.
2. Applies `ToExactList` before fetching any values. An exact list is reused;
   other iterables are fully materialized into a fresh list.
3. Uses an ordinary iterator over the live list. For each key, checks for a
   duplicate before fetching `source[key]`, then inserts the value.

Materialization, value lookup, hashing, and equality are outside the
`AttributeError` translation boundary; their exceptions propagate unchanged.
List replacement, growth, shrinkage, and shifted-entry behavior remain visible
to its ordinary iterator. An iterable of pairs without a mapping interface is
not accepted.

Non-string keys are not rejected during construction. Later keyword sources
still execute, and their duplicate/mapping errors can precede the eventual
non-string-key error at call entry. Partially constructed containers retain
successfully appended/inserted entries until normal lifetime cleanup.

## Expanded binding and collectors

`build_expanded_call_binding_plan(thread, function, positional_count, keywords)`
returns a fallible native plan. It operates on an already-resolved Function and
rooted inputs, with no writes to the outgoing parameter window:

1. Validate every keyword key is a string, accepting subclasses, before any
   comparison callback can run.
2. Mark positional formal slots filled. Process incoming keywords in insertion
   order, scanning keyword-bindable parameters in declaration order. Reject
   duplicate formal fills and unmatched names without a collector.
3. Reject excess positional arguments unless `*args` is present.
4. Record defaults for unfilled slots; reject missing required arguments.

This gives keyword errors precedence over excess positional arguments, then
missing arguments. Construction errors in precode occur before all these
checks. An invalid plan is neither applied nor installed.

Exact-string keys use native `string_eq`. Subclass keys use
`ThreadState::test_equal`, including reflected rich comparison, truth conversion,
and exception propagation. This uncommon path permits C++-to-managed interpreter
reentry. Matching does not hash keys or probe once per formal parameter.
The builder's filled-slot scratch uses `absl::InlinedVector<bool, 16>`.

```cpp
struct ExpandedCallBindingPlan {
    static constexpr uint32_t UnmatchedKeyword = UINT32_MAX;
    std::vector<uint32_t> keyword_destinations;
    std::vector<uint32_t> default_slots;
    KwargsAction kwargs_action;
};
```

The plan contains logical parameter indices and flags, no Python references.
Each keyword destination corresponds to an incoming dict entry; unmatched
entries use `UnmatchedKeyword`. Default slots identify values to read from the
Function's current defaults tuple.

`apply_expanded_call_binding_plan` constructs and roots collectors before
writing outgoing parameters, so nested callbacks cannot overwrite partially
prepared arguments. It then copies positional and keyword values, current
defaults, and collectors into the target's parameter slots.

| Collector action | Behavior |
|---|---|
| `None` | No `**kwargs` parameter; unmatched keywords were rejected |
| `ReuseTransport` | No keyword matched a named parameter; hand over the fresh transport dict |
| `BuildLeftovers` | Some keywords matched named parameters; allocate a second fresh dict containing only unmatched entries |

Transport is never consumed or pruned. Leftovers retain original key objects
and relative order; all-matched keywords still produce a fresh empty collector.
If a `*args` collector is present, application reuses the input tuple when it is
exactly the required suffix, otherwise packs a new tuple of the remaining
positional values.

A signature exactly `(*args, **kwargs)` with no implicit receiver prefix takes
the pure-collector path: validate keys and install both original containers
directly, without a binding plan or another dict.

## Expanded-call IC

`ExpandedCallInlineCache` stores callable identity, the resolved Function,
constructor lookup validity dependency where applicable, adapter mode, and a
`unique_ptr` to one optional out-of-line entry:

```cpp
struct ExpandedCallCachedPlan {
    uint32_t positional_count;
    std::vector<Owned<TValue<String>>> keyword_names;
    ExpandedCallBindingPlan plan;
};
```

A target-guard miss resolves the target again and clears the subordinate plan,
including after constructor lookup invalidation. The ordered keyword names
retain their own references; keyword values and default values are never cached.
Function code/signature immutability means no separate signature-version
mechanism is needed.

For general binding:

1. A hit requires matching total positional count, keyword count, and ordered
   exact-string key contents. `string_eq` accepts equal noninterned names, not
   just identical objects. A successful guard also proves key validity.
2. On a hit, apply the stored plan directly. Guarding and application for these
   exact-string inputs do not invoke Python callbacks.
3. On a miss, build and apply a local plan. After success,
   `convert_to_expanded_call_cache_entry` checks eligibility, retains names, and
   moves the existing plan into a new entry. It does not bind again.
4. Any subclass key prevents both a hit and installation. Such calls run their
   comparison protocols every time and do not evict an existing plan for the
   same target. Non-string keys fall through to normal validation and fail.

Replacement, cache clearing, and CodeObject destruction release the native entry
and its names. IC copies deep-copy entries because `BytecodeDecoder` snapshots
the tables; moves transfer ownership. Retained names follow the same native
`Owned`-container pattern as `CodeObject::constant_table`.

## Methods, constructors, and runtime boundaries

Fused method opcodes combine attribute lookup/binding with call entry. The
attribute lookup IC and call IC are separate. Lookup produces the callable plus
an optional implicit receiver; instance-held Functions and module attributes do
not acquire a receiver merely because they are callable.

For an expanded attribute call:

```python
def caller(obj, args, kwargs):
    return obj.m(*args, **kwargs)
```

codegen emits the same transport construction as the combined expansion example,
with `obj` in `r0`, and ends with:

```text
CallMethodAttrExpanded r0, "m", lookup_cache[0], expanded_call_cache[0], containers=r1, first_arg=r4
Return
```

This instruction performs lookup and receiver binding after expansion; no early
method lookup is emitted.

Expanded methods pass an `ExpandedCallPositionalArguments` view containing the
tuple and optional self prefix. `not_present` means no prefix, keeping `None` a
valid receiver. The binder sees the total count, including self; application
indexes the view without allocating a prepended tuple. If only self is consumed,
the original tuple can become the varargs suffix. If the collector includes
self, application packs a new tuple. Pure-collector direct handoff is restricted
to calls without a prefix.

Eligible class calls resolve to constructor thunks through the shared callable
resolver. Thunks mirror the selected `__init__` or `__new__` signature with
`self`/`cls` removed, shifting keyword remaps and rebuilding default metadata.
Binding happens at the public class-call boundary. The thunk receives normalized
parameters and forwards through prepared `CallCodeObject` entry rather than
binding keywords again. See [Constructor Thunks](constructor-thunks.md) for
allocation and constructor-selection policy.

Managed expansion helpers enter through the normal outgoing window at
`code_object->get_first_free_arg_encoded_reg()`. No extra window operand or
special continuation instruction is required. Helpers return through the
ordinary accumulator convention or propagate exceptions through normal
unwinding. Sources, builders, destinations, and iterator state remain rooted
in managed registers/helper locals across callbacks.

Before callback-capable native binding, expanded handlers publish the caller's
live frame frontier. The resolved Function and optional receiver are retained
independently of the live cache: recursive calls can replace it. Uncached plans
stay local across callbacks; cache hits borrow a plan only on the callback-free
exact-string path. Failures during collector construction do not enter the
callee. Pending-exception propagation is explicit in the native helper return
types.

Expanded calls currently resume in the interpreter from JIT code. Bytecode
metadata describes transport inputs and accumulator results for safe fallback;
there is no specialized expanded-call JIT lowering or separate continuation
protocol.

## Compatibility boundaries

These are existing behavior boundaries, not unfinished expanded-call stages:

- Method lookup happens after argument evaluation/construction for fused method
  calls. Correcting it requires work shared across all argument forms.
- Collector construction does not reproduce CPython's additional subclass-key
  hash/equality callbacks or their interleaving with binding. Transport reuse
  can skip reinsertion entirely; leftovers are built only after successful
  planning. Named-parameter matching still runs rich comparison on subclass
  keys every call.
- Generic mapping error translation is limited to the direct `source.keys()`
  call. Later callback exceptions retain their original types.
- Binding `TypeError` messages remain generic in several paths.
- Arbitrary object `__call__`, descriptor-driven callable behavior beyond the
  existing method support, and native-to-managed keyword-call APIs require
  separate work.

## Implementation map

| Area | Source |
|---|---|
| Signature/argument syntax and evaluation | [parser.cpp](../src/compiler/parser.cpp), [codegen.cpp](../src/compiler/codegen.cpp) |
| Signature and IC representation | [code_object.h](../src/bytecode/code_object.h), [function.h](../src/object_model/function.h) |
| Native expanded binder, cache guards, application | [expanded_call.h](../src/runtime/expanded_call.h), [expanded_call.cpp](../src/runtime/expanded_call.cpp) |
| Call/helper dispatch and explicit-keyword binding | [interpreter.cpp](../src/runtime/interpreter.cpp) |
| Managed iteration/mapping helpers | [builtin_implementations.py](../src/bootstrap/builtin_implementations.py) |
| Dict merge and known-hash helper generation | [dict.cpp](../src/builtin_types/dict.cpp) |
| Constructor forwarding | [constructor_thunk.cpp](../src/object_model/constructor_thunk.cpp) |

Interpreter, parser, codegen, and JIT fallback tests cover these paths, including
callback mutation, error precedence, key identity, method binding, cache
ownership/invalidation, and reentrant calls. The `expanded_call_*.py` benchmarks
cover direct comparisons, positional/keyword/combined expansion, mixed
construction, collector reuse/leftovers, and expanded methods.
