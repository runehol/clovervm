# Optimization Ideas

| Field | Value |
|---|---|
| Document type | Investigation |
| Status | Experimental |
| Implementation | Candidate opcode unimplemented; scratch-ABI experiment rejected |
| Scope | Unimplemented interpreter optimization experiments and retained negative results |
| Owning layers | Codegen, interpreter, inline caches, runtime calls, and object protocols |
| Validated against | `ecf3faa9` (2026-10-24) |
| Supersedes | N/A |

This note records unimplemented optimization experiments and evidence against
repeating rejected experiments. Implemented call machinery is documented in
[Function Call Adaptation](function-call-adaptation.md).

## CallGlobalSimple Macro Opcode

A possible next experiment is a macro opcode for the common pattern:

```text
LdaGlobal name
Star r
CallPositional r, args
```

A fused opcode could look like:

```text
CallGlobalSimple const_idx, cache_idx, argc
```

The opcode would combine global lookup caching with the same call-plan machinery
used by `CallPositional`. The hot path for a stable global Python function can avoid
materializing the callable in a temporary register and skip two interpreter
dispatches.

The likely execution tiers are:

- Validate the cached global binding.
- Check or reuse the cached callable identity.
- Execute the call-site-specific plan.
- Fall back to a slow path that resolves the global, validates the call, and
  populates the positive cache entry.

The call-plan cache prerequisite is satisfied, but this fused opcode is not
implemented or selected as the next task. Measure the lookup/dispatch cost
before adding it; reuse the existing call-plan builder rather than introducing
another binder.

## AArch64 Scratch Argument Experiment

An experiment widened the threaded-dispatch ABI with three extra scratch words
so `op_call_positional` could pass decoded operands to `op_call_positional_slow` without
decoding the bytecode again. On AArch64 this stayed in registers (`x5`-`x7`) and
kept `op_call_positional` frame-free, but the benchmark result was mixed: a small
`BM_MethodCall` improvement did not justify regressions in broader call-heavy
benchmarks.

The conclusion was that extra dispatch ABI arguments are mechanically viable on
AArch64, but not attractive as a global optimization unless a future benchmark
shows a larger win.
