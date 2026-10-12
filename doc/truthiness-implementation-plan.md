# Truthiness Implementation Plan

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Accepted |
| Implementation | Partial |
| Scope | Cached truth conversion, result validation, and staged conditional-jump integration |
| Owning layers | Builtin numeric methods and integer validation; runtime dispatch and caches; interpreter continuations; codegen and bytecode metadata |
| Validated against | `856bf6c4` (2026-10-12) |
| Supersedes | General truthiness dispatch entry in [Small Tasks](small-tasks.md) |

This plan records the agreed implementation direction and separates it into
independently verifiable stages. It is not a permanent architecture contract.
Retire it when the work is complete; retain the architecture contract and
reconcile existing dispatch and deviation documentation with the implementation.

## Accepted direction

The [truthiness architecture contract](truthiness-dispatch.md) owns semantics,
cache validity, result-kind metadata, frame ownership, and return validation.
This plan owns implementation order and verification. Do not duplicate or change
those decisions inside an individual stage without revisiting the contract.

The stages implement builtin methods, dispatch selection, cached positive and
negative conversion, then conditional jumps. Index coercion and correcting
sys.maxsize remain separate follow-ups. Immediate values retain their existing
fast path, including truthy NotImplemented.

## Implementation stages

### 1. Builtin methods and integer result validation

- [x] Add checked native `int.__bool__` and `float.__bool__` methods in their
  owning builtin type files, with trusted resolvers following nearby patterns.
- [x] Cover SMI/bool/BigInt integer receivers and float receivers, inherited
  methods, receiver rejection, boxed zero/one, float signed zero, and NaN.
- [x] Add a fallible integer-length validation helper alongside existing integer
  conversion policy. It must accept boxed values through INT64_MAX without
  narrowing them to SMI or calling Python methods.
- [x] Pin valid boundaries, invalid types, negative values, and overflow in
  focused helper tests. Use Python/interpreter tests for the public dunders.

This stage does not change conditional jumps or wire new truthiness opcodes.
Adding builtin numeric methods is necessary before a default-true table row can
handle numeric objects correctly.

### 2. Dispatch selection and cache metadata

- [ ] Add the truthiness table, explicit Bool/Length selection metadata, and the
  guarded default-true replay handler in the existing dispatch/walker substrate.
- [ ] Update cache construction, copying, reset, and inspection as needed for
  the new metadata; preserve existing operator behavior and cache ownership.
- [ ] Verify selection without entering interpreter callbacks: bool priority,
  missing-bool length selection, default true, invalid callable errors, trusted
  versus Python-function targets, and required validity guards.
- [ ] Verify that changing a class invalidates previously selected fallback
  paths, including adding a method after negative lookup.

This is substrate preparation. Do not emit unusable new bytecode or add a
temporary public fallback merely to connect an incomplete stage.

### 3. Positive conversion and Python return continuation

- [ ] Give ToBool its cache operand and have its emitter append the embedded
  FinishTruthiness return target, matching the CheckOperatorNotImplemented
  bundle convention. Callers emit only the main instruction. Update all in-repo
  emitters, printers, decoding/effect metadata, and relevant JIT boundaries
  together; ordinary disassembly presents one bundled instruction.
- [ ] Reserve caller scratch space using the existing operator-continuation
  convention. Keep saved state and callback arguments visible to frame/root
  scanning across calls and safepoints.
- [ ] Implement immediate, trusted, Python-function, and default paths. Trusted
  and immediate successes skip the finisher; Python returns validate there.
- [ ] Keep errors and Python dispatch in separate NOINLINE paths and preserve
  the interpreter's PARAMS/ARGS, START/COMPLETE, and MUSTTAIL conventions.
- [ ] Test custom bool/length returns, builtin containers, boxed integers,
  exceptions, invalid returns, inherited overrides, and warmed-cache replay.
- [ ] Test re-entry that changes the same IC from Bool to Length or vice versa,
  and class mutation during a callback. The outer return must use its saved kind.
- [ ] Check frame/root visibility and exception attribution around continuation
  returns. JIT support may exit explicitly at the unsupported protocol boundary;
  it must not accidentally use an inline-value truth test on a pointer.
- [ ] Add structural checks for bundled encoding/disassembly, the Python return
  offset, and advancing past the whole bundle on paths that finish directly.

Positive conversion is independently usable by existing membership/dictionary
bytecode. Negative conversion and conditional jumps retain their previous
coverage until their respective stages land.

### 4. Negative conversion and Python `not`

- [ ] Give ToBoolNot its cache operand and embedded FinishTruthinessNot return
  target, emitted and disassembled as part of the main instruction. Share the
  dispatch and validation implementation with positive conversion.
- [ ] Lower Python `not` through ToBoolNot and retire the duplicate Not opcode,
  updating every in-repo user rather than retaining a compatibility alias.
- [ ] Verify exactly one selected method call, inversion only after validation,
  error propagation, re-entry, and membership `not in` lowering.
- [ ] Verify that the immediate fast path still makes NotImplemented truthy and
  `not NotImplemented` false, while rejecting it as a `__bool__` return.

### 5. Conditional jumps

- [ ] Take a readiness review for cached JumpIfTrue/JumpIfFalse integration,
  reusing the now-implemented truth dispatch and return-validation policy.
- [ ] Preserve original operand values for short-circuit `and`/`or` expressions;
  normalization for the branch must not replace their Python-visible result.
- [ ] Cover `if`, `while`, conditional expressions, short-circuit operators,
  mutation/re-entry, callback errors, bytecode control flow, and JIT guard/exit
  behavior.

The precise branch continuation encoding belongs to that review. This plan does
not select a second cache family or silently broaden the conversion stages into
branch implementation work.

## Verification and completion

Run focused checks while implementing each stage and `ninja -C build-debug all
check` before finalizing implementation changes. For the interpreter/frame
stages, also run the release `check_opcode_frames` target; update the hot-handler
list for new or renamed hot handlers. No new benchmark suite is part of this
plan. Release measurements become relevant if later work makes a specific
performance claim.

Keep the stage checkboxes current as work lands. Update
[Python Deviations](python-deviations.md#truthiness) for each newly supported
surface, retaining the deferred index coercion and intentional NotImplemented
behavior explicitly. When completed, reconcile
[Fast Operator Dispatch](fast-operator-dispatch.md) with the implemented
truthiness cache/continuation contract, mark the architecture contract implemented,
update development priorities, and delete this plan.
