# clovervm TODO

These are the lowest-hanging improvements I found after reading the current tokenizer, parser, code generator, interpreter, tests, and build setup. The items are ordered roughly by payoff versus implementation cost.

## High value, low effort

- [x] Add negative tests for unsupported syntax and runtime failures.
  Why: the current test suite is almost entirely happy-path. A few small tests for `NameError`, non-callable calls, bad indentation, and parse failures would harden behavior quickly and make refactors safer.
  Where: `tests/test_interpreter.cpp`, `tests/test_parser.cpp`, `tests/test_tokenizer.cpp`.

- [x] Add interpreter tests for arithmetic edge cases, especially shifts and overflow boundaries.
  Why: `src/interpreter.cpp` already has `TODO` notes for left-shift overflow, and arithmetic ops depend on overflow intrinsics that are not being stress-tested.
  Good cases: negative shift counts, large left shifts, `-x`, add/sub/mul overflow edges, right shift of negative values.

- [x] Replace silent `-1` parser stubs with explicit "not implemented" errors.
  Why: several parser entry points currently return `-1` for constructs like `import`, `del`, `yield`, `class`, `for`, `try`, `with`, `global`, and `nonlocal`. That is brittle and makes failures harder to diagnose.
  Where: `src/parser.cpp`.

- [x] Improve parse error messages with token position/source context.
  Why: current parser errors usually say only "Expected token X, got Y" or "Unexpected token Y". Including the source offset or line/column would make debugging much faster.
  Where: `src/parser.cpp`, possibly `src/compilation_unit.h` if line/column helpers are needed.

- [x] Document the currently supported Python subset in `README.md`.
  Why: the README describes long-term goals, but not the syntax and semantics that actually work today. A short "currently supported" list would reduce confusion immediately.

## Nice wins in the parser/codegen path

- [x] Validate assignment targets before codegen.
  Why: the parser leaves a `TODO` about checking for a single assignment target, while codegen later rejects anything except simple variable assignment. Catching this earlier would produce clearer errors.
  Where: `src/parser.cpp`, `src/codegen.cpp`.

- [ ] Add parser/codegen/interpreter coverage for `if/elif/else` and loop `else`.
  Why: the parser and codegen both support these forms, but the existing tests barely exercise them.
  Where: `tests/test_parser.cpp`, `tests/test_codegen.cpp`, `tests/test_interpreter.cpp`.

- [ ] Add tests for function behavior beyond the recursive Fibonacci smoke test.
  Why: functions exist, but there is very little coverage for multiple parameters, implicit `return None`, local-vs-global lookup, and nested control flow inside functions.
  Where: `tests/test_interpreter.cpp`, `tests/test_codegen.cpp`.

- [ ] Store actual string literal values in the AST instead of `Value::None()`.
  Why: `atom()` creates string literal AST nodes but currently records `Value::None()` instead of the parsed string. That makes string support look further along than it really is.
  Where: `src/parser.cpp`.

## Tokenizer improvements that should be cheap

- [ ] Add tokenizer tests for number formats that the regex claims to support.
  Why: the tokenizer accepts decimal, hex, octal, binary, and underscores, but tests only cover simple decimal literals.
  Good cases: `0xff`, `0b1010`, `0o77`, `1_000_000`.

- [ ] Decide whether comments/blank lines should emit leading `NEWLINE` tokens and lock it down with tests.
  Why: there is already a regression-style tokenizer test around comments, which suggests this area is subtle and worth specifying clearly.
  Where: `src/tokenizer.cpp`, `tests/test_tokenizer.cpp`.

- [ ] Tighten string literal handling or explicitly mark it as intentionally minimal.
  Why: the current tokenizer regex for strings is very limited and does not support escapes or single quotes. Either improving it a bit or documenting the limitation would avoid surprising behavior.
  Where: `src/tokenizer.cpp`, `README.md`.

## Small runtime / engineering cleanups

- [ ] Turn `short_vector` bounds/type assertions into proper exceptions.
  Why: the file already has `TODO` comments for this. Converting assertion-only failures into runtime errors would make behavior safer outside debug-only scenarios.
  Where: `src/short_vector.h`.

- [ ] Add a small test helper layer to reduce repeated `VirtualMachine` setup in tests.
  Why: several tests manually compile/parse/run strings in nearly identical ways. A tiny shared helper would make adding new coverage much easier.
  Where: `tests/`.

- [ ] Add a simple benchmark or smoke target to track interpreter throughput over time.
  Why: there are benchmark scripts in `benchmark/`, but they are not integrated into the workflow. Even one documented benchmark command would help catch obvious slowdowns.

## Probably not "lowest-hanging", but worth keeping in view

- [ ] Fill out the incomplete Python grammar incrementally: collections, attribute access, subscripting, richer call syntax, and more statements.
- [ ] Implement real slow paths for non-SMI arithmetic/comparisons instead of generic exceptions.
- [ ] Scan function bodies for locals before codegen so local slot allocation is more complete and less ad hoc.
- [ ] Revisit memory/container placeholders like the comments saying some structures "need to be CL arrays at some point".
