# Function Call Adaptation

This note sketches the planned function-call adaptation model for Python
keyword calls and richer signatures. It builds on the low-level frame mechanics
documented in [CloverVM Function Calling Convention](function-calling-convention.md).

The goal is to add Python's richer call binding semantics without turning the
existing positional `CallSimple` hot path into a broad generic slow path.

## Current State

The implemented call path is positional:

- callers lay out positional values in a contiguous outgoing register window;
- `CallSimple`, `CallMethodAttr`, and constructor thunks enter ordinary
  `Function` objects through the same frame convention;
- `Function` metadata tracks minimum and maximum positional arity;
- entry adaptation supports fixed arity, positional defaults, and callee
  `*args`;
- parser/codegen do not represent caller keywords, caller `*args`, caller
  `**kwargs`, callee keyword-only parameters, bare `*`, callee `**kwargs`, or
  positional-only `/`.

The first richer-call slice should extend this model for ordinary functions and
eligible constructor thunks. It should not introduce the full Python callable
protocol. In particular, descriptor dispatch and arbitrary object `__call__`
remain separate object-model work.

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
style, but codegen should be able to derive two runtime default groups:

- trailing defaults for positional-or-keyword parameters;
- per-parameter defaults for keyword-only parameters, with a clear required
  marker for missing defaults.

## Call Argument Shape

Call arguments should get explicit AST nodes distinct from signature
parameters. The initial runtime slice needs:

- positional argument: `f(value)`;
- keyword argument: `f(name=value)`.

Caller-side expansion is deliberately deferred:

- starred positional expansion: `f(*values)`;
- keyword mapping expansion: `f(**mapping)`;
- mixed repeated expansions.

The deferred forms should have AST space reserved when convenient, but they do
not need runtime support in the first keyword-call slice.

## Keyword Call Bytecode

The first bytecode shape should keep the outgoing value window as values only.
Keyword names should be carried out-of-band as a constant tuple of interned
strings:

```text
CallKeyword callable, first_arg, n_pos_args, n_kw_args, kw_names_const, call_ic
```

The outgoing register window is still contiguous:

```text
a0 = positional value 0
a1 = positional value 1
a2 = keyword value 0
a3 = keyword value 1
```

`kw_names_const` names the keyword values starting at
`a[n_pos_args]`. For example:

```python
f(10, c=30, b=20)
```

can be represented as:

```text
values   = [10, 30, 20]
kw_names = ("c", "b")
```

The names tuple is a stable call-site shape. It avoids interleaving metadata
with values and gives inline caches a compact guard key.

## Runtime Scope

The initial keyword-call runtime should support:

- ordinary `Function` objects;
- eligible `ClassObject` calls that resolve to constructor thunks;
- direct method calls whose existing method-call resolution produces a
  `Function`.

It should not attempt to support:

- arbitrary object `__call__`;
- descriptor execution that is not already handled by the current method-call
  path;
- caller `*args` or `**kwargs` expansion.

Unsupported callable shapes should remain explicit instead of being partially
simulated inside the keyword binder.

## Signature Keyword Layout

The function object or its code object needs compact signature metadata for
keyword binding. This should be closer to a shape descriptor than to a Python
`dict`.

A simple first representation is:

```text
SignatureKeywordLayout:
  names:       [interned String value]
  param_slots: [parameter index or encoded frame offset]
```

Only keyword-bindable parameters belong in this layout:

- positional-or-keyword parameters;
- keyword-only parameters.

Positional-only names should remain visible to diagnostics and duplicate checks,
but they are not valid keyword targets. A keyword that names a positional-only
parameter must raise `TypeError`.

The uncached binder can linearly scan `names`. Parameter counts and keyword
counts are normally small, and interned names make comparison pointer/raw-value
equality. This avoids carrying a hash table for every function signature before
evidence says it is needed.

If large signatures become important, better function-global accelerators can be
added later, such as sorted arrays or a small custom open-addressed table.
Those should be implementation details behind the same signature metadata
contract.

## Binding Algorithm

For an uncached keyword call:

1. Compute the callee frame pointer from the target code object's physical
   parameter slot count, as positional calls already do.
2. Mark all formal parameters as unfilled in a local binder state.
3. Copy positional values into formal parameter slots in order.
4. Reject too many positional values unless the callee has `*args`.
5. For each supplied keyword name/value pair:
   - reject duplicate supplied keyword names at the call site;
   - reject a keyword that names a positional-only parameter;
   - find a matching positional-or-keyword or keyword-only parameter;
   - reject if that formal slot was already filled by position or keyword;
   - write the value into the resolved frame slot and mark it filled;
   - if no formal matches and `**kwargs` exists, add the entry there;
   - otherwise raise unexpected-keyword `TypeError`.
6. Fill missing positional defaults.
7. Fill missing keyword-only defaults.
8. Reject any remaining required parameters.
9. Initialize `*args` to a tuple of extra positional values, or an empty tuple
   if the varargs slot exists and no extra values were supplied.
10. Initialize `**kwargs` to a dict of unconsumed keyword values, or an empty
    dict if the kwargs slot exists and no extra keywords were supplied.
11. Enter the target function frame using the ordinary frame header and return
    machinery.

For the first implementation slice, steps involving caller `*args` and
`**kwargs` expansion can remain absent because the call site provides only
explicit positional and keyword values.

## Keyword Call Inline Cache

The keyword call inline cache should specialize a call site on both the callee
and the keyword-name tuple:

```text
guard:
  callee identity, or class plus constructor validity cell
  kw_names tuple identity
  n_pos_args

plan:
  keyword value index -> parameter slot
  default-fill obligations
  varargs/kwargs initialization shape
  target code object
```

The cold path performs full Python binding checks and populates the plan. The
warm path should avoid name lookup and semantic rediscovery. It should mostly
copy positional values, move keyword values to precomputed slots, fill known
defaults, initialize varargs/kwargs slots when needed, and enter the frame.

The plan may initially store logical parameter indexes because they are easier
to inspect while the metadata is still evolving. A later fast-path-oriented
version can store encoded frame offsets directly.

## Constructor Thunks

Constructor thunks should continue to mirror the selected `__init__` signature
with `self` removed. Keyword adaptation happens at the public call boundary
before entering the thunk.

The thunk body should not become a second keyword binder. It should receive a
prepared, normalized frame and then enter the known initializer code object
through the existing internal `CallCodeObject` mechanism.

This matches the current constructor-thunk design: the ordinary class hot path
gets signature-aware specialization, while unusual construction remains a
future generic protocol path.

## Implementation Checklist

This is the intended first implementation path. Each stage should leave the VM
in a coherent state and avoid silently implementing the deferred starred-call or
generic callable protocol work.

- [ ] **Parser and AST signature grouping**

  Replace the current flat function-parameter sequence with an explicit
  signature shape that preserves the CPython-style groups:
  positional-only, positional-or-keyword, optional `*args`, keyword-only, and
  optional `**kwargs`.

  This stage should parse `/`, bare `*`, `*args`, keyword-only parameters, and
  `**kwargs` into distinct AST structure. It should keep existing simple
  function definitions rendering and compiling while making unsupported runtime
  cases explicit. Defaults can stay attached to parameter nodes, but the grouped
  AST must make it straightforward for codegen to derive positional defaults and
  keyword-only defaults separately.

  Parser tests should cover ordinary parameters, trailing commas, `/`, bare
  `*`, `*args`, keyword-only required/default parameters, `**kwargs`, and syntax
  errors for invalid ordering or repeated separators.

- [ ] **Codegen and function signature metadata**

  Extend `CodeObject` and `Function` metadata beyond
  `n_positional_parameters` plus `HasVarArgs`. The metadata should describe:
  positional-only count, positional-or-keyword count, keyword-only count,
  varargs/kwargs presence, parameter names, positional defaults, keyword-only
  defaults, and the compact keyword-bindable layout.

  Existing positional-only-at-runtime behavior should keep using the current
  fast path when a function has no richer signature features. The new metadata
  should be cheap to inspect from the interpreter and precise enough for the
  binder to answer whether a name is positional-only, keyword-bindable,
  required, or defaulted.

  This stage should update constructor-thunk generation to preserve the richer
  public signature shape with `self` removed, but the thunk body should still
  assume it receives a normalized frame.

- [ ] **Call argument AST and keyword-call bytecode**

  Add call-argument AST nodes for explicit positional and explicit keyword
  arguments. Caller `*args` and `**kwargs` may get placeholder AST forms if that
  simplifies the grammar, but codegen should reject them until their separate
  runtime slice is designed.

  Add a keyword-call bytecode shape along these lines:

  ```text
  CallKeyword callable, first_arg, n_pos_args, n_kw_args, kw_names_const, call_ic
  ```

  Codegen should continue to emit `CallSimple` for calls with no keywords. For
  calls with explicit keywords, it should evaluate argument values left to right
  into one contiguous outgoing value window and store keyword names as a
  constant tuple of interned strings. The keyword tuple should be the call-site
  shape key used by the eventual inline cache.

  Method-call lowering needs a parallel keyword-aware form or a clearly shared
  helper so direct method calls can bind `self` and then run the same keyword
  adaptation policy.

  Open design point: the current `CallMethodAttr` opcode deliberately fuses
  attribute lookup, method binding, and `CallSimple`-style frame entry for the
  common `obj.method(...)` shape. Keyword calls could either add a matching
  fused method-keyword opcode, or split method lookup/binding from call entry so
  the same `CallKeyword` machinery can be reused. Splitting reduces opcode
  combinations, but the fused method call is a common hot shape. This should be
  decided with the interpreter hot path and call-cache shape in view.

- [ ] **Uncached runtime binder**

  Implement the cold keyword-call path for ordinary `Function` objects,
  eligible constructor thunks, and direct method calls that resolve to a
  `Function`. It should not attempt arbitrary object `__call__` or descriptor
  execution beyond what the current method-call path already supports.

  The binder should prepare the callee frame, copy positional arguments, resolve
  each keyword by scanning the signature keyword layout, reject duplicate
  fills, reject positional-only names used as keywords, reject unexpected
  keywords without `**kwargs`, fill positional and keyword-only defaults, build
  `*args`/`**kwargs` slots when those formal parameters exist, and report
  missing required parameters.

  Fallibility should follow the interpreter's pending-exception conventions.
  The semantic slow path may be cold, but it should still make allocation,
  exception, and frame-entry behavior explicit.

- [ ] **Keyword call inline-cache plan**

  Extend function-call cache state, or add a sibling keyword-call cache state,
  that specializes on callee identity, constructor validity where relevant,
  keyword-name tuple identity, and positional argument count.

  The cached plan should store the resolved mapping from keyword value index to
  parameter slot, plus enough default-fill and varargs/kwargs initialization
  information for the warm path to avoid name lookup and semantic
  rediscovery. It is acceptable for the first plan to store logical parameter
  indexes; a later optimization can store encoded frame offsets directly.

  Cache misses should fall back to the uncached binder and only populate a plan
  after the full Python binding checks have succeeded.

- [ ] **Semantics and regression tests**

  Add interpreter tests for keyword calls to functions, methods, and eligible
  constructors. Cover positional plus keyword mixing, keyword reordering,
  defaults, keyword-only required/default parameters, positional-only rejection,
  duplicate arguments, missing required arguments, unexpected keywords, varargs
  interaction, and kwargs formal initialization once `**kwargs` on the callee is
  implemented.

  Add codegen/disassembly tests only where they pin down high-value structure:
  `CallSimple` remains the no-keyword path, keyword calls carry a names tuple,
  and method keyword calls preserve receiver binding. Keep broad behavior in
  interpreter tests.

  Update documentation for any intentionally unsupported edge that remains
  after the first slice, especially caller `*args`, caller `**kwargs`, generic
  `__call__`, and descriptor-heavy callable behavior.

## Deferred Work

The following require separate design/implementation slices:

- caller `*args` expansion, including iterable protocol details and error
  ordering;
- caller `**kwargs` expansion, including mapping protocol details, duplicate
  detection, and merge order;
- arbitrary object `__call__`;
- descriptor-driven callables and bound method objects;
- richer TypeError wording compatible with CPython;
- native-to-managed keyword-call APIs;
- native variadic and keyword-aware intrinsic conventions.

The first slice should leave clear unsupported edges rather than adding partial
fallback machinery that obscures later semantics.
