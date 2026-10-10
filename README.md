# clovervm - Clover Virtual Machine
A Python VM experiment with a bytecode interpreter, shape-based objects, and
an opt-in AArch64 baseline JIT. Very much work-in-progress.

## Current status

`clovervm` can already parse, compile, and interpret a growing Python-like
subset. The project is still exploratory, so unsupported constructs are common
and some runtime behavior is intentionally narrow, but the runtime now includes
shape-based object layouts and inline caches for common dynamic operations.
On native AArch64 builds, `CLOVERVM_ENABLE_JIT` enables synchronous tiering of
warmed functions into executable machine code. The current compiler covers a
deliberately narrow subset and exits back to the interpreter when an assumption
or unsupported operation is encountered.

## Language surface

The implemented subset is enough to run small Python programs with functions,
classes, inheritance, loops, conditionals, integer arithmetic, assertions,
exceptions, boxed floats, arbitrary-size integers, and native
string/bytes/list/tuple/dict values. Function calls support positional and
keyword defaults, positional-only and keyword-only parameters, and callee
`*args`/`**kwargs`; caller unpacking remains incomplete.

`try`/`except`/`else`/`finally` covers the current useful
exception-handling slice, including nonlocal `return`, `break`, and `continue`
through `finally`. Classes support ordinary `__init__`, explicit bases,
`__bases__`, `__mro__`, and C3 method resolution order. Attribute,
method-call, function-call, constructor-call, and common subscript operations
are all exercised by the interpreter tests and benchmarks. Source modules,
packages, relative imports, and native extension modules can be loaded through
the implemented import system. Integer literals and common arithmetic operations
promote beyond the tagged small-integer range to heap-allocated arbitrary-
precision integers.

## Known limitations

This is still a deliberately small Python subset. Large parts of the language
and standard library are absent, including generators, comprehensions, broad
descriptor and metaclass semantics, the full `__new__`/`__init__` construction
protocol, complex assignment targets, richer string syntax, full tracebacks,
range-object semantics, and `yield from` / generator iterator semantics. The
import system does not yet expose the full `importlib` protocol, and some
integer-consuming APIs remain limited to tagged small integers even though
general arithmetic supports arbitrary precision. Error messages and runtime
semantics are improving, but CPython compatibility is not yet a project
guarantee.

## Runtime design

- Python 3 source subset.
- V8-inspired register/accumulator bytecode.
- Tagged value representation with small integers, booleans, and `None`
  represented without indirection or allocation; larger integers use a heap
  `BigInt` representation while retaining Python-visible `int` semantics.
- Shape-based object layouts, including transition chains for instance and
  class attributes.
- Inline caches for attribute reads, attribute writes/deletes, function calls,
  direct method calls, and constructor calls. Attribute caches are guarded by
  receiver shapes and validity cells so cached inherited lookups observe class
  and MRO mutations.
- Deferred refcounting: stack values are borrowed, and heap/object stores are
  the places that retain references.
- An opt-in AArch64 JIT with SSA Core and Machine IR, register allocation,
  executable side exits, and guarded calls to selected trusted native handlers.
- Still planned: broader JIT operation and call coverage, Python C API
  compatibility for extension support, and fast multithreading without a GIL.

For the broader architecture map and links to the detailed design notes, see
[`doc/architecture.md`](doc/architecture.md).
Standard-library coverage and remaining compatibility gaps are tracked in
[`doc/stdlib-module-bringup-checklist.md`](doc/stdlib-module-bringup-checklist.md).

## Benchmarking

The repository includes Google Benchmark-based microbenchmarks that run
clovervm and CPython on the same workloads. The suite reports normalized
throughput and a `vs_cpython` counter for quick comparison. Benchmarks with a
barrier-protected C++ reference also report `vs_cpp`.

Snapshot measured on October 10, 2026 at commit `4371ac51`, on an Apple M4
with 16 GB RAM and macOS 26.6.2. Release build with JIT disabled, compared
with CPython 3.14.5.

Run the full suite with `cmake --build build-release --target run_benchmark`.
The target uses a one-second minimum per benchmark; comparison counters use
separate samples of at least 0.1 seconds. These are single-run measurements.
[Full suite output](benchmark/results/2026-10-10.txt).

| Benchmark | Throughput | vs CPython 3.14.5 |
| --- | ---: | ---: |
| Recursive Fibonacci | 147M calls/s | 2.83x |
| While loop | 199M iters/s | 3.50x |
| For loop | 344M iters/s | 4.59x |
| For loop slow path | 150M iters/s | 2.01x |
| Nested for loop | 256M iters/s | 6.16x |
| Class instantiation, no `__init__` | 66.3M objects/s | 2.60x |
| Class instantiation with `__init__` | 43.5M objects/s | 2.18x |
| Instance attribute add in constructor | 31.0M attrs/s | 2.36x |
| Instance attribute add after construction | 35.9M attrs/s | 2.37x |
| Instance attribute read | 227M reads/s | 3.50x |
| Class attribute read | 161M reads/s | 2.69x |
| Instance attribute write | 226M writes/s | 3.79x |
| Class attribute write | 111M writes/s | 4.88x |
| Method call | 141M calls/s | 4.30x |
| Function default parameter | 130M calls/s | 3.86x |
| Function varargs | 45.0M calls/s | 1.74x |
| Function varargs with positional args | 51.5M calls/s | 1.86x |
| Function default varargs | 56.0M calls/s | 1.72x |
| Method call with class attribute write | 74.1M calls/s | 5.27x |
| Pystone lite | 4.05M runs/s | 2.23x |
| Pystone | 1.88M runs/s | 1.72x |
| Pystone arithmetic | 1.07M runs/s | 2.20x |
