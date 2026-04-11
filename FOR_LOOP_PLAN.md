# For Loop Support Plan

## Goal

Add pragmatic Python-style `for` loop support without first building full
Python generators, method dispatch, or exception handling. The initial target is
to support code like:

```python
total = 0
for x in range(5):
    total += x
total
```

The design should also set us up to add builtins such as `print` later through
the same callable mechanism.

## Guiding Approach

Use a builtin-function path plus iterator-oriented bytecode.

Instead of modeling the first version as a real Python generator whose `next()`
raises `StopIteration`, keep exhaustion as an internal iterator result handled
directly by the interpreter. This avoids blocking on method calls, `raise`,
`try`, and user-visible exception objects.

## Why Builtin Functions

Adding a `BuiltinFunction` runtime object is the cleanest way to support
`range`, and later `print`, without special-casing individual names all over
the VM.

Benefits:

- `range` becomes a normal callable value in the builtin scope.
- `print` can reuse the same mechanism later.
- `CallSimple` grows into a dispatch point for bytecode functions and native
  builtins.
- The scope model already has parent-scope hooks that fit builtin lookup well.

## Scope Of The First Version

Keep the first implementation intentionally narrow:

- Support `for <name> in range(<int>):`
- Support loop `else`
- Support `break`
- Support `continue`
- Support only simple variable targets
- Support only `range(stop)` with implicit `start=0`, `step=1`

This gives end-to-end `for` loop semantics with minimal surface area.

## Implementation Plan

### 1. Add AST and parser support for `for` [done]

Files:

- [src/ast.h](/Users/runehol/projects/clovervm/src/ast.h)
- [src/parser.cpp](/Users/runehol/projects/clovervm/src/parser.cpp)
- [src/ast_print.h](/Users/runehol/projects/clovervm/src/ast_print.h)

Changes:

- Add `STATEMENT_FOR` to the AST node kinds.
- Treat it as a statement in `is_expression()`.
- Replace the current `for_stmt()` placeholder with real parsing for:

```python
for <target> in <expression>:
    <block>
else:
    <block>
```

- Reuse existing assignment-target validation and keep the first version strict:
  the loop target must be a simple variable.
- Teach the AST printer to render `for` statements and optional loop `else`
  blocks.

Suggested child layout:

- child 0: target
- child 1: iterable expression
- child 2: body
- child 3: optional else block

Status:

- Implemented in [src/ast.h](/Users/runehol/projects/clovervm/src/ast.h),
  [src/parser.cpp](/Users/runehol/projects/clovervm/src/parser.cpp), and
  [src/ast_print.h](/Users/runehol/projects/clovervm/src/ast_print.h)
- Parser tests added in
  [tests/test_parser.cpp](/Users/runehol/projects/clovervm/tests/test_parser.cpp)
- Current behavior intentionally remains limited to simple variable targets

### 2. Introduce builtin-function runtime support [done]

Files:

- new runtime object header/source, likely alongside [src/function.h](/Users/runehol/projects/clovervm/src/function.h)
- [src/interpreter.cpp](/Users/runehol/projects/clovervm/src/interpreter.cpp)
- builtin-scope setup code, likely in thread or VM initialization

Changes:

- Add a `BuiltinFunction` object type that stores a native C++ callback and
  optional metadata such as arity.
- Extend `CallSimple` so it can dispatch to:
  - bytecode `Function`
  - native `BuiltinFunction`
- Keep the first builtin API small and purpose-built for the current call
  convention.

Suggested direction:

- Native callback accepts a lightweight `CallArguments` view over the current
  interpreter call slots.
- Builtins can advertise fixed arity, bounded multi-arity, or varargs.
- Callback returns a `Value` directly.
- Runtime errors can still use the current C++ exception strategy for now.

Status:

- Implemented in
  [src/builtin_function.h](/Users/runehol/projects/clovervm/src/builtin_function.h)
  and [src/interpreter.cpp](/Users/runehol/projects/clovervm/src/interpreter.cpp)
- `CallSimple` now dispatches to either bytecode `Function` or native
  `BuiltinFunction`
- The builtin calling interface uses `CallArguments` instead of copying values
  out of the interpreter stack
- Arity handling now supports exact arity, bounded multi-arity, and varargs
- Interpreter tests added in
  [tests/test_interpreter.cpp](/Users/runehol/projects/clovervm/tests/test_interpreter.cpp)

### 3. Add a builtin scope [done]

Files:

- scope setup path used by code generation/runtime startup
- [src/scope.h](/Users/runehol/projects/clovervm/src/scope.h)
- [src/scope.cpp](/Users/runehol/projects/clovervm/src/scope.cpp)
- potentially [src/codegen.cpp](/Users/runehol/projects/clovervm/src/codegen.cpp) depending on where root scopes are created

Changes:

- Introduce a builtin parent scope beneath module globals.
- Register `range` in that builtin scope.
- Preserve the existing fast-path-friendly parent lookup design.

Why this fits well:

- `Scope::register_slot_index_for_read()` already anticipates parent scopes and
  builtin-style fallback.
- This avoids hard-coding `range` as a special global name.

Status:

- Implemented.


### 4. Implement `range` as a builtin returning an iterator object

Files:

- new runtime object file for range iterator
- builtin registration site

Changes:

- Add a lightweight `RangeIterator` object with state such as:
  - current
  - stop
  - step
- For the first cut, only support `range(stop)`.
- The builtin `range` returns a `RangeIterator` directly.

Note:

This is intentionally more like “an iterator object produced by a builtin” than
“a full Python generator.” That is enough for `for` lowering and avoids the
need for `yield`.

### 5. Add iterator bytecodes

Files:

- [src/bytecode.h](/Users/runehol/projects/clovervm/src/bytecode.h)
- [src/code_object_print.h](/Users/runehol/projects/clovervm/src/code_object_print.h)
- [src/interpreter.cpp](/Users/runehol/projects/clovervm/src/interpreter.cpp)

Changes:

- Add `GetIter`
- Add `ForIter`

Suggested semantics:

- `GetIter`
  - input: iterable in accumulator
  - output: iterator in accumulator
- `ForIter <iter-reg>, <jump-target>`
  - advances the iterator stored in `iter-reg`
  - on success, leaves the next value in the accumulator
  - on exhaustion, jumps to the target

This makes iterator exhaustion a control-flow result instead of a general
exception.

### 6. Lower `for` loops in codegen

Files:

- [src/codegen.cpp](/Users/runehol/projects/clovervm/src/codegen.cpp)

Changes:

- Add a `STATEMENT_FOR` codegen path that mirrors the existing `while` lowering.
- Reuse the existing `loop_targets` stack so `break` and `continue` keep working
  uniformly across loop kinds.

Suggested lowering shape:

1. Evaluate iterable expression.
2. Emit `GetIter`.
3. Store iterator in a temporary register.
4. Mark loop-head target.
5. Emit `ForIter` on the iterator register, jumping to the loop-else or done
   target on exhaustion.
6. Assign accumulator to the loop variable.
7. Emit loop body.
8. Resolve `continue` back to the loop head.
9. Jump back to loop head.
10. Resolve exhaustion target.
11. Emit optional `else` block.
12. Resolve `break` target after the `else`.

Desired behavior:

- Normal exhaustion runs loop `else`.
- `break` skips loop `else`.
- `continue` advances the same iterator and resumes at the next iteration.

### 7. Implement iterator execution in the interpreter

Files:

- [src/interpreter.cpp](/Users/runehol/projects/clovervm/src/interpreter.cpp)

Changes:

- Add dispatch-table entries for `GetIter` and `ForIter`.
- Add runtime logic for `RangeIterator`.

Suggested behavior:

- `GetIter` verifies the accumulator is an iterable object that the runtime
  knows how to iterate.
- `ForIter` mutates iterator state in place.
- When a next value exists, place it in the accumulator and continue.
- When exhausted, branch without throwing.

This is the key place where we deliberately keep `StopIteration` internal and
implicit for now.

### 8. Defer full Python iterator protocol and exceptions

Not part of the first implementation:

- method-call parsing such as `obj.next()`
- attribute lookup
- `raise`
- `try`
- Python exception objects
- `yield`
- user-visible `StopIteration`

These can be layered on later without invalidating the `for`-loop bytecode
shape. If we eventually expose `next()`, the runtime can wrap the same internal
iterator state machine in a Python-visible exception path.

## Test Plan

### Parser tests

File:

- [tests/test_parser.cpp](/Users/runehol/projects/clovervm/tests/test_parser.cpp)

Add coverage for:

- simple `for x in range(3): pass`
- `for ... else`
- simple variable target accepted
- tuple or other complex targets rejected for now

### Codegen tests

File:

- [tests/test_codegen.cpp](/Users/runehol/projects/clovervm/tests/test_codegen.cpp)

Add structural coverage for:

- simple `for` lowering over `range(...)`
- loop `else` layout
- `break` path skipping `else`

Keep these tests structural and focused on lowering shape rather than full
semantics.

### Interpreter tests

File:

- [tests/test_interpreter.cpp](/Users/runehol/projects/clovervm/tests/test_interpreter.cpp)

Add semantic coverage for:

- summing values from `range(5)`
- loop `else` runs after normal exhaustion
- `break` suppresses loop `else`
- `continue` skips to the next iteration correctly
- nested loop sanity case

## Milestones

### Milestone 1 [done]

- AST support for `STATEMENT_FOR`
- parser implementation for `for`
- AST printer support
- parser tests

### Milestone 2

- `BuiltinFunction` runtime object
- builtin scope
- `range(stop)` builtin

### Milestone 3

- `GetIter` and `ForIter` bytecodes
- interpreter support for iterators
- code-object printer support

### Milestone 4

- codegen lowering for `for`
- `break`, `continue`, and `else` semantics validated for `for`

### Milestone 5

- extend `range` to additional signatures if desired
- add more builtins such as `print`

## Main Risks

- The biggest architectural seam is extending `CallSimple` beyond bytecode
  `Function` objects.
- Builtin-scope creation needs to be done carefully so module/global lookup
  still behaves as expected.
- If the implementation expands to include method calls or Python exceptions too
  early, the scope will grow quickly.

## Recommendation

Start with the narrowest useful slice:

- `for`
- builtin `range(stop)`
- iterator bytecodes
- loop control-flow correctness

Once that is in place, the same builtin-function mechanism can naturally grow to
cover `print` and other small native helpers.
