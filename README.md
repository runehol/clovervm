# clovervm - Clover Virtual Machine
Experiments with JIT compilation. Very much work-in-progress.

## Current status

`clovervm` can already parse, compile, and interpret a small Python-like
subset. The project is still exploratory, so unsupported constructs are common
and some runtime behavior is intentionally narrow.

## Currently supported subset

- Module-style execution of expressions and simple statement sequences.
- Integer, boolean, and `None` values.
- Names, assignment, and augmented assignment such as `+=`.
- Attribute access and assignment with `obj.name` and `obj.name = value`.
- Function definitions, parameter passing, calls, recursion, and `return`.
- Class definitions with executable class bodies and method definitions.
- Basic instance creation with `Cls()`.
- Direct method-call syntax such as `obj.method(...)`.
- `pass`.
- `if` / `elif` / `else` statements.
- `for` loops over `range(...)`, including `break`, `continue`, and loop
  `else`.
- `while` loops, including `break`, `continue`, and loop `else`.
- Arithmetic and bitwise operators:
  `+`, `-`, `*`, `/`, `//`, `%`, `**`, `<<`, `>>`, `|`, `&`, `^`, unary `+`,
  unary `-`, `~`.
- Boolean operators and comparisons:
  `not`, `and`, `or`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `is`, `is not`, `in`,
  `not in`.
- Tuple syntax in the parser for comma-separated expression lists.
- Tokenization for names, indentation-sensitive blocks, and Python-style short
  string literals with single or double quotes, escape sequences, and `r` / `u`
  prefixes (no triple-quoted strings yet).
- Integer literal spellings including decimal, hexadecimal, octal, binary, and
  underscores.

## Known limitations

- Much of Python is not implemented yet, including `import`, `try`, `with`,
  `global`, `nonlocal`, `del`, and `yield`.
- Class support is still intentionally narrow:
  no inheritance semantics yet, no `__init__` calls during instantiation, no
  metaclass machinery, and no escaping bound-method objects.
- Assignment is currently much narrower than Python. In practice, codegen only
  supports simple variable targets cleanly.
- String support still does not include triple-quoted literals, bytes literals,
  f-strings, or implicit literal concatenation.
- Arithmetic currently focuses on tagged small integers. Overflow and several
  non-small-integer paths still raise generic VM exceptions instead of matching
  CPython behavior.
- Error messages and runtime semantics are improving, but compatibility is not
  yet a project guarantee.

## Planned design points

- Python 3 source language.
- V8-inspired register/accumulator byte code.
- Value representation with integers, bools and None represented without indirection and memory allocation.
- JIT compilation to native code for selected architectures.
- Type feedback for specialising the native code.
- Hidden classes to handle Python objects with C struct-like performance.
- Python C API compatibility for supporting the extension ecosystem.
- Memory management using deferred refcounting - only references on the heap are refcounted, not references on the stack. This means only stores into objects trigger refcounting.
- No GIL, fast multithreading.

## Benchmarking

The repository includes Google Benchmark-based microbenchmarks that run both
clovervm and CPython on the same workloads.

Build and run them with:

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target run_benchmark
```

These benchmarks load the source once and then measure repeated execution.
clovervm compiles once per benchmark case, and the CPython path keeps one
persistent `python3` subprocess alive per case so process startup is not on the
hot path. For the loop benchmarks, the normalized throughput signal is loop
iterations per second. For recursive Fibonacci, the benchmark normalizes by
recursive function calls per second so regressions are easier to compare over
time. The benchmark programs themselves live as external files under
`benchmark/`, so adjusting or adding workloads does not require editing the
harness logic. Benchmark numbers should be taken from a `Release` build rather
than `Debug`.
