# Optimization Ideas

This note collects interpreter optimization ideas. Some sections describe
implemented baseline machinery plus remaining follow-ups.

## CallSimple Inline Cache

`CallSimple` now has a positive, monomorphic inline cache for ordinary
`Function` calls and eligible constructor calls. The cache is keyed by exact
callable identity and the fixed positional argument count encoded in the
bytecode.

Current behavior:

- Positive results are cached; non-callable and wrong-arity misses are not.
- Plain `Function` calls cache the callable, code object, argument adaptation,
  and any lookup validity needed by the plan.
- Eligible `ClassObject` calls cache the class identity plus the constructor
  thunk returned by `ClassObject::get_or_create_constructor_thunk()`.
- Constructor cache entries use the class's existing MRO shape+contents
  validity cell. The class owns thunk creation and invalidation.

Useful follow-ups:

- Keep only the hottest plan kind in the main `op_call_simple` handler.
- Execute bulkier cached plans through a cold cached slow path.
- Add structural tests for the constructor-cache lowering and guard shape if
  those decisions become externally visible enough to pin down.

The implemented shape is roughly:

```cpp
enum class FunctionCallInlineCacheKind : uint8_t
{
    Empty,
    Function,
    Constructor,
};

struct FunctionCallInlineCache
{
    FunctionCallInlineCacheKind kind = FunctionCallInlineCacheKind::Empty;
    Object *guard_object = nullptr;
    Function *function = nullptr;
    CodeObject *code_object = nullptr;
    ValidityCell *validity_cell = nullptr;
    uint32_t n_args = 0;
    FunctionCallAdaptation adaptation = FunctionCallAdaptation::Invalid;
};
```

For ordinary functions, the hot path checks callable identity and enters the
cached function frame directly. Fixed-arity native thunks share that plan
because they are ordinary `Function` objects. A later `NativeThunkExactFrame`
plan could recognize tiny `CallNativeN; Return` code objects and jump to a
slimmer adapter when benchmarks show that the extra inline code pays for itself.

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
