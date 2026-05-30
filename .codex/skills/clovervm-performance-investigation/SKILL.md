---
name: clovervm-performance-investigation
description: Investigate, benchmark, or review clovervm performance changes. Use when a request involves interpreter hot paths, inline caches, dispatch overhead, call overhead, object layout, allocation cost, shape lookup, benchmark comparisons, profiling, regressions, or checking that hot opcode handlers still satisfy release-build frame and musttail constraints.
---

# clovervm Performance Investigation

Use this workflow to answer performance questions with measurements while preserving Python semantics and interpreter invariants.

## Workflow

1. Define the performance question.

Be specific about what is being measured:

- dispatch overhead
- attribute or item lookup
- inline cache hit/miss behavior
- function, method, constructor, varargs, or keyword call overhead
- allocation, reclamation, refcount, or ownership cost
- object shape, descriptor, or validity-cell lookup
- bytecode/codegen changes that affect interpreter work
- a suspected regression from a commit range

If the question is semantic correctness, use a semantics/design workflow first. Do not treat benchmark wins as valid if Python-visible behavior changed.

2. Establish correctness before trusting timing.

Run focused tests while iterating. For completed code changes, use the project-required debug verification unless the user explicitly scoped the task to measurement only:

```bash
ninja -C build-debug all check
```

For `src/interpreter.cpp`, preserve the opcode-handler shape and `MUSTTAIL` conventions. If a handler has custom control flow, keep instruction length handling explicit and avoid hiding slow semantics in inline helpers.

3. Use release builds for performance claims.

Do not benchmark debug builds for performance conclusions. If `build-release/` is missing, stale after dependency changes, or configured with the wrong generator/type, configure it with:

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
```

Build the benchmark target before running targeted benchmarks:

```bash
ninja -C build-release benchmark/bench_clovervm
```

4. Choose a targeted benchmark slice.

Prefer an existing focused benchmark over the full benchmark suite when it answers the question. Common examples:

```bash
build-release/benchmark/bench_clovervm --benchmark_filter=BM_InstanceAttributeRead --benchmark_min_time=20s --benchmark_counters_tabular=true
build-release/benchmark/bench_clovervm --benchmark_filter=BM_ForLoopSlowPath --benchmark_min_time=20s --benchmark_counters_tabular=true
build-release/benchmark/bench_clovervm --benchmark_filter=BM_FunctionVarargs --benchmark_min_time=20s --benchmark_counters_tabular=true
build-release/benchmark/bench_clovervm --benchmark_filter=BM_ClassInstantiationNoInit --benchmark_min_time=20s --benchmark_counters_tabular=true
```

Use the full suite when the change is broad or when the user asks for it:

```bash
cmake --build build-release --target run_benchmark
```

5. Compare against a baseline.

When possible, collect before/after numbers with the same command, build type, machine state, and minimum time. Use longer `--benchmark_min_time` or repeated runs for noisy microbenchmarks.

If no baseline is available, say so and report the result as an absolute measurement, not a speedup.

6. Use the hot-path checker for opcode-handler changes.

`tools/check_opcode_frames.py` verifies that release-build hot-path opcode handlers enter without setting up stack frames. It is wired into `benchmark/check_opcode_frames` and `benchmark/run_benchmark`.

Run it through CMake when touching hot interpreter handlers or changing frame-sensitive dispatch code:

```bash
cmake --build build-release --target check_opcode_frames
```

When debugging the script directly, pass the release `clovervm` binary and required handler list:

```bash
tools/check_opcode_frames.py build-release/clovervm benchmark/hot_path_opcode_handlers.txt
```

If you add or rename a hot-path opcode handler, update `benchmark/hot_path_opcode_handlers.txt` in the same change.

7. Profile when numbers need explanation.

If a benchmark regresses, improves unexpectedly, or does not isolate the cause, use sampling/profiling before guessing. On macOS, `sample` is useful for quick stack evidence:

```bash
build-release/benchmark/bench_clovervm --benchmark_filter=BM_Name --benchmark_min_time=30s --benchmark_counters_tabular=true
```

Run the benchmark long enough to sample it, then inspect whether time moved into:

- interpreter dispatch or frame setup
- allocation/reclamation/refcount work
- shape lookup, descriptor classification, or validity checks
- call argument normalization or tuple/dict construction
- generic slow paths, exception creation, or formatting
- benchmark harness overhead

8. Report conservatively.

Include:

- exact benchmark command
- baseline and after numbers when available
- percent change only when comparable
- noise caveats
- profiling evidence if used
- likely cause, clearly marked as inference when not directly proven
- correctness checks and hot-path checker results

Do not claim broad VM performance wins from one narrow benchmark. Do not claim a speedup from one short noisy run unless the delta is large and stable.

## Guardrails

- Correct Python semantics beat speed.
- Benchmark Release, debug-test correctness.
- Prefer targeted benchmarks before full-suite runs.
- Do not disable `ccache` or alter CMake flags to sidestep local setup issues.
- Keep interpreter hot-path helpers small and mechanical.
- Keep allocation, descriptor invocation, bytecode execution, exception creation, and formatting out of inline hot helpers unless there is measured justification and the semantics are explicit.
