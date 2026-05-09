# For Loop Support

This note records the current `for` loop implementation.

Current fast-iteration design work lives in
[iteration-plans.md](iteration-plans.md). Exception transport details live in
[exception-handling-transition-plan.md](exception-handling-transition-plan.md).

## Supported Surface

Implemented `for` loop behavior includes:

- `for <name> in <iterable>: ...`
- optional loop `else`
- `break` and `continue`
- nonlocal `return`, `break`, and `continue` through active `finally` blocks
- generic iterator protocol calls through `iter()` and `__next__`
- `StopIteration` consumption for generic loops through exception-table
  handlers
- optimized direct builtin `range(...)` loops

Loop targets are still intentionally narrow: the parser accepts simple variable
targets, while tuple and other complex targets remain future work.

## Generic Lowering

Generic `for` loops lower through the normal managed call machinery:

1. Evaluate the iterable.
2. Call `iter(iterable)` once.
3. Store the iterator in a temporary register.
4. At the loop header, call `iterator.__next__()`.
5. Protect the `__next__` call with an exception-table range.
6. If `StopIteration` is caught, clear it and jump to the loop `else` / exit
   target.
7. If any other exception is caught, reraise it.
8. Assign the yielded value to the loop target and run the body.
9. Route fallthrough and `continue` back to the loop header.
10. Route `break` past the optional `else` block.

This keeps ordinary iterator semantics in one path: user-defined iterators,
tuple/list iterators, and fallback range iteration all use the same
`iter()` / `__next__` protocol shape.

## Range Fast Path

Direct calls to the exact builtin `range` object can lower to specialized
bytecodes:

- `ForPrepRange1`
- `ForPrepRange2`
- `ForPrepRange3`
- `ForIterRange1`
- `ForIterRangeStep`

The prep opcodes guard that the resolved callable is still the builtin `range`.
If the guard fails, codegen falls back to the generic iterator-protocol path so
shadowing `range` preserves Python lookup behavior.

On the fast path, iteration state lives in registers instead of a public
iterator object. Exhaustion is an internal branch to the loop exit / `else`
target, not a Python-visible `StopIteration`.

The runtime still exposes `range()` as returning a `RangeIterator` directly.
That is not final Python semantics; a real reusable range object remains future
work.

## Runtime Pieces

The implementation is spread across:

- [src/parser.cpp](../src/parser.cpp) and [src/ast.h](../src/ast.h) for `for`
  syntax and AST shape
- [src/codegen.cpp](../src/codegen.cpp) for generic loop lowering, range fast
  paths, loop `else`, `break`, `continue`, and `finally` replay
- [src/bytecode.h](../src/bytecode.h) and
  [src/code_object_print.h](../src/code_object_print.h) for range-loop
  bytecodes and printing
- [src/interpreter.cpp](../src/interpreter.cpp) for range fast-path execution
  and managed exception-table unwinding
- [src/range_iterator.h](../src/range_iterator.h) and related runtime setup for
  the current public `range()` result

Native builtins, including `range`, are ordinary `Function` objects backed by
native thunk `CodeObject`s.

## Test Coverage

Parser coverage lives in [tests/test_parser.cpp](../tests/test_parser.cpp) and
covers basic `for`, loop `else`, simple variable targets, and rejection of
unsupported target shapes.

Codegen coverage lives in [tests/test_codegen.cpp](../tests/test_codegen.cpp)
and pins down:

- direct `range(...)` fast-path lowering
- generic fallback CFG from the specialized range path
- generic iterator-protocol calls for non-direct loops
- loop `else` and `break` layout

Interpreter coverage lives in
[tests/test_interpreter.cpp](../tests/test_interpreter.cpp) and the
self-checking Python files under [tests/python](../tests/python). It covers:

- summing ranges
- loop `else`
- `break` and `continue`
- nested loops
- one-, two-, and three-argument `range`
- negative-step ranges
- shadowed `range` fallback
- tuple and list iteration
- user-defined `__iter__` / `__next__`
- generic `for` discarding `StopIteration.value`
- propagation of non-`StopIteration` exceptions from `__next__`

## Remaining Work

Relevant follow-ups are now broader language/runtime work rather than
first-implementation `for` tasks:

- real range objects distinct from range iterators
- tuple and other destructuring loop targets
- generators and `yield`
- `yield from` and delegation over `StopIteration.value`
- full descriptor and metaclass behavior for arbitrary iterator methods
- iterator-plan specialization for ranges, containers, and other known iterable
  shapes
