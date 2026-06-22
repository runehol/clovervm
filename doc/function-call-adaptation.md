# Function Call Adaptation

This note records the implemented function-call adaptation model for Python
keyword calls and the remaining design work for richer signatures. It builds on
the low-level frame mechanics documented in
[CloverVM Function Calling Convention](function-calling-convention.md).

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

Call arguments use explicit AST nodes distinct from signature parameters. The
implemented runtime slice supports:

- positional argument: `f(value)`;
- keyword argument: `f(name=value)`.

Caller-side expansion is deliberately deferred:

- starred positional expansion: `f(*values)`;
- keyword mapping expansion: `f(**mapping)`;
- mixed repeated expansions.

The parser rejects repeated explicit keyword names at the call site with a
`SyntaxError`. The deferred expansion forms should get AST space when their
runtime semantics are designed.

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
