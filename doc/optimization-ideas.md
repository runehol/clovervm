# Optimization Ideas

This note collects interpreter optimization ideas that look promising but are
not implemented yet.

## CallSimple Inline Cache

Add a positive, monomorphic inline cache for `CallSimple`, keyed by exact
callable identity. The cache should describe a call-site-specific plan for the
fixed positional argument count encoded in the bytecode, not just the callable's
general type or arity.

The first version should focus on the common direct Python function case:

- Cache only positive results. Do not cache non-callable or wrong-arity misses.
- Key the cache by the callable object pointer, not by object shape.
- Build the cache entry in the slow path with the call site's `n_args`.
- Validate the argument count while building the plan.
- Keep only the hottest plan kind in the main `op_call_simple` handler.
- Execute bulkier cached plans through a cold cached slow path.

A possible shape:

```cpp
enum class CallSimplePlanKind : uint8_t
{
    Empty,
    PythonFunctionExactFrame,
    NativeThunkExactFrame,
    ClassObjectZeroArg,
};

struct CallSimpleInlineCache
{
    Object *callable = nullptr;
    CallSimplePlanKind kind = CallSimplePlanKind::Empty;

    CodeObject *code_object = nullptr;
};
```

For `PythonFunctionExactFrame`, the hot path can check callable identity and
enter the cached function frame directly. Fixed-arity native thunks can share
that plan at first because they are ordinary `Function` objects. A later
`NativeThunkExactFrame` plan could recognize tiny `CallNativeN; Return` code
objects and jump to a slimmer adapter when benchmarks show that the extra inline
code pays for itself.

Future Python call features such as default arguments, keyword arguments,
`*args`, and `**kwargs` should extend the plan model instead of turning
`CallSimple` into one generic mega-handler. For example, later plans could
describe how many defaults to fill or whether argument normalization is needed.

## CallGlobalSimple Macro Opcode

After `CallSimple` has a useful call-plan cache, add a macro opcode for the
common pattern:

```text
LdaGlobal name
Star r
CallSimple r, args
```

A fused opcode could look like:

```text
CallGlobalSimple const_idx, cache_idx, argc
```

The opcode would combine global lookup caching with the same call-plan machinery
used by `CallSimple`. The hot path for a stable global Python function can avoid
materializing the callable in a temporary register and skip two interpreter
dispatches.

The likely execution tiers are:

- Validate the cached global binding.
- Check or reuse the cached callable identity.
- Execute the call-site-specific plan.
- Fall back to a slow path that resolves the global, validates the call, and
  populates the positive cache entry.

This should be implemented after the `CallSimple` inline cache so the macro
opcode can reuse the call-plan builder and plan execution code.

## AArch64 Scratch Argument Experiment

An experiment widened the threaded-dispatch ABI with three extra scratch words
so `op_call_simple` could pass decoded operands to `op_call_simple_slow` without
decoding the bytecode again. On AArch64 this stayed in registers (`x5`-`x7`) and
kept `op_call_simple` frame-free, but the benchmark result was mixed: a small
`BM_MethodCall` improvement did not justify regressions in broader call-heavy
benchmarks.

The conclusion was that extra dispatch ABI arguments are mechanically viable on
AArch64, but not attractive as a global optimization unless a future benchmark
shows a larger win.
