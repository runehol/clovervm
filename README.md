# clovervm - Clover Virtual Machine
A Python VM experiment with a bytecode interpreter, shape-based objects, and
the runtime machinery needed for future JIT compilation. Very much
work-in-progress.

## Current status

`clovervm` can already parse, compile, and interpret a growing Python-like
subset. The project is still exploratory, so unsupported constructs are common
and some runtime behavior is intentionally narrow, but the runtime now includes
shape-based object layouts and inline caches for common dynamic operations.

## Language surface

The implemented subset is enough to run small Python programs with functions,
classes, inheritance, loops, conditionals, integer arithmetic, assertions,
`raise`, `try`/`except`, and native string/list/tuple/dict values. Exception
handlers support bare handlers, typed handlers, `except ... as e`, and bare
reraise within active handlers. Function calls support defaults and `*args`;
classes support ordinary `__init__`, explicit bases, `__bases__`, `__mro__`,
and C3 method resolution order. Attribute, method-call, function-call,
constructor-call, and common subscript operations are all exercised by the
interpreter tests and benchmarks.

## Known limitations

This is still a deliberately small Python subset. Large parts of the language
and standard library are absent, including imports, `with`, `finally`,
generators, comprehensions, broad descriptor and metaclass semantics, custom
`__new__`, complex assignment targets, richer string syntax, arbitrary-precision
integer arithmetic, full tracebacks, and generic iterator-protocol
`StopIteration` handling. Error messages and runtime semantics are improving,
but CPython compatibility is not yet a project guarantee.

## Runtime design

- Python 3 source subset.
- V8-inspired register/accumulator bytecode.
- Tagged value representation with integers, booleans, and `None` represented
  without indirection or allocation.
- Shape-based object layouts, including transition chains for instance and
  class attributes.
- Inline caches for attribute reads, attribute writes/deletes, function calls,
  direct method calls, and constructor calls. Attribute caches are guarded by
  receiver shapes and validity cells so cached inherited lookups observe class
  and MRO mutations.
- Deferred refcounting: stack values are borrowed, and heap/object stores are
  the places that retain references.
- Still planned: JIT compilation to native code for selected architectures,
  Python C API compatibility for extension support, and fast multithreading
  without a GIL.

## Benchmarking

The repository includes Google Benchmark-based microbenchmarks that run
clovervm and CPython on the same workloads. The suite reports normalized
throughput and a `vs_cpython` counter for quick comparison.

Representative output from a Mac Mini M4:

| Benchmark | Throughput | vs CPython 3.14.4 |
| --- | ---: | ---: |
| Recursive Fibonacci | 142M calls/s | 3.06x |
| While loop | 187M iters/s | 3.14x |
| For loop | 297M iters/s | 3.92x |
| For loop slow path | 292M iters/s | 3.90x |
| Nested for loop | 239M iters/s | 5.33x |
| Class instantiation, no `__init__` | 50.1M objects/s | 1.99x |
| Class instantiation with `__init__` | 36.8M objects/s | 1.85x |
| Instance attribute add in constructor | 27.7M attrs/s | 2.02x |
| Instance attribute add after construction | 31.7M attrs/s | 2.04x |
| Instance attribute read | 204M reads/s | 3.13x |
| Class attribute read | 161M reads/s | 2.62x |
| Instance attribute write | 206M writes/s | 3.39x |
| Class attribute write | 111M writes/s | 5.07x |
| Method call | 127M calls/s | 3.56x |
| Function default parameter | 121M calls/s | 3.41x |
| Function varargs | 36.3M calls/s | 1.37x |
| Function varargs with positional args | 39.9M calls/s | 1.37x |
| Function default varargs | 53.0M calls/s | 1.57x |
| Method call with class attribute write | 77.1M calls/s | 5.24x |
| Pystone lite | 4.22M runs/s | 2.27x |
| Pystone arithmetic | 1.11M runs/s | 2.29x |
