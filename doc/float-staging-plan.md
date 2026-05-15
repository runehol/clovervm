# Float Staging Plan

This document stages the implementation described in
[Float Design](float-design.md). The ordering is meant to keep each step
reviewable, testable, and honest about the `nbody` goal without letting float
work sprawl into constructors, imports, or a general numeric tower.

## Stage 1: Heap Float Type And Builtin Class

Status: complete.

Goal: create a Python-visible heap type that can exist in the VM, without
literal parsing or arithmetic yet.

Implementation pieces:

- [x] add `Float` in `src/float.h` and `src/float.cpp`;
- [x] store one `double value`;
- [x] include only the inherited `Object` value span;
- [x] use `CL_DECLARE_STATIC_OBJECT_SIZE(Float)`;
- [x] add `NativeLayoutId::Float`;
- [x] add `Float` to the native layout registry;
- [x] add `make_float_class` and `install_float_class_methods`;
- [x] add `VirtualMachine::float_class()`;
- [x] register the builtin class during VM bootstrap;
- [x] expose `float` in builtin scope through the existing builtin class loop.

Initial methods:

- [x] `float.__str__`;
- [x] `float.__repr__`.

Tests:

- [x] VM bootstrap exposes builtin `float`;
- [x] a directly allocated `Float` has native layout `Float`;
- [x] `__class__` of a float object is the VM's `float` class;
- [x] `repr` and `str` work for direct C++-created float objects.

Non-goals:

- no `float(...)`;
- no parser support;
- no arithmetic;
- no truthiness.

Validation:

- [x] `ninja -C build-debug all check`.

## Stage 2: Float Literal Tokenization And Parsing

Status: complete.

Goal: source literals can create heap `Float` constants.

Implementation pieces:

- [x] extend numeric tokenization to recognize Python float forms needed by
  `nbody`;
- [x] split numeric tokens into `INT_NUMBER` and `FLOAT_NUMBER`;
- [x] update parser literal handling to use token kind instead of reclassifying
  numeric text;
- [x] parse float literals with C++ conversion to `double`;
- [x] allocate `Float` constants;
- [x] keep integer literals on the existing SMI path;
- [x] keep codegen using `LdaConstant` for heap float constants.

Required literal forms:

- [x] `1.0`;
- [x] `1.`;
- [x] `.5`;
- [x] `1e3`;
- [x] `1E3`;
- [x] `1.2e-3`;
- [x] valid underscore forms if underscore support is extended in the same
  change.

Tests:

- [x] tokenizer recognizes the required forms as `FLOAT_NUMBER`;
- [x] parser/codegen can load representative float constants;
- [x] evaluating a float literal returns a `Float`;
- [x] `1` still returns an SMI;
- [x] invalid numeric boundaries are pinned where practical.

Non-goals:

- no arithmetic;
- no truthiness;
- no constructor support.

Validation:

- [x] `ninja -C build-debug all check`.

## Stage 3: Float Printing

Status: complete.

Goal: `print`, `str`, and `repr` are useful for float literals and test output.

This can be folded into Stage 1 if direct formatting is trivial, but it is a
separate stage if shortest-representation formatting needs iteration.

Implementation pieces:

- [x] implement stable compact formatting for finite values;
- [x] ensure integer-valued finite floats include `.0`;
- [x] preserve negative zero as `-0.0`;
- [x] print infinities and NaN as `inf`, `-inf`, and `nan`;
- [x] avoid `std::to_wstring` fixed-width output.

Tests:

- [x] `str(1.5) == "1.5"` through the float `__str__` method;
- [x] `repr(1.5) == "1.5"`;
- [x] float literal `print` output uses `__str__`;
- [x] `str(1.0) == "1.0"`;
- [x] `str(-0.0) == "-0.0"`;
- [x] representative exponent output if exponent formatting is implemented.

Non-goals:

- exact CPython formatting parity for every edge case;
- string `%` formatting;
- f-string format specs.

Validation:

- [x] `ninja -C build-debug all check`.

## Stage 4: Float Truthiness

Status: complete.

Goal: `if`, `while`, `and`, `or`, `not`, and `assert` can use float values.

Implementation pieces:

- [x] add a cold truthiness continuation behind pointer cases in `JumpIfTrue` and
  `JumpIfFalse`;
- [x] add the same float truthiness continuation behind pointer cases in `Not`;
- [x] recognize exact `Float`;
- [x] classify `value != 0.0` as true;
- [x] keep `0.0` false;
- [x] keep direct heap `Float(-0.0)` false by using double zero comparison;
- [x] tail-call onward for non-float pointer truthiness misses.

Tests:

- [x] `assert 1.0`;
- [x] `assert not 0.0`;
- [x] `if 1.0: ...`;
- [x] `if 0.0: ... else: ...`;
- [x] `while` with a float guard if that exposes a different path.

Non-goals:

- no general `__bool__`;
- no `__len__` truthiness protocol;
- no generic object truthiness changes beyond the existing behavior unless a
  separate object-protocol plan calls for it.

Validation:

- [x] `ninja -C build-debug all check`.

## Stage 5: Pure Float Arithmetic Tier

Status: complete.

Goal: add the semi-hot numeric continuation for float arithmetic while leaving
SMI fast paths clear.

Implementation pieces:

- [x] for `Add`, `Sub`, and `Mul`, keep all-SMI fast paths inline;
- [x] on non-SMI, tail-call an operator-specific numeric continuation;
- [x] add immediate-SMI continuations for `AddSmi`, `SubSmi`, and `MulSmi`;
- [x] in the regular binary continuation, accept only:
  - [x] `float op float`;
  - [x] `float op int`;
  - [x] `int op float`;
- [x] in the immediate-SMI continuation, accept only `float op immediate-int`;
- [x] promote accepted operands to `double`;
- [x] compute as pure double arithmetic;
- [x] return a new heap `Float`;
- [x] tail-call onward on numeric miss.

Unary:

- [x] unary `-` accepts `Float` and returns `Float`;
- [x] unary `+` accepts `Float` and returns `Float`.

Tests:

- [x] float/float arithmetic for `+`, `-`, `*`;
- [x] int/float arithmetic for `+`, `-`, `*`;
- [x] float/int arithmetic for `+`, `-`, `*`;
- [x] unary `-1.5`;
- [x] unary `+1.5`;
- [x] `assert not -0.0` once source-level float unary negation exists;
- [x] all-SMI integer arithmetic still returns SMI where it did before.

Non-goals:

- no `//`;
- no `%`;
- no `**`;
- no bitwise operators;
- no constructor conversion.

Validation:

- [x] `ninja -C build-debug all check`;
- consider a focused microbenchmark only after correctness lands.

## Stage 6: True Division

Status: complete.

Goal: implement Python 3 `/`.

Implementation pieces:

- [x] implement `Div` as true division;
- [x] do not define a `DivSmi` opcode; the operator table lowers `/` only to
  `Div`;
- [x] return `Float` for all accepted numeric combinations, including
  `int / int`;
- [x] accept:
  - [x] `int / int`;
  - [x] `float / float`;
  - [x] `float / int`;
  - [x] `int / float`;
- [x] promote both operands to `double`;
- [x] detect zero divisor before computing;
- [x] add builtin `ZeroDivisionError` if missing;
- [x] raise `ZeroDivisionError` for numeric division by zero;
- [x] report unsupported operand kinds as `TypeError`.

Tests:

- [x] `1 / 2 == 0.5`;
- [x] `1.0 / 2 == 0.5`;
- [x] `1 / 2.0 == 0.5`;
- [x] `1.0 / 2.0 == 0.5`;
- [x] division by integer zero;
- [x] division by float positive zero;
- [x] division by float negative zero;
- [x] unsupported division reports a Python exception rather than generic C++
  failure.

Non-goals:

- no floor division;
- no modulo;
- no conversion constructors.

Validation:

- [x] `ninja -C build-debug all check`.

## Stage 7: Float Comparisons

Goal: make float values usable in numeric guards and benchmark computations.

Implementation pieces:

- add semi-hot numeric comparison continuations for:
  - `<`;
  - `<=`;
  - `>`;
  - `>=`;
  - `==`;
  - `!=`;
- accept:
  - `float cmp float`;
  - `float cmp int`;
  - `int cmp float`;
- promote operands to `double`;
- return `True` or `False`;
- preserve existing SMI/bool equality behavior on the current fast path;
- tail-call onward for comparison misses.

Tests:

- each comparison for float/float;
- each comparison for int/float;
- each comparison for float/int;
- equality around `0.0` and `-0.0`;
- NaN comparison behavior if a NaN source exists by this point.

Non-goals:

- no full rich comparison protocol;
- no arbitrary object comparison behavior beyond existing paths.

Validation:

- `ninja -C build-debug all check`.

## Stage 8: Builtin `sqrt`

Goal: provide the primitive needed by adapted `nbody` without implementing
imports or the `math` module.

Implementation pieces:

- add builtin native function `sqrt`;
- accept `int` and `float`;
- promote to `double`;
- reject negative inputs with `ValueError`;
- return heap `Float`;
- expose `sqrt` in builtin scope.

Tests:

- `sqrt(4) == 2.0`;
- `sqrt(2.25) == 1.5`;
- `sqrt(0.0) == 0.0`;
- `sqrt(-1.0)` raises `ValueError`.

Non-goals:

- no `math.sqrt`;
- no import system;
- no broader math library.

Validation:

- `ninja -C build-debug all check`.

## Stage 9: Adapted NBody Benchmark

Goal: add a repo-local benchmark that exercises the float implementation.

Implementation pieces:

- add `benchmark/nbody.py`;
- add `benchmark/nbody.cpp` if maintaining C++ comparison remains useful;
- register `BM_NBody`;
- avoid imports, `sys.argv`, and formatting dependencies;
- call builtin `sqrt`;
- keep benchmark source shaped like existing `def run(n): ...` benchmarks.

Harness decision:

- decide whether `run(n)` should return a `Value`, return a trivial integer
  sentinel, or use benchmark-specific validation;
- do not let the existing integer-result harness constraint affect float
  runtime semantics.

Tests and validation:

- compile and run adapted `nbody` with a small `n`;
- compare against a known result if the harness supports float validation;
- run `ninja -C build-debug all check`;
- run release benchmark only after debug correctness is stable.

## Suggested Commit Boundaries

Prefer one commit per stage once tests pass. If a stage becomes too large, split
by substrate and behavior, for example:

- native layout and builtin class;
- literal parsing;
- printing;
- truthiness;
- arithmetic;
- division and `ZeroDivisionError`;
- comparisons;
- `sqrt`;
- benchmark.

Avoid mixing benchmark harness changes into core float semantics unless the core
tests already pass. That keeps regressions much easier to localize.

## Stop Points

Stop and revisit the design if implementation uncovers any of these:

- parser constant allocation cannot safely allocate heap objects at parse time;
- float constants need a different ownership model than strings/classes;
- pointer truthiness needs broader object semantics to avoid special-case debt;
- numeric miss behavior wants rich method dispatch earlier than planned;
- `nbody` requires unsupported syntax or runtime behavior beyond the float
  slice.

These are design questions, not details to paper over inside the first float
implementation.
