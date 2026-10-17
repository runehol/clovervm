# Function Call Adaptation

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Explicit calls and callee adaptation implemented; caller expansion remains a design sketch |
| Scope | Python argument binding and adaptation above the managed frame convention |
| Owning layers | Parser, codegen, function metadata, runtime call adaptation, and interpreter |
| Validated against | `a909d10` (2026-06-25); expanded-call operand contract agreed 2026-10-17 |
| Supersedes | N/A |

This note records the implemented function-call adaptation model for Python
keyword calls and the current design direction for caller argument expansion. It builds on
the low-level frame mechanics documented in
[CloverVM Function Calling Convention](function-calling-convention.md).
Call-span lifetime, method-lookup ordering, alignment, and lazy traceback
preservation are detailed in
[Call Argument Temporaries And Lazy Tracebacks](call-argument-temporaries-and-lazy-tracebacks.md).

The goal is to add Python's richer call binding semantics without turning the
existing positional `CallPositional` hot path into a broad generic slow path.

## Current State

The implemented call path now has separate positional and keyword-aware entry
paths:

- callers lay out positional values in a contiguous temporary call argument
  span;
- `CallPositional`, `CallMethodAttrPositional`, and constructor thunks enter ordinary
  `Function` objects through the same frame convention;
- `CallKeyword` supports explicit caller keywords for ordinary functions and
  eligible constructor thunks;
- `Function` metadata tracks minimum and maximum positional arity, grouped
  signature counts, keyword-bindable parameter remaps, and
  `first_default_slot` plus `default_presence_mask`;
- entry adaptation supports fixed arity, positional defaults, required and
  defaulted keyword-only parameters, mixed positional/keyword-only default
  layouts, callee `*args`, and callee `**kwargs`;
- parser/codegen represent explicit caller positional and keyword arguments;
- parser and codegen support positional-only parameters before `/`;
- caller `*args` and `**kwargs` expansion remain unsupported.

The keyword-call path deliberately does not introduce the full Python callable
protocol. Descriptor dispatch, arbitrary object `__call__`, keyword method-call
lowering, and caller expansion forms remain separate work.

## Python Signature Shape

Function signatures should be represented in the same broad groups CPython uses:

```text
arguments(
  posonlyargs,
  args,
  vararg,
  kwonlyargs,
  kw_defaults,
  kwarg,
  defaults
)
```

For clovervm this does not require copying CPython's AST classes directly, but
the parser should preserve the same semantic grouping:

- positional-only parameters before `/`;
- positional-or-keyword parameters after `/` and before `*`;
- optional varargs parameter, `*args`;
- keyword-only parameters after `*args` or bare `*`;
- optional kwargs parameter, `**kwargs`.

This grouping is important because each parameter class has different binding
rules. Codegen should not have to rediscover where `/` or `*` appeared by
scanning a flat parameter list.

Defaults may remain attached to parameter nodes if that best fits the local AST
style, but codegen should lower positional-or-keyword defaults and keyword-only
defaults into one runtime default table whose zero index corresponds to the
first callee parameter slot that has a default.

## Call Argument Shape

Implemented calls represent positional and explicit keyword arguments with
call-argument AST nodes distinct from signature parameters. Caller `*iterable`
and `**mapping` expansion, including mixed and repeated sources, is not yet
implemented.

## Expanded Calls: Current Design Direction

The direction agreed during discussion on 2026-10-13, with the input layout
settled on 2026-10-17, is container-building precode followed by a container-input
call adapter, called the **expander** here.
This is a design sketch, not an implementation-ready contract or implementation
plan. Ordinary calls without expansion keep their existing fast paths.

```text
evaluate callable into an ordinary register
precode -> adjacent exact positional tuple + fresh exact keyword dict
expander -> normalized callee parameter window -> frame entry
```

Precode owns evaluation, iterable/mapping callbacks, container construction, and
expansion validation. The expander owns callable/signature resolution, argument
binding, defaults, collector handling, and frame entry. A new dynamically indexed
managed argument area is not required by this direction.

### Positional precode

For a single `*source`, reuse an exact tuple; otherwise consume the iterable into
an exact tuple. Tuple subclasses go through iterable conversion so overrides of
`__iter__` are honored. For mixed positional arguments or multiple starred
sources, use a growing list builder, append ordinary values, extend from each
starred iterable, and convert to an exact tuple when the final count is known.
Tuple storage requires its size up front; arbitrary iterables do not provide
that count in advance. With no positional sources, an empty tuple suffices.

```text
f(x, *a, y, *b)

callable_reg = evaluate f
builder = list containing evaluated x
extend builder from evaluated a
append evaluated y
extend builder from evaluated b
args_tuple = convert builder to an exact tuple
```

These operations preserve Python evaluation and expansion sequencing. Iteration
is outside the expander, leaving separate specialization points for expansion
and binding. Copy element references, not the elements themselves.

#### List-builder bytecodes

Use the existing `CreateList` with zero items to allocate an empty exact list,
and add two mechanical builder operations:

```text
CreateList ..., 0     -> empty exact list in accumulator
ListAppend builder   <- append the item in accumulator
ListToTuple builder  -> exact tuple in accumulator
```

`ListAppend` and `ListToTuple` require an exact list builder created by generated
precode. They access native list storage directly, without Python method lookup
or callbacks. Append retains the stored item; tuple construction retains the
copied element references. Converting to a tuple does not mutate the builder.
Codegen keeps the builder rooted while evaluating expressions and running any
iteration code that supplies items.

These operations cover building mixed positional arguments. They do not perform
iterable expansion themselves. Keyword merging and the detailed iteration/error
contracts still need concrete sketches before implementation.

#### Single-source conversion: ToExactTuple

`ToExactTuple` takes an iterable source in the accumulator and returns an exact
tuple in the accumulator. An exact tuple passes through unchanged, preserving
identity and avoiding allocation. Every other input, including tuple subclasses,
takes the iteration path so subclass `__iter__` overrides are honored.

The fallback is an ordinary managed call to a VM-owned helper Function compiled
once from trusted Python source with the other builtin implementations:

```python
def _clover_iterable_to_exact_tuple(source):
    builder = []
    for item in source:
        __clover_list_append__(builder, item)
    return __clover_list_to_tuple__(builder)
```

The two `__clover_*` spellings are proposed trusted compiler intrinsics, lowered
directly to `ListAppend` and `ListToTuple`, not runtime function calls. The list
literal uses existing list allocation. Ordinary loop compilation supplies the
iterator protocol calls and their ICs, loop control, and StopIteration handling;
the loop and exception tables are not hand-emitted in C++.

The VM retains the resulting Function and the opcode enters it directly, without
public builtin lookup. Its CodeObject owns the normal iteration ICs, shared by
fallback calls. Exact-tuple reuse remains in the opcode; the helper only collects
an iterable. A future tuple constructor can reuse this conversion machinery.

A general iterator does not supply a reliable final element count. Collection
therefore uses growable list storage before allocating the fixed-size tuple and
copying element references. An exact-list-to-tuple fast path could avoid the
intermediate builder later; it is not required by this slice.

The opcode's exact-tuple guard advances directly on a hit. Otherwise it
tail-dispatches to a slow handler, which places the source in a one-argument
outgoing window below the caller's live registers and enters the helper through
existing managed frame-entry machinery. The saved return PC points immediately
after `ToExactTuple`. The helper's ordinary return restores the caller with the
tuple in the accumulator:

```text
Ldar source
ToExactTuple
Star containers_reg
```

The source parameter, builder local, and iterator state use normal managed
rooting. Iterator callbacks make normal managed calls; failures propagate
through existing exception unwinding instead of resuming at the following
`Star`. No recursive C++ interpreter invocation or separate continuation-state
structure is introduced. Detailed scratch-window placement and expansion-error
context must be resolved before implementation.

### Keyword precode and error context

Explicit keyword runs and repeated `**mapping` sources build one fresh exact
transport dict. Merge operations reject duplicate names rather than overwriting
them. A mapping source must support mapping-style key enumeration and value
lookup; arbitrary iterables of pairs are not accepted. Validate string keyword
names before binding.

For the first `**source`, when no keyword destination has been built yet, an
exact dict can use a direct native copy into a fresh exact dict. Its keys are
already unique, so that copy needs no duplicate checking; string-key validation
still applies. A generic mapping can repeat keys even within the first source,
so its fallback must still reject duplicates before fetching the repeated key's
value.
Other mappings, including dict subclasses, use the general mapping merge path.
Never reuse the caller's mutable dictionary as transport. Precode always
provides a fresh exact dict, including a fresh empty dict for `f(*args)` when
there are no keyword sources.

Here "exact dict" means the builtin dict class, not a restriction to its
string-keyed storage shape. Keyword keys may be string subclasses and must be
preserved as the original objects, without conversion to exact strings. CPython
3.14.5 preserves key identity and subclass attributes when the callee captures
the key in `**kwparams`. A key consumed by a named parameter does not appear in
that collector.

Exact builtin string keys use string-keyed storage. A string-subclass key uses
general dict storage, promoting the destination when needed and honoring its
hash/equality protocols. Non-string keys raise `TypeError`. Both keyword builder
operations and `CallExpanded` must support either dict storage shape; neither
may assume that an exact dict has only exact string keys. General binding must
also preserve the original key objects when packing leftover keywords into a
callee's `**kwargs` dict.

The callable is evaluated first and remains rooted in an ordinary register while
arguments are constructed. Normalization and merge operations receive that
register where needed for call-specific errors. They do not need the signature
merely to name the callable in an expansion error.

#### Keyword builder, merge, and insertion bytecodes

The two mapping operations take their source mapping from the accumulator and have
an explicit callable-context register and destination register:

```text
BuildCallKeywords callable_reg, dict_reg
MergeCallKeywords callable_reg, dict_reg
AddCallKeyword callable_reg, dict_reg, name_const, value_reg
```

`BuildCallKeywords` creates a fresh exact dict in `dict_reg`, using the native
copy fast path for an exact dict source. `MergeCallKeywords` extends the existing
exact, call-owned dict in `dict_reg`, rejecting duplicate keys instead of
replacing values. Both operations accept either source/destination dict storage
shape, preserving string-subclass keys and promoting the destination as needed.
The destination is stable across operations; no `Star` is
needed between building the dictionary and subsequent merges. The accumulator
is the source operand, not the dictionary result interface.

`AddCallKeyword` inserts one explicit `name=value` argument into the existing
exact, call-owned dict in `dict_reg`. `value_reg` holds the evaluated value;
`name_const` identifies the exact builtin string name in the constant pool.
It rejects an existing equal key instead of replacing its value, using
`callable_reg` for duplicate-keyword error context. It supports both destination
storage shapes; general storage must honor string-subclass keys' hash/equality
protocols during duplicate detection. Successful insertion appends the new entry
in keyword order. The destination remains in its register, without a subsequent
`Star`.

Codegen evaluates every value in a consecutive explicit keyword run into
temporary registers, in order, before emitting that run's `AddCallKeyword`
operations in the same order. The values remain rooted through evaluation and
insertion, including hash/equality callbacks. No accumulator reload is needed
for insertion. An initial explicit run uses a fresh empty destination dict.
Explicit runs and `MergeCallKeywords` operations alternate in keyword order.

For `f(**d, a=first(), b=second(), **later())`, both `first()` and `second()` run
before either explicit keyword is inserted. If inserting `a` detects a duplicate,
the call fails before evaluating `later()`. These operations build transport
only; parameter matching and positional-versus-keyword conflicts remain the
binder's responsibility.

The callable register supplies context for duplicate-keyword, invalid-mapping,
and non-string-key errors. Signature resolution is not needed for these errors.
The exact-dict merge fast path reads native entries but must check for collisions
with the destination before inserting them.

Generic sources, including dict subclasses, use a managed helper receiving
`(callable, destination, source)`. Build starts with a fresh empty destination;
merge supplies the existing one. The helper calls `source.keys()` and normalizes
the result to an exact list: reuse an exact list unchanged; materialize every
other iterable, including list subclasses, into a fresh exact list. Materialization
finishes before any `source[key]` lookup or insertion from this source. If it
raises, no values from this source have been fetched or inserted.

The merge then visits that list in order, checks each key for a duplicate before
fetching `source[key]`, and inserts the value. Reusing an exact list is observable:
if a value lookup changes a later entry in the returned list, the merge sees the
changed key. Do not snapshot an exact list into a copy or tuple. The list and
source remain rooted across callbacks. The exact list-conversion lowering and
mutation-during-traversal mechanics still need an implementation sketch.

The helper validates string keyword names before binding; detailed validation
and error timing still need a semantic sketch. An iterable of pairs without a
mapping interface is not accepted. Calls and iteration use normal managed ICs.

The destination must remain rooted in its register while callbacks run. Managed
fallback entry/return must preserve the direct destination-register contract;
it must not rely on a caller-emitted `Star` after the operation. A build fallback
can establish the empty destination before entering the shared merge helper.
Detailed continuation and failure-cleanup mechanics remain to be specified.

For a first keyword mapping followed by another mapping, precode is:

```text
accumulator = args_source
ToExactTuple
Star containers_reg
accumulator = first_keyword_source
BuildCallKeywords callable_reg, next_container_reg
accumulator = next_keyword_source
MergeCallKeywords callable_reg, next_container_reg
CallExpanded callable_reg, containers_reg, first_arg_reg, cache_idx
```

Here `next_container_reg` denotes the next logical register after
`containers_reg`. Explicit keywords use `AddCallKeyword` with the same destination
and uniqueness rule. With no keyword sources, precode creates a fresh empty dict
in that register.

Mixed sources use builder/merge precode before the same call opcode. Codegen
reserves two adjacent logical
registers: `containers_reg` holds the exact positional tuple and the next logical
register holds the fresh exact keyword dict. Only the first register is encoded;
there are no separate tuple and keyword-register operands.

`first_arg_reg` is a separate outgoing parameter-window anchor, following the
existing managed call convention. Keep the callable and input pair outside that
window so parameter binding cannot overwrite inputs it still needs to read.
`cache_idx` identifies the expanded-call cache; the result returns through the
accumulator. Runtime container sizes supply argument counts.

There is one expanded-call opcode and one input layout. Both containers are
always present. There is no tuple-only variant, absent-keyword flag, or None
sentinel for the keyword input. Ordinary calls without expansion retain their
existing opcodes.

Duplicate keywords across sources are detected during container merging, before
insertion loses the duplicate information. This allows CPython's early error
boundaries, including stopping before later keyword-source expressions and,
for generic mappings, before fetching a duplicate key's value. Positional versus
keyword parameter conflicts are detected later by signature binding. Earlier
discussion allowed best-effort early duplicate timing for register-span
transport; that latitude is recorded under alternatives rather than used to
justify overwriting duplicates in dictionary transport.

### Expander and two call-site IC modes

Inputs are the callable and the adjacent exact positional tuple/fresh exact
keyword dict pair. Both containers are always present. All user iterable/mapping
expansion protocols have already run. The keyword dict may have either
string-keyed or general storage. Resolve the
callable through existing function/constructor-thunk machinery, then select:

- **Pure collector:** the signature is exactly `(*args, **kwargs)`, with no named
  positional or keyword-only parameters. Install the exact tuple and fresh dict
  directly into the two parameter slots, including the fresh empty dict built
  by precode when no keywords were supplied. No positional repacking or second
  dict is needed.
- **General binding:** bind native tuple elements and dict entries into the
  signature's parameter slots, validate arity and positional/keyword conflicts,
  supply defaults, and pack collector leftovers. Enter only after successful
  normalization. See [Expanded Call Binding](#expanded-call-binding).

The IC guards the resolved callable/signature and applicable lookup validity and
records the adapter mode. Mode selection does not depend on keyword names.
General binding still validates dynamic tuple length and keyword contents unless
those facts are separately guarded. The fresh dict may be consumed by deleting
bound keys, leaving unmatched entries for direct handoff to `**kwargs`; choosing
that strategy versus preserving transport inputs remains open.

### Optional general-binding plan cache

General binding constructs a temporary per-call plan, validates it, then applies
it. The plan records ordered keyword names, parameter or collector destinations,
positional count, and adaptation/default placement. It is not built directly
into the live IC. Installing a successfully validated plan in the binding-plan
cache is optional and separate from applying it. Cached and temporary plans use
the same application path. Callable/signature resolution and adapter-mode
caching remain independent of keyword binding-plan eligibility.

Only calls whose incoming keyword keys are all exact builtin strings are
eligible to construct or hit a cached binding plan. Calls containing a
string-subclass key run the ordered binder afresh, including rich comparisons
and their side effects, then apply the temporary plan without installing it.
They bypass the binding-plan cache without evicting an existing exact-string
plan. Key identity or subclass-shape guards cannot justify skipping comparisons:
even the same key object may have changing comparison results or observable
effects on each call. Retain installed name references through the code object's
cache ownership and tracing machinery.

A plan hit requires exact-string incoming keys, the callable and valid lookup
dependencies, the same positional and keyword counts, and `string_eq` for each
corresponding incoming and cached name. Values are irrelevant. Reordered names
may miss despite binding identically. Exact-string names need not be interned:
existing `string_eq`
accepts pointer identity, rejects distinct interned strings, and otherwise
compares contents. These native comparisons are only sufficient for exact-string
names; subclass names require the uncached rich-comparison binder.
The current explicit-keyword cache assumes constant names and cannot be reused
without this additional guard.

On a hit, use cached destinations instead of repeating name resolution and
conflict/required-parameter validation. Read defaults from current function
storage rather than freezing default values. The tuple/dict must remain intact
for all guard and source reads needed before any destructive adaptation.

The comparison is guard plus plan application versus cold plan construction
plus the same application. With K keywords and P keyword-bindable parameters,
an ordered-name guard uses K comparisons; search-based construction may use
K*P parameter comparisons plus K*(K-1)/2 duplicate comparisons for a source
representation that retains duplicates, and required/default checks. Dictionary
transport has already rejected keyword-source duplicates. Stable names are a
promising cache case, not a measured guarantee. Measure reused key objects,
new equal strings, and changing names: changing names pay a failed guard before
binding. Collector allocation/packing is common work, not eliminated by the
cache. The additional plan cache remains a design option within general mode.

### JIT boundary and unresolved details

Tuples and dicts are ordinary values; indexed accesses operate on native
container memory rather than logical bytecode registers. The IC-guarded target
signature identifies destination parameter slots. Pure collection has two clear
assignments; general binding may initially be a guarded adapter operation with
explicit parameter-window effects instead of a fully static move sequence.

Operand widths and byte encoding, generic protocol continuations, tuple-suffix packing,
dict consumption, ownership transfer, rooting across callbacks, failure cleanup,
frame placement, and JIT lowering/exception exits still need coherent sketches.
The current direction does not authorize implementation of these open details.

## Alternatives Considered and Tradeoffs

### Managed argument area

Expand positional values and parallel keyword-name/value spans into an
addressable managed area, then bind using runtime counts. Its main attraction
is avoiding transport container allocation for mixed calls into named
signatures. Exact tuples can copy references directly; other sources iterate.
Collectors then pack positional leftovers and unmatched keywords into containers.

The area must be outside the logical bytecode register file. The JIT tracks
statically identified slots as individual SSA values; forcing indexed registers
onto the stack does not resolve which values an indexed store replaces. Ordinary
registers would hold an area handle/descriptor, counts, and expansion operands;
area accesses would be explicit memory operations.

Its representation remains unclear: the logical spans do not require three
adjacent physical arrays. Allocation/growth, live extent, stack capacity,
ownership/rooting, nested callbacks, cleanup, JIT memory effects, side exits,
and transfer into the callee window need a new contract. Managed-stack storage
is possible, not settled. Operator calls might share the area for arguments and
continuation state, but their fixed scratch prefix is only an existing pattern
for comparison, not proof that a dynamic area already exists. Unification is
an option, not a required migration.

A register-span binder could retain duplicate keyword entries until final
binding. Early duplicate detection was accepted as best effort for that option:
final-adapter detection may allow later callbacks or another error first, but
must still reject duplicates before callee entry. Any implemented timing
deviation would need documentation and tests. This latitude does not relax
general argument evaluation or expansion sequencing.

### Hybrid forwarding containers and mixed-source area

Normalize exactly `f(*args, **kwargs)` into an exact tuple and fresh dict, but
use the managed area for mixed sources. This preserves pure collector handoff
while avoiding some allocations for mixed ordinary-signature calls. It requires
two binding input forms plus the managed-area contract. They might share
signature validation, destination plans, and parameter placement, but that
sharing has not been designed. Container-input general binding reads native
storage without repeating user iteration.

### Uniform heap transport: accepted tradeoff of the current sketch

Container precode uses existing ownership, rooting, and addressable storage,
removes the managed-area subsystem, and gives every expanded call one adapter
boundary. Exact tuple reuse and fresh-dict collector handoff favor forwarding
chains. Mixed positional calls may allocate both a list builder and a final
tuple; named signatures then copy contents into parameter slots, and keyword
transport may allocate even when no kwargs collector is needed.

The tradeoff is allocation, reference copying, and temporary garbage against
storage/lifetime/JIT complexity. Compare regular-signature calls, mixed
expansions, and forwarding chains before considering another storage form.
Precode remains separate from binding, so using containers does not force one
large opcode to perform all expansion and call work.

## Keyword Call Bytecode

The implemented bytecode shape keeps value ranges as values only. Keyword names
are carried out-of-band as a constant tuple of interned strings:

```text
CallKeyword callable, first_pos_arg, n_pos_args, first_kw_value, n_kw_args,
            kw_names_const, call_ic
```

The positional range is staged in an ABI-aligned temporary call argument span
because those values are already in the right place for the eventual frame
entry:

```text
call_arg[0] = positional value 0
call_arg[1] = positional value 1
```

The keyword range is staged separately in ordinary temporary registers:

```text
r1 = keyword value 0
r2 = keyword value 1
```

`kw_names_const` names the keyword values starting at `first_kw_value`. The
keyword binder copies those values into the mapped parameter slots. Keeping
keyword sources outside the positional call argument span avoids source/destination
overlap when keyword order differs from parameter order. For example:

```python
f(10, c=30, b=20)
```

can be represented as:

```text
positional values = [10]
keyword values    = [30, 20]
kw_names          = ("c", "b")
```

The names tuple is a stable call-site shape. It avoids interleaving metadata
with values and gives `KeywordCallInlineCache` a compact guard key.

## Expanded Call Binding

Expanded calls consume the normalized `args_tuple` and fresh exact kwargs dict
from the adjacent register pair. The dict may use string-keyed or general
storage, retaining string-subclass key identity. Both are always present. They
should share the same callee signature semantics as ordinary
keyword calls, but they cannot reuse the existing keyword-call cache structure
directly because the keyword set is runtime data rather than a static names
tuple.

The positional phase copies values from `args_tuple` into positional formal
slots until either the tuple or the positional parameter range is exhausted.
Remaining tuple items are an error unless the callee has `*args`; with
`*args`, the callee receives a tuple containing the remaining positional suffix.
For the pure collector case `def f(*args, **kwargs)`, the normalized
`args_tuple` can be installed directly as the callee `*args` value.

Default handling follows the same slot-indexed default metadata used by
ordinary keyword calls. A positional or keyword value wins over a default. A
slot that is not filled by the positional phase and is not filled by the keyword
phase is satisfied from the default table if the corresponding
`default_presence_mask` bit is present; otherwise the missing argument is an
error. This applies to both positional-or-keyword parameters and keyword-only
parameters. Defaults are copied as references to the stored Python values, not
deep-copied.

The keyword phase visits incoming entries in insertion order, rather than
probing the transport dict once per parameter. Positional assignment first marks
its destination slots filled:

```text
for each (key, value) in incoming keyword insertion order:
  scan keyword-bindable parameters in declaration order for the first match
  if a parameter matches:
    if its slot is already filled:
      raise duplicate-value TypeError
    copy value into its slot
    mark the slot filled
  else:
    if the callee has no **kwargs collector:
      raise unexpected-keyword TypeError
    retain the original key and value as an unmatched keyword

after all keywords have been processed:
  fill defaults into unfilled parameter slots
  reject remaining missing required parameters
```

Exact builtin string keys can use native string comparison against parameter
names. String-subclass keys require normal rich equality, including subclass
dispatch, truth conversion, and exception propagation. Parameter matching does
not use the incoming key's hash. Distinct keys that coexist in the transport
dict can match the same parameter; the filled-slot check must reject the second
match. Comparison callbacks run in incoming keyword order and, within each
keyword, parameter declaration order. Missing-parameter checks happen only
after this keyword pass, not during parameter matching.

Only positional-or-keyword and keyword-only parameters are keyword targets.
Positional-only names do not participate in matching. An incoming keyword with
such a name is collected by `**kwargs` like any other unmatched keyword, or
rejected if there is no collector.

Unmatched entries retain their original key objects and relative insertion
order in the callee's `**kwargs` dict. Whether general binding builds a separate
collector dict or removes matched entries from the fresh, call-owned transport
dict remains open. Either strategy must preserve the matching and callback
order above; it must not replace parameter matching with ordinary dict lookup.

When an expanded call has no keyword sources, precode supplies a fresh empty
transport dict. A callee `**kwargs` collector can receive that dict directly;
frame entry needs no absent-keyword case or additional empty-dict allocation.
The keyword input must never be `None` or a shared mutable object.

The first useful fast path is the pure collector signature:

```python
def f(*args, **kwargs):
    ...
```

For that shape, expanded entry can install the normalized positional tuple and
the fresh kwargs transport dict directly. Precode has already allocated the
fresh dict even when the caller supplied no keyword sources; the adapter does
not allocate another one.

## Runtime Scope

The implemented keyword-call runtime supports:

- ordinary `Function` objects;
- eligible `ClassObject` calls that resolve to constructor thunks.

It does not attempt to support:

- keyword method-call lowering for `obj.method(name=value)`;
- arbitrary object `__call__`;
- descriptor execution beyond current non-keyword method-call support;
- caller `*args` or `**kwargs` expansion.

Unsupported callable shapes should remain explicit instead of being partially
simulated inside the keyword binder.

## Signature Keyword Layout

`CodeObject` owns compact signature metadata for keyword binding. It is closer
to a shape descriptor than to a Python `dict`:

```text
SignatureKeywordLayout:
  names:       [interned String value]
  param_slots: [parameter index or encoded frame offset]
```

Only keyword-bindable parameters belong in this layout:

- positional-or-keyword parameters;
- keyword-only parameters.

The `**kwargs` parameter is not keyword-bindable and must not appear in this
layout. It is the collector slot initialized by frame entry. An incoming
`kwargs=value` keyword is treated like any other explicit keyword: it binds a
real keyword-bindable parameter named `kwargs` if such a parameter exists, is
collected into `**kwargs` if no such parameter exists and the callee has
`**kwargs`, or is rejected otherwise.

Positional-only names should remain visible to diagnostics, but they are not
valid keyword targets and must not appear in the keyword remap. A keyword that
names a positional-only parameter is treated like any other unmatched keyword:
it is collected into `**kwargs` if the callee has a kwargs parameter, and is
otherwise rejected.

The cold binder linearly scans `names`. Parameter counts and keyword counts are
normally small, and interned names make comparison pointer/raw-value equality.
This avoids carrying a hash table for every function signature before evidence
says it is needed.

If large signatures become important, better function-global accelerators can be
added later, such as sorted arrays or a small custom open-addressed table.
Those should be implementation details behind the same signature metadata
contract.

## Default Slot Layout

The runtime stores defaults as a slot-indexed suffix that begins at the first
callee parameter slot with a default, instead of using separate positional and
keyword-only default containers. The common no-default case is represented by no
defaults tuple.

The representation needs:

```text
first_default_slot
default_values tuple
default_presence_mask
```

`first_default_slot` is the lowest callee parameter slot that has any default.
`default_values[0]` corresponds to that slot, and `default_values[i]`
corresponds to callee slot `first_default_slot + i`. Bit `i` of
`default_presence_mask` says whether that tuple entry is a real default.

`first_default_slot` belongs in the function signature metadata because it is a
property of the callee's formal parameter layout. The default values tuple
belongs on the `Function`, because default expressions are evaluated when the
function object is created.

The tuple must contain ordinary Python-visible values only. Placeholder entries
should use `None`, not VM sentinels such as `not_present`, because tuples are
normal heap objects and sentinel values would violate tuple storage invariants.
The mask, not the placeholder value, determines whether an entry can satisfy a
missing parameter during IC setup. Frame entry still copies the default tuple
blindly; keyword values and `*args` overwrite any placeholders that should not
survive. This matters because `None` is also a valid user default.

For example:

```python
def f(a, b=5, *args, c=6, d=7):
    ...
```

with callee slots:

```text
slot 0: a
slot 1: b
slot 2: *args
slot 3: c
slot 4: d
```

can lower to:

```text
first_default_slot = 1
default_values     = (5, None, 6, 7)
default_presence_mask = 1011
```

The `None` at tuple offset 1 is a placeholder for the `*args` slot. If a user
writes `b=None`, that entry is also `None`, but its mask bit is set and the
default can satisfy a missing argument.

Constructor thunks rebuild default metadata when removing initializer slot 0
(`self`). Each present initializer default shifts from `init_slot` to
`init_slot - 1`; defaults that belong to `self` are dropped. The thunk then
normalizes its own `first_default_slot`, defaults tuple, and presence mask from
the surviving defaults.

This keeps default validation and initialization uniform:

```text
if slot is missing and default_presence_mask has no corresponding bit:
  reject the call while building the IC

frame_slot[default_copy_start_slot + i] =
  default_values[(default_copy_start_slot - first_default_slot) + i]
```

The mask is cold-path validation metadata. Hot frame entry does not consult it:
after IC setup has accepted the call shape, frame entry copies a contiguous
suffix of the default tuple, stores `*args` if present, stores `**kwargs` if
present, then copies keyword arguments to their encoded destination registers or
inserts them into the `**kwargs` dictionary. Defaults that should not survive
are naturally overwritten.

## Call Adaptation Tiers

`FunctionCallAdaptation` should stay a compact path selector, not a matrix of
every possible signature feature combination. The intended tiers are:

```text
FixedArity
Defaultable
Full
```

`FixedArity` means frame entry can commit directly after arity/cache validation.
`Defaultable` means only default-value initialization may be needed before
entry.
`Full` means the callee may require default initialization, `*args` tuple
construction, and/or `**kwargs` dictionary construction.

The tiers are ordered by entry path complexity, not by exact signature contents.
A function with `**kwargs` but no defaults and no `*args` still uses `Full`,
because it needs a fresh dictionary in the kwargs slot. This avoids a growing
enum for combinations such as defaults-plus-kwargs or varargs-plus-kwargs. The
`Full` path checks the function signature to decide which work is actually
needed.

For positional calls, `**kwargs` never absorbs extra positional arguments.
Positional arity remains governed by the positional parameter count and `*args`.
If the callee has `**kwargs`, `CallPositional` initializes that slot with a
fresh empty dictionary after ordinary arity validation and before entering the
callee frame.

The `Full` path initializes `*args` and `**kwargs` late. It is acceptable for an
earlier default-copy step to write placeholder values into those slots because
the full adaptation step overwrites them before Python code can observe the
callee frame.

## Binding Algorithm

For a keyword-call cache miss in the implemented runtime subset, extended with
the accepted callee-`**kwargs` design:

1. Compute the callee frame pointer from the target code object's physical
   parameter slot count, as positional calls already do.
2. Build a local candidate `KeywordCallInlineCache` plan; do not mutate the
   live inline cache before validation succeeds.
3. Mark positional-or-keyword formal parameters supplied by position as filled.
4. Reject too many positional values unless the callee has `*args`.
5. For each supplied keyword name/value pair:
   - find a matching positional-or-keyword parameter by scanning the code
     object's keyword remap;
   - if a matching keyword-bindable parameter is found, reject if that formal
     slot was already filled by position or keyword, mark the formal slot
     filled, and store the encoded destination frame register in the candidate
     plan;
   - if no matching keyword-bindable parameter is found and the callee has
     `**kwargs`, store the kwargs-dictionary sentinel in the candidate plan;
   - if no matching keyword-bindable parameter is found and the callee does not
     have `**kwargs`, reject the unexpected keyword.
6. Reject any remaining required parameter whose default-presence bit is clear.
   The `*args` and `**kwargs` slots are initialized by frame entry and are not
   user-supplied required parameters.
7. Compute `default_fill_start_slot = max(n_filled_by_position,
   first_default_slot)`.
8. Commit the candidate plan to the live cache.
9. Apply the plan: copy the accepted default tuple suffix, store `*args` if
   present, store a fresh `**kwargs` dictionary if present, copy keyword values
   to their encoded destination registers or insert them into the kwargs
   dictionary, and enter the target function frame using the ordinary frame
   header and return machinery.

The parser already rejects duplicate explicit keyword names. Caller `*args` and
caller `**kwargs` are still deferred. Callee `**kwargs` only consumes unmatched
explicit `name=value` keywords in the existing call-site order. Those names are
the existing explicit keyword names, so the kwargs dictionary is string-only;
support for non-string key validation belongs to later caller `**mapping`
expansion.

## Keyword Call Inline Cache

`KeywordCallInlineCache` specializes a call site on both the callee and the
keyword-name tuple:

```text
guard:
  callee identity, or class plus constructor validity cell
  kw_names tuple identity
  n_pos_args

plan:
  keyword value index -> encoded destination frame register, or kwargs sentinel
  default_fill_start_slot
  FunctionCallAdaptation
  target code object
```

The cold path performs binding checks into a local candidate plan and only
commits it to the live cache after validation succeeds. Bad calls therefore do
not evict a previously valid cache entry.

The keyword destination vector stays compact as an `int8_t` array. Destination
value `0` is reserved as the kwargs-dictionary sentinel for this vector. Real
entries are callee-frame-relative encoded parameter destinations; `fp[0]` is
the previous-frame-pointer header slot, not a writable parameter destination.
Cache construction should assert that real keyword destination registers never
equal the sentinel, and frame entry must branch on the sentinel before writing
through the destination value.

The warm path avoids name lookup and semantic rediscovery. Positional values are
already in the temporary call argument span. The plan fills defaults from
`default_fill_start_slot` when the adaptation requires defaults, initializes
`*args` and/or `**kwargs` on the `Full` path, copies keyword values to
precomputed encoded frame registers, and inserts sentinel-routed keyword values
into the fresh kwargs dictionary. The sentinel-aware loop should only run on the
`Full` path; ordinary fixed/default-only keyword calls keep the direct copy loop.

## Constructor Thunks

Constructor thunks continue to mirror the selected `__init__` or `__new__`
signature with `self` or `cls` removed, including `**kwargs` metadata. Keyword
adaptation happens at the public class-call boundary before entering the thunk.
The public thunk signature preserves `HasKwArgs`; keyword remap entries are
copied with parameter indexes shifted down by one, while the kwargs collector
itself remains excluded from the remap.

The thunk body does not become a second keyword binder. It receives a prepared,
normalized frame and then enters the known initializer code object through the
existing internal `CallCodeObject` mechanism. The inner call is a prepared
positional-only forwarding call: the thunk has already normalized the public
call into the target parameter layout, including defaults, `*args`, and
`**kwargs`.

This matches the current constructor-thunk design: the ordinary class hot path
gets signature-aware specialization, while unusual construction remains a
future generic protocol path.

## Implementation Status

This is the implementation status for the keyword-call work. Completed stages
keep the VM in a coherent state and avoid silently implementing the deferred
starred-call or generic callable protocol work.

- [x] **Parser and AST signature grouping**

  The parser uses an explicit signature shape that preserves the CPython-style
  groups:
  positional-only, positional-or-keyword, optional `*args`, keyword-only, and
  optional `**kwargs`.

  It parses `/`, bare `*`, `*args`, keyword-only parameters, and `**kwargs`
  into distinct AST structure. Defaults remain attached to parameter nodes for
  now.

- [x] **Codegen and function signature metadata**

  `CodeObject` and `Function` metadata now go beyond
  `n_positional_parameters` plus `HasVarArgs`. The metadata describes grouped
  parameter counts, varargs/kwargs flags, `first_default_slot`,
  `default_presence_mask`, and the compact keyword-bindable layout. `Function`
  keeps the hot call signature copy, while keyword remap metadata stays on
  `CodeObject`.

  Constructor-thunk generation mirrors the public constructor signature with
  `self` removed for the currently supported runtime subset.

- [x] **Call argument AST and keyword-call bytecode**

  Calls use explicit call-argument AST nodes for positional and keyword
  arguments. Caller `*args` and `**kwargs` are rejected until their separate
  runtime slice is designed.

  Keyword calls lower to:

  ```text
  CallKeyword callable, first_pos_arg, n_pos_args, first_kw_value, n_kw_args,
              kw_names_const, call_ic
  ```

  Codegen continues to emit `CallPositional` for calls with no keywords. For calls
  with explicit keywords, it evaluates argument values left to right while
  staging positional values into an aligned temporary call argument span and
  keyword values into a separate contiguous temporary-register span. Keyword
  names are stored as a
  constant tuple of interned strings and used as the call-site shape key.

  Method-call lowering needs a parallel keyword-aware form or a clearly shared
  helper so direct method calls can bind `self` and then run the same keyword
  adaptation policy.

  Open design point: the current `CallMethodAttrPositional` opcode deliberately fuses
  attribute lookup, method binding, and `CallPositional`-style frame entry for the
  common `obj.method(...)` shape. Keyword calls could either add a matching
  fused method-keyword opcode, or split method lookup/binding from call entry so
  the same `CallKeyword` machinery can be reused. Splitting reduces opcode
  combinations, but the fused method call is a common hot shape. This should be
  decided with the interpreter hot path and call-cache shape in view.

- [x] **Runtime binder for ordinary functions and constructor thunks**

  The cold keyword-call path is implemented for ordinary `Function` objects and
  eligible constructor thunks. Direct method keyword calls are still rejected in
  codegen pending a method-call opcode/cache design.

  The binder builds a local candidate IC plan while validating, resolves each
  keyword by scanning the signature keyword layout, rejects duplicate formal
  fills, reports missing required parameters, and only commits the live cache
  entry after validation succeeds. Error messages are intentionally generic for
  now. For callees with `**kwargs`, unexpected explicit keywords are
  sentinel-routed kwargs-dictionary insertions.

- [x] **Keyword call inline-cache plan**

  `KeywordCallInlineCache` is a sibling cache state that specializes on callee
  identity, constructor validity where relevant, keyword-name tuple identity,
  and positional argument count.

  The cached plan stores encoded destination frame registers or the kwargs
  sentinel for each keyword value, `default_fill_start_slot`, and
  `FunctionCallAdaptation`. Cache misses build a local candidate plan and only
  commit it after validation succeeds.

- [x] **Semantics, regression tests, and benchmarks**

  Interpreter tests cover keyword calls to ordinary functions and eligible
  constructors, positional plus keyword mixing, keyword reordering, defaults,
  missing required arguments, unexpected keywords, duplicate formal fills, and
  varargs initialization. Parser tests cover repeated explicit keyword names.

  Codegen/disassembly tests pin down that `CallPositional` remains the no-keyword
  path and keyword calls carry a names tuple. Keyword call microbenchmarks cover
  all-keyword, mixed positional/keyword, and default-using keyword calls.

  Method keyword calls, caller `*args`, caller `**kwargs`, generic `__call__`,
  and descriptor-heavy callable behavior remain unsupported.

## Deferred Work

The following require separate design/implementation slices:

- keyword method calls, including whether to add a fused method-keyword opcode
  or split method lookup/binding from keyword call entry;
- caller `*args` expansion, including iterable protocol details and error
  ordering;
- caller `**kwargs` expansion, including mapping protocol details, duplicate
  detection, and merge order;
- arbitrary object `__call__`;
- descriptor-driven callables and bound method objects;
- richer TypeError wording compatible with CPython;
- native-to-managed keyword-call APIs;
- native variadic and keyword-aware intrinsic conventions.

Unsupported edges should remain explicit rather than adding partial fallback
machinery that obscures later semantics.
