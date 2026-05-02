# Exception Handling Transition Plan

## Goal

Move clovervm from C++ exceptions as the runtime error transport to a Python VM
exception substrate, while preserving the interpreter's current call/return
performance shape and keeping ordinary `for` loops cheap.

The target design is described in
[doc/lazy-exceptions-and-tracebacks.md](./lazy-exceptions-and-tracebacks.md).
This file tracks the implementation sequence we can check off as it lands.

## Current State

- Runtime failures mostly throw `std::runtime_error` from cold interpreter and
  runtime helpers.
- `Value::exception_marker()` exists, but there is no pending Python exception
  state on `ThreadState`.
- Frames currently keep caller metadata at and above `fp`: `fp[0]` is the
  previous frame pointer, `fp[2]` is the return `CodeObject`, and `fp[3]` is
  the return pc.
- Native function thunks return through ordinary bytecode `Return`; native
  failures may still throw C++ exceptions.
- `for` loops use internal iterator exhaustion for builtin `RangeIterator`
  paths, not user-visible `StopIteration`.
- `raise`, `try`, `except`, reraise, exception objects, and tracebacks are not
  implemented.

## Guiding Constraints

- Keep the frame-layout work separate from exception semantics where possible.
  The frame move should be behavior-preserving and easy to bisect.
- Keep ordinary successful returns separate from exceptional frame exit.
  Normal return should continue to carry the result in the accumulator.
- Use pending exception state on `ThreadState` as the single source of truth for
  Python exceptions in flight.
- Preserve Python's `StopIteration` semantics: ordinary `for` loops discard
  `StopIteration.value`, while future delegation machinery such as `yield from`
  may inspect it.
- Keep general user exception construction eager. Compact/lazy representation is
  special to `StopIteration` until there is a measured and safe reason to widen
  it.
- Keep C++ exceptions available as temporary outer panic plumbing only while the
  VM exception transport is being migrated.
- Treat eventual `-fno-exceptions` support as a design constraint. New runtime
  error paths should move toward explicit VM exception transport or explicit
  fatal-abort/panic paths, not new C++ exception dependencies.

## Stage 0: Call Frame Layout Refactor

- [x] Move the full call frame/header to the above-`fp` side.
- [x] Update frame setup helpers to write caller metadata through one canonical
      layout.
- [x] Update `op_return` and any class-body, constructor-thunk, and native-thunk
      setup paths to read the new layout.
- [x] Keep runtime behavior identical: ordinary calls, constructor calls, class
      bodies, native thunks, and returns should still pass existing tests.
- [x] Add or adjust structural tests only where they pin down frame layout or
      call/return invariants that are otherwise easy to regress.

Deliverable: one canonical frame metadata area, with no Python exception
semantic changes.

## Stage 1: Return Target Abstraction

- [ ] Introduce helper types/functions for writing and reading return targets.
- [ ] Route frame setup through those helpers instead of open-coding physical
      frame slots.
- [ ] Route normal return restoration through those helpers.
- [ ] Keep the internal representation equivalent to the current
      `CodeObject* + pc` shape until tagging is needed.

Deliverable: later exception work can change return-target representation in one
place rather than across every call path.

## Stage 2: Pending Exception State

- [ ] Add pending exception state to `ThreadState`.
- [ ] Start with a conservative shape:

```cpp
enum class PendingExceptionKind
{
    None,
    Object,
    StopIteration,
};
```

- [ ] Represent general exceptions as realized objects once exception objects
      exist; until then, use narrow VM-originated placeholders only where needed
      for migration.
- [ ] Add helpers to set, inspect, clear, and assert pending exception state.
- [ ] Ensure pending exception state is never used for normal returns.

Deliverable: the VM has one canonical location for a Python exception in flight.

## Stage 3: Exceptional Frame Exit

- [ ] Add an exceptional frame-exit helper distinct from `op_return`.
- [ ] Make the helper restore or pop the current frame using the frame-layout
      helpers.
- [ ] Initially treat all bytecode caller targets as ordinary propagation
      targets.
- [ ] At top-level or native test-harness boundaries, temporarily convert
      unhandled pending exceptions back into the current C++ error mechanism.

Deliverable: pending exceptions can cross Clover/Python frame boundaries without
using C++ unwinding through the interpreter dispatch loop.

## Stage 4: Tagged Return Modes

- [ ] Extend the return-target abstraction with:

```text
BytecodeViaUnwind
BytecodeViaResult
Native
```

- [ ] Encode the return-target kind in frame metadata, using pointer tagging or
      another representation that preserves raw bytecode pcs.
- [ ] Make ordinary function calls install `BytecodeViaUnwind`.
- [ ] Make protocol calls able to install `BytecodeViaResult` with a saved
      continuation pc.
- [ ] Make native/C boundaries install or emulate `Native` return targets where
      a C sentinel convention is needed.

Deliverable: exceptional exits can either keep unwinding, return
`Value::exception_marker()` to a bytecode continuation, or convert to a native
sentinel.

## Stage 5: Convert Selected VM Slow Errors

- [ ] Convert a small set of cold interpreter helpers from direct
      `std::runtime_error` throws to pending-exception setup plus exceptional
      frame exit.
- [ ] Good first candidates:
  - [ ] `NameError`
  - [ ] simple `TypeError`
  - [ ] `ValueError: negative shift count`
  - [ ] `AssertionError`
- [ ] Keep complex runtime failures and construction-time failures on the old
      path until object materialization and native normalization are ready.
- [ ] Add interpreter tests for propagation across nested calls.

Deliverable: real VM exception propagation exists for selected existing failure
paths, even before `raise` or `try`.

## Stage 6: `raise` Without Local Handlers

- [ ] Add AST and parser support for a narrow first slice of `raise`.
- [ ] Add bytecode and codegen for unprotected raise sites, initially
      `RAISE_FAST`.
- [ ] Implement `RAISE_FAST` by setting pending exception state and entering
      exceptional frame exit without a local table lookup.
- [ ] Keep the first version outside `try`, `with`, `finally`, and other
      protected regions.
- [ ] Add interpreter tests for Python-authored raises propagating out through
      nested calls.

Deliverable: Python-authored `raise` exists and uses VM exception transport.

## Stage 7: `ViaResult` Iterator Protocol

- [ ] Split generic iterator protocol execution into a call half and a
      continuation half, such as `FOR_ITER1` and `FOR_ITER2`.
- [ ] Make the iterator-next call use a `BytecodeViaResult` return target whose
      saved pc is the continuation opcode.
- [ ] Teach the continuation to classify:

```text
normal result:
  consume yielded item

pending StopIteration:
  clear pending exception
  exit loop

other pending exception:
  promote to ordinary propagation at this bytecode site
```

- [ ] Preserve current optimized builtin `range` paths as internal-exhaustion
      fast paths.
- [ ] Ensure ordinary `for` discards `StopIteration.value` in both compact and
      materialized forms.

Deliverable: user-visible `StopIteration` works for iterator protocol, while
ordinary `for` keeps Python's payload-discarding behavior.

## Stage 8: Compact Pending `StopIteration`

- [ ] Add compact pending representation for `StopIteration`.
- [ ] Use `Value::not_present()` to distinguish `StopIteration()` from
      `StopIteration(None)`.
- [ ] Lower recognized `raise StopIteration(value)` paths to the compact form
      where construction side effects are known not to matter.
- [ ] Teach `except StopIteration` matching to recognize the compact pending
      kind without materializing the object.
- [ ] Keep internal iterator exhaustion distinct from Python `StopIteration`.

Deliverable: Python-authored `StopIteration` can be represented cheaply and
remain JIT-visible, without confusing it with internal iterator exhaustion.

## Stage 9: Native Exception Normalization

- [ ] Add native thunk return adapters or a `NativeReturn` opcode shape that can
      normalize native failures.
- [ ] Make native failure leave pending exception state and return through the
      caller's requested mode.
- [ ] For ordinary bytecode callers, continue unwinding.
- [ ] For `ViaResult` protocol callers, return `Value::exception_marker()`.
- [ ] For C/native API boundaries, return the appropriate C sentinel.

Deliverable: native failures no longer need to unwind C++ exceptions through
the VM call stack.

## Stage 10: Exception Tables And Local Handlers

- [ ] Add exception table metadata to `CodeObject`.
- [ ] Add compiler tracking for protected bytecode ranges.
- [ ] Add `RAISE_UNWIND` for raise sites that may be covered by a local handler.
- [ ] Add table lookup and stack/register trimming for local handler entry.
- [ ] Add parser, AST, codegen, and interpreter support for a first useful
      `try` / `except` slice.
- [ ] Keep `RAISE_FAST` valid only for bytecode offsets outside all protected
      regions.

Deliverable: pending exceptions can be caught by handlers in the current frame.

## Stage 11: Exception Objects And Materialization

- [ ] Add enough Python-visible exception object support for caught exceptions.
- [ ] Materialize compact `StopIteration` when the exception object becomes
      observable.
- [ ] Support `except StopIteration as e` and `e.value`.
- [ ] Replace placeholder VM-originated exceptions with specific exception
      object construction.
- [ ] Normalize constructor, descriptor, arithmetic, and other slow-path errors
      into specific VM exceptions.

Deliverable: caught exceptions are inspectable Python objects rather than only
transport state.

## Stage 12: Lazy Tracebacks

- [ ] Add lazy traceback metadata to pending exception state.
- [ ] Record raise-site code object, pc, frame pointer or preserved frame
      metadata, and any previous traceback chain.
- [ ] Identify stack-lifetime boundaries where lazy frame-backed traceback data
      must be copied or materialized.
- [ ] Materialize traceback objects for observation boundaries such as
      `e.__traceback__`, formatting, debugging, and APIs exposing frames.

Deliverable: exception propagation can produce Python-compatible tracebacks
without eagerly allocating traceback objects on every hot failure path.

## Stage 13: Reraise And Delegating Iteration

- [ ] Add bare `raise` support.
- [ ] Preserve the current logical exception and traceback chain on reraise.
- [ ] Start a fresh lazy traceback segment at the reraise site.
- [ ] Add `yield from` or equivalent delegating-iteration machinery that
      consumes `StopIteration.value` as the delegation result.
- [ ] Keep ordinary `for` loops payload-discarding.

Deliverable: Python-compatible reraising and the first machinery that makes
`StopIteration.value` semantically observable without manual iterator driving.

## Stage 14: Remove C++ Exception Dependency

- [ ] Audit all remaining `throw`, `try`, and `catch` uses in runtime code.
- [ ] Classify each use as:
  - [ ] Python-visible VM exception transport
  - [ ] parser/compiler diagnostic path
  - [ ] test-only helper
  - [ ] benchmark/tooling-only helper
  - [ ] fatal internal panic
- [ ] Convert Python-visible runtime failures to pending VM exceptions.
- [ ] Convert fatal internal failures to explicit abort/panic helpers that do
      not require C++ exception unwinding.
- [ ] Keep parser/compiler diagnostics separate from the interpreter runtime
      decision; they may continue using host-side error mechanisms until there
      is a separate reason to change them.
- [ ] Add a CI or local build configuration that compiles the runtime and
      interpreter with `-fno-exceptions`.
- [ ] Decide whether tests, benchmarks, and standalone tools also need
      `-fno-exceptions`, or whether the constraint applies only to the VM
      runtime library.

Deliverable: the VM runtime can be built without C++ exception support; Python
exceptions are represented by CloverVM state, not host-language unwinding.

## First Milestone

The first coherent milestone is stages 0 through 5:

1. finish the call-frame layout refactor
2. hide return-target physical layout behind helpers
3. add pending exception state
4. add exceptional frame exit
5. add tagged return modes
6. convert a few selected interpreter slow errors

That milestone gives the VM an exception transport backbone without also taking
on `try`, exception object materialization, tracebacks, `yield from`, or full
native normalization in the same step.
