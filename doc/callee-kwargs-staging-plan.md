# Callee Kwargs Staging Plan

This plan stages runtime support for callee `**kwargs` parameters. It does not
include caller-side `**mapping` expansion.

## Invariants

- `**kwargs` never absorbs positional arguments. Positional arity remains
  controlled by positional parameters and `*args`.
- Every call to a function with `**kwargs` receives a fresh dictionary in the
  kwargs parameter slot, including calls with no keyword arguments.
- Unmatched explicit `name=value` keywords are inserted into the kwargs
  dictionary in call-site order.
- The `**kwargs` parameter is not keyword-bindable and is not present in the
  function keyword remap.
- `keyword_dest_regs` remains compact. Destination value `0` is reserved as the
  kwargs-dictionary sentinel and must not be used as a writable parameter
  destination.
- Cache construction asserts that real keyword destination registers never equal
  the kwargs sentinel, and frame entry branches on the sentinel before writing
  through a destination value.
- The kwargs dictionary is string-only in this stage because it only receives
  explicit `name=value` keyword names. Caller `**mapping` validation is separate
  later work.
- `*args` and `**kwargs` slots are initialized late on the full adaptation path,
  after any default-copy placeholders that may have touched those slots.
- Caller `*args` and caller `**mapping` expansion remain unsupported and
  explicit.

## Implementation Stages

- [ ] Replace `FunctionCallAdaptation::Varargs` with the compact tiered model:
  `FixedArity`, `Defaultable`, and `Full`.
- [ ] Update positional call adaptation selection so functions with `*args` or
  `**kwargs` use `Full`, while fixed/default-only functions keep the existing
  cheaper paths.
- [ ] Extend function signature lowering to include a trailing `**kwargs`
  parameter slot and set `FunctionParameterFlags::HasKwArgs`.
- [ ] Keep `**kwargs` out of `FunctionKeywordRemap`, while continuing to remap
  positional-or-keyword and keyword-only parameters.
- [ ] Update function layout assertions for optional `*args` and `**kwargs`
  slots.
- [ ] Update constructor-thunk signature copying so public class-call thunks
  mirror `__init__` or `__new__` with `self` or `cls` dropped, including
  `HasKwArgs`.
- [ ] Copy constructor-thunk keyword remaps with parameter indexes shifted down
  by one, while keeping kwargs collectors out of the remap.
- [ ] Keep constructor-thunk bodies as prepared positional-only forwarding
  calls into the selected `__init__` or `__new__` code object.
- [ ] Add frame-entry helpers to compute the kwargs slot and store a fresh empty
  dictionary for functions with `**kwargs`.
- [ ] Update `CallPositional` entry so the `Full` path initializes defaults,
  `*args`, and `**kwargs` as required by the callee signature.
- [ ] Update keyword-call IC construction so unmatched explicit keywords become
  kwargs-sentinel destinations when the callee has `**kwargs`, and remain
  errors otherwise.
- [ ] Update keyword-call frame entry so the `Full` path uses a sentinel-aware
  keyword loop, while fixed/default-only keyword calls keep the direct register
  copy loop.
- [ ] Ensure required-parameter validation skips `*args` and `**kwargs` slots
  because frame entry initializes them.
- [ ] Keep duplicate formal-fill validation for keywords that match real
  keyword-bindable parameters.

## Tests

- [ ] Parser/codegen tests for accepted `**kwargs` signatures and rejected
  positional-only signatures if positional-only remains unsupported.
- [ ] Codegen tests for kwargs parameter layout, flags, and absence from the
  keyword remap.
- [ ] Interpreter tests for `def f(**kwargs): return kwargs` with no keywords
  and with multiple explicit keywords.
- [ ] Interpreter tests that every call receives a fresh kwargs dictionary.
- [ ] Interpreter tests for mixed named parameters and unmatched keywords:
  `def f(a, *, b=2, **kwargs)`.
- [ ] Interpreter tests that duplicate formal fills still raise, for example
  `def f(a, **kwargs); f(1, a=2)`.
- [ ] Interpreter tests that extra positional arguments still raise when there
  is no `*args`, even if `**kwargs` exists.
- [ ] Interpreter tests for combined `*args` and `**kwargs`.
- [ ] Constructor tests for keyword calls to `__init__` and `__new__`
  signatures that include `**kwargs`.
- [ ] Cache-hit tests for repeated keyword calls with the same keyword-name
  tuple and kwargs-sentinel routing.

## Verification

- [ ] Run `clang-format -i` on touched C++ source and header files.
- [ ] Run `ninja -C build-debug all check`.
- [ ] For interpreter hot-path-sensitive edits, consider
  `cmake --build build-release --target run_benchmark`.
