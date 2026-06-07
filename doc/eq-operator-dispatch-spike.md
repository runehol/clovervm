# `__eq__` Operator Dispatch Spike

This plan describes a cacheless first spike for rich-comparison dispatch using
`==`. It builds on `fast-operator-dispatch.md` and intentionally avoids
Python-candidate inline caching for multi-candidate tables.

The spike goal is correctness and shape validation:

- preserve existing direct primitive equality fast paths;
- add table-driven rich-comparison dispatch for non-primitive `==`;
- call Python `__eq__` candidates through the paired
  `CheckOperatorNotImplemented` continuation opcode;
- return arbitrary non-`NotImplemented` rich-comparison results unchanged;
- fall back to identity only when both rich-comparison candidates return
  `NotImplemented`.

## Non-Goals

- Do not install Python-candidate cache entries for `CompareEq` table rows.
- Do not implement trusted native handler caching for comparisons in this
  spike.
- Do not implement arithmetic operators, in-place operators, ternary `pow`, or
  membership.
- Do not cache or fast-replay descriptor/call shapes beyond the fixed
  function-shaped path already considered cacheable for operator calls. They
  still need Python-correct generic handling when selected.
- Do not truth-test rich-comparison results inside `TestEqual`.

## Code-Change Hygiene

- Run `clang-format -i` on touched C++ source/header files as part of each
  implementation change.
- Run `ninja -C build-debug all check` before considering the spike ready to
  commit.

## Stage 1: Pin Semantics

Semantic facts to preserve:

- User-defined `__eq__` may return a non-bool value, and expression-level `==`
  returns that value unchanged.
- If both candidates return `NotImplemented`, `==` falls back to identity:
  distinct objects compare false and the same object compares true.
- For operands of the same exact type, Python may call `__eq__` twice when the
  first call returns `NotImplemented`.
- For rich comparison, a right-hand strict subtype gets reflected priority when
  its reflected method resolves.
- A candidate may mutate class state before returning `NotImplemented`; the
  continuation must observe current lookup state for later candidates.
- A found `__eq__` candidate is not missing just because it is non-callable or
  not supported by the fast function-shaped entry path. It must be called
  through a generic path, raise the ordinary call error, or continue with
  `NotImplemented` from the next row according to its actual result.
- Exceptions raised during lookup, binding, or method execution propagate.

Checklist:

- [x] Add focused interpreter tests for non-bool rich-comparison results.
- [x] Add interpreter tests for equality identity fallback.
- [x] Add interpreter tests for same-exact-type double `__eq__` dispatch.
- [x] Add interpreter tests for right-subclass reflected-priority ordering.
- [x] Add adversarial tests for class mutation before returning
      `NotImplemented`.
- [x] Add tests that exceptions raised during lookup, binding, or method
      execution propagate and do not update operator caches.

## Stage 2: Bytecode Shape

- [ ] Add `CheckOperatorNotImplemented` to the bytecode enum.
- [ ] Add interpreter dispatch table entry for `CheckOperatorNotImplemented`.
- [ ] Implement only a skeleton `CheckOperatorNotImplemented` handler at this
      stage; it should not yet be reachable from emitted `==` bytecode.
- [ ] Update bytecode printing so paired operator instructions hide the check
      byte in ordinary disassembly.
- [ ] Add runtime tracing support that can still show
      `CheckOperatorNotImplemented` when it is actually reached as a return
      target.
- [ ] Add shared code object walking/instruction-length support for paired
      operator opcodes.
- [ ] Keep existing `==` emission on the old path until the table walker,
      continuation prefix, Python candidate entry, and check-opcode resume path
      are wired together.

## Stage 3: Dispatch Table Metadata

- [ ] Add `OperatorStepAction : uint8_t`.
- [ ] Add `OperatorStepApplicability : uint8_t`.
- [ ] Add `OperatorStep` with:
      `ImmortalInternedString *dunder_name`, action, applicability, and
      `uint8_t else_goto`.
- [ ] Add immutable operator dispatch table storage indexed by small SMI table
      ids.
- [ ] Add the `CompareEq` table:

```text
CompareEq
    0: CallBinaryReflected("__eq__",
                           IfRichComparisonReflectedPriority,
                           else goto normal_first)
    1: CallBinary("__eq__", IfMethodFound)
    2: IdentityEq(Always)

normal_first:
    3: CallBinary("__eq__", IfMethodFound)
    4: CallBinaryReflected("__eq__", IfMethodFound)
    5: IdentityEq(Always)
```

- [ ] Add table metadata helpers to fetch a table by SMI id and validate row
      indices in debug builds.

## Stage 4: Table Control Helpers

- [ ] Implement cold helpers that inspect table rows from a starting row.
- [ ] Apply `else_goto` when applicability fails; otherwise fall through to the
      next row.
- [ ] Implement `IfMethodFound` using current special-method lookup.
- [ ] Treat a found non-callable or non-fast-callable value as a selected
      candidate, not as a missing method.
- [ ] Implement `IfRichComparisonReflectedPriority`: the reflected method must
      resolve and `type(operand1)` must be a strict subclass of
      `type(operand0)`.
- [ ] Implement candidate selection for `CallBinary` and
      `CallBinaryReflected` without entering Python yet.
- [ ] Implement `IdentityEq` fallback as `operand0 is operand1`.
- [ ] Return one of: selected Python candidate, native fallback result, raised
      error, or exhausted table.

## Stage 5: Continuation Frame Layout

- [ ] Allocate continuation registers from `get_first_free_arg_encoded_reg()`.
- [ ] Store the hidden prefix:
      `reg(0) = table id`, `reg(1) = next row`, `reg(2) = operand0`,
      `reg(3) = operand1`.
- [ ] Build callee-visible call arguments after the continuation prefix.
- [ ] Account for the downward-growing physical frame layout while keeping the
      logical register numbering clear.
- [ ] Ensure continuation operand slots are root-scanned.
- [ ] Ensure callee-visible argument slots are not reused as saved operands.

## Stage 6: Python Candidate Entry

- [ ] Implement `CallBinary` and `CallBinaryReflected` call layout using the
      Stage 5 continuation prefix.
- [ ] Stop table walking as soon as a Python function candidate is selected.
      There is no native loop through subsequent Python candidates.
- [ ] Write table id, next row, and canonical operands before entering the
      candidate.
- [ ] Enter the Python function with the paired `CheckOperatorNotImplemented`
      byte as the return PC.
- [ ] Keep the helper cacheless for Python candidates.
- [ ] Route selected candidates that are unsupported by the fast function-shaped
      entry path through a generic call path, or raise the ordinary call error
      for non-callable candidates, without treating them as missing methods.
- [ ] Preserve the same next-row `NotImplemented` continuation semantics for
      generic-call candidates as for fast function-shaped candidates.

## Stage 7: `CheckOperatorNotImplemented` Resume

- [ ] Replace the Stage 2 skeleton with the real resume handler once the table
      walker and Python candidate entry exist.
- [ ] If the accumulator is not the singleton `NotImplemented`, finish the
      logical `TestEqual` instruction with the accumulator unchanged.
- [ ] If the accumulator is `NotImplemented`, read table id, next row,
      operand0, and operand1 from the hidden prefix.
- [ ] Resume table walking from the saved row using current lookup state.
- [ ] Assert table id, row index, and prefix layout in debug builds.
- [ ] Do not install or update inline caches from the continuation path.
- [ ] Propagate pending exceptions through the interpreter exception path.

## Stage 8: Integrate `TestEqual`

- [ ] Keep existing direct equality fast paths for inline values and builtin
      primitives ahead of operator dispatch.
- [ ] Route non-primitive or otherwise unsupported direct cases into
      `CompareEq` table dispatch.
- [ ] Add or update a codegen emit helper for real `==` emission that emits
      both `TestEqual` and `CheckOperatorNotImplemented`.
- [ ] Enable `TestEqual`'s paired-instruction length so it owns the following
      check byte as one logical instruction once real `==` emission produces
      the pair.
- [ ] Preserve the existing accumulator/register operand convention when
      mapping source operands into semantic `operand0` and `operand1`.
- [ ] Return arbitrary rich-comparison results unchanged.
- [ ] Fall back to identity only after both table branches return
      `NotImplemented`.

## Stage 9: Verification

- [ ] Add regression tests for bytecode printing/walking if the paired check
      byte changes existing disassembly assumptions.
- [ ] Add adversarial interpreter tests for mutation between comparison
      candidates.
- [ ] Confirm no Python-candidate operator cache entries are installed by the
      `__eq__` spike.

## Stage 10: Follow-Up Work

- [ ] Decide the table-row Python-candidate cache payload shape.
- [ ] Represent skipped-row applicability dependencies for future cache hits.
- [ ] Decide trusted-handler guard policy for rich comparisons.
- [ ] Generalize the table walker to `!=`, ordering comparisons, binary
      arithmetic, and in-place arithmetic only after `CompareEq` is stable.
- [ ] Revisit odd callable shapes beyond fixed function-shaped method calls.
