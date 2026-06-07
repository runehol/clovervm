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

- [x] Add `CheckOperatorNotImplemented` to the bytecode enum.
- [x] Add interpreter dispatch table entry for `CheckOperatorNotImplemented`.
- [x] Implement only a skeleton `CheckOperatorNotImplemented` handler at this
      stage; it should not yet be reachable from emitted `==` bytecode.
- [x] Add runtime tracing support that can still show
      `CheckOperatorNotImplemented` when it is actually reached as a return
      target.
- [x] Keep existing `==` emission on the old path until the table walker,
      continuation prefix, Python candidate entry, and check-opcode resume path
      are wired together.

## Stage 3: Dispatch Table Metadata

- [x] Add `OperatorStepAction : uint8_t`.
- [x] Add `OperatorStepApplicability : uint8_t`.
- [x] Add `OperatorStep` with:
      VM-local interned `String *dunder_name`, action, applicability, and
      `uint8_t else_skip`.
- [x] Add immutable operator dispatch table storage indexed by table ids. The
      continuation prefix stores the table id as an SMI, but the C++ metadata
      API works on the decoded integer payload.
- [x] Add the `CompareEq` table:

```text
CompareEq
    0: CallBinaryReflected("__eq__",
                           IfRichComparisonReflectedPriority,
                           else +2 to normal_first)
    1: CallBinary("__eq__", IfMethodFound)
    2: IdentityEq(Always)

normal_first:
    3: CallBinary("__eq__", IfMethodFound)
    4: CallBinaryReflected("__eq__", IfMethodFound)
    5: IdentityEq(Always)
```

- [x] Add table metadata helpers to fetch a table by decoded table id and
      validate row indices in debug builds.

## Stage 3.5: Trusted Handler Resolver Shape

- [x] Define the trusted-handler resolver contract for table-selected
      candidates before implementing the walker.
- [x] The walker may call the trusted-handler resolver for a selected function,
      but it must not call the trusted handler itself. The opcode handler owns
      the actual trusted-handler call, accumulator update, pending-exception
      propagation, and cache installation policy.
- [x] Trusted-handler resolver results must be normalized for the opcode's
      physical operand order. Cached replay should be able to call
      `handler.binary(thread, operand0, operand1)` directly, with no operand
      order branch and no trampoline.
- [x] If the selected row is reflected, the resolver must either return a
      direct handler with the opcode operand-order calling convention or decline
      by returning no trusted handler. Do not introduce reflected-order
      trampolines; this is a hot-path replay contract.

## Stage 4: Table Control Helpers

- [x] Implement cold helpers that inspect table rows from a starting row.
- [x] Apply `else_skip` when applicability fails: the next row is
      `row + 1 + else_skip`, so `else_skip = 0` is ordinary fallthrough.
- [x] Implement `IfMethodFound` using current special-method lookup.
- [x] Treat a found non-callable or non-fast-callable value as a terminal row,
      not as a missing method. It should raise the ordinary call error or enter
      the generic-call path when that path exists; it must not continue to later
      rows as though the method were absent.
- [x] Implement `IfRichComparisonReflectedPriority`: the reflected method must
      resolve and `type(operand1)` must be a strict subclass of
      `type(operand0)`.
- [x] Implement candidate selection for `CallBinary` and
      `CallBinaryReflected` without entering Python yet.
- [x] Implement `IdentityEq` fallback as `operand0 is operand1`.
- [x] Return an `OperatorWalkDescriptor`-style result. The status enum should
      use the default underlying type, not `uint8_t`; the descriptor is
      transient interpreter control data, and packing it can make register
      passing and use-site code generation worse.
- [x] The table walker should walk multiple rows until it reaches one terminal
      boundary:
      `CallPythonFunction`, `CallTrustedHandler`, `NativeResult`, or
      `PropagatePendingException`.
- [x] Use the Stage 3.5 trusted-handler resolver contract: resolve trusted
      handlers as terminal walk results, but leave handler execution to the
      opcode handler.
- [x] Return an inert `OperatorInlineCache` candidate in the walk descriptor so
      the result shape already matches future replay. For this spike, fill it
      opportunistically when the data is available, but do not install it.
      Skipped-row guard representation and validation are follow-up work.
- [x] Do not return an exhausted-table state. Dispatch tables must end in an
      unconditional terminal action. Falling off a table is a VM metadata bug:
      use a debug-only assertion followed by `__builtin_unreachable()` rather
      than a Python-visible dynamic result.

## Stage 5: Continuation Frame Layout

- [x] Allocate continuation registers from `get_first_free_arg_encoded_reg()`.
- [x] Store the hidden prefix:
      `reg(0) = table id`, `reg(1) = next row`, `reg(2) = operand0`,
      `reg(3) = operand1`.
- [x] Build callee-visible call arguments after the continuation prefix.
- [x] Add continuation frame setup helpers per opcode/operator-handler arity.
      The handler owns arity; the hidden continuation prefix stores canonical
      operands and is independent of whether the selected call is normal or
      reflected.
- [x] Insert one padding slot after odd-arity continuation prefixes so the
      callee-visible argument stack remains 16-byte aligned.
- [x] Add callee argument setup helpers that use the selected walk action, so
      `CallBinary` and `CallBinaryReflected` build the correct callee-visible
      argument order.
- [x] Account for the downward-growing physical frame layout while keeping the
      logical register numbering clear.
- [x] Ensure continuation operand slots are root-scanned.
- [x] Ensure callee-visible argument slots are not reused as saved operands.

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
- [ ] Update bytecode printing so paired operator instructions hide the check
      byte in ordinary disassembly.
- [ ] Add shared code object walking/instruction-length support for paired
      operator opcodes.
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
