# clovervm - Clover Virtual Machine

A Python VM experiment with a bytecode interpreter, shape-based objects, and
an opt-in AArch64 baseline JIT. Very much work-in-progress: the Python language
and standard library are incomplete, and CPython compatibility is not guaranteed.

## Runtime design

- V8-inspired register/accumulator bytecode.
- Tagged values keep small integers, booleans, and `None` allocation-free.
- Shape-based object layouts and inline caches for dynamic operations.
- Deferred refcounting and an optional AArch64 baseline JIT.

See the [architecture map](doc/architecture.md),
[semantic deviations](doc/python-deviations.md),
[standard-library coverage](doc/stdlib-module-bringup-checklist.md), and
[development priorities](doc/development-priorities.md) for details.

## Build and run

Requires Clang with C++20 support, CMake, Ninja, and Python 3. Use Clang for the
interpreter's required tail-call support. From the repository root:

```sh
cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
ninja -C build-debug all

# Run a source file or a short command.
./build-debug/src/clovervm your_program.py
./build-debug/src/clovervm -c 'print(1 + 2)'

# Run the regular suite (includes focused reclamation tests).
ninja -C build-debug check

# Run the full suite, including broad reclamation stress tests.
ninja -C build-debug check-all
```

Tests in suites whose names start with `Heavy` are broad fixtures replayed with
forced reclamation. Small, focused lifetime and safepoint regressions stay in
the regular suite. Use the `Heavy` suite prefix for new broad stress replays,
including parameterized fixture classes.

CTest labels expose the same two modes:

```sh
ctest --test-dir build-debug -L '^regular$' --output-on-failure
ctest --test-dir build-debug -L '^full$' --output-on-failure
```

Regular tests carry both labels; broad stress replays carry only `full`.
Unfiltered CTest and direct `test_clovervm` execution also run the full suite.
CI runs full once in every build configuration.

For a release build:

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
ninja -C build-release all
```

On native AArch64 builds, add `-DCLOVERVM_ENABLE_JIT=ON` to the CMake command to
enable JIT tiering. It is disabled by default.

## Benchmarking

Google Benchmark microbenchmarks compare CloverVM and CPython on the same
workloads. Run them with:

```sh
cmake --build build-release --target run_benchmark
```

Snapshot measured on October 10, 2026 at commit `4371ac51`, on an Apple M4
with 16 GB RAM and macOS 26.6.2. Release build with JIT disabled, compared
with CPython 3.14.5. These are single-run measurements;
[full suite output](benchmark/results/2026-10-10.txt) includes the remaining cases.

| Benchmark | Throughput | vs CPython 3.14.5 |
| --- | ---: | ---: |
| Recursive Fibonacci | 147M calls/s | 2.83x |
| While loop | 199M iters/s | 3.50x |
| For loop | 344M iters/s | 4.59x |
| Nested for loop | 256M iters/s | 6.16x |
| Class instantiation with `__init__` | 43.5M objects/s | 2.18x |
| Instance attribute read | 227M reads/s | 3.50x |
| Instance attribute write | 226M writes/s | 3.79x |
| Method call | 141M calls/s | 4.30x |
| Function default parameter | 130M calls/s | 3.86x |
| Pystone | 1.88M runs/s | 1.72x |
| Pystone arithmetic | 1.07M runs/s | 2.20x |
