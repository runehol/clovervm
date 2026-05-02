# Exception Handling Transition Plan

## Goal

Move clovervm from C++ exceptions as the runtime error transport to a Python VM
exception substrate, while preserving the interpreter's current call/return
performance shape and keeping ordinary `for` loops cheap.

The target design is described in
[doc/exception-transport-and-protocols.md](./exception-transport-and-protocols.md).
This file tracks the implementation sequence we can check off as it lands.

## Current State

- Runtime failures mostly throw `std::runtime_error` from cold interpreter and
  runtime helpers.
- `Value::exception_marker()` and pending Python exception state on
  `ThreadState` exist, but no opcode or adapter uses them as the managed
  exception transport yet.
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
- Keep general user exception construction eager. Compact pending
  representation is special to `StopIteration` until there is a measured and
  safe reason to widen it.
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

## Stage 1: Frame Header Contract And Accessors

- [x] Document the current frame header slots as part of the runtime contract:
      `fp[0]` is previous `fp`, `fp[1]` is reserved for native pc / future
      compiled-frame metadata, `fp[2]` is the return `CodeObject`, and `fp[3]`
      is the return pc.
- [x] Add named constants or small accessors for these slots so interpreter code
      stops open-coding magic indices.
- [x] Route frame setup and normal return restoration through those constants or
      accessors.
- [ ] Make the GC/root-scanning contract consume the documented slot meanings
      directly.
- [x] Do not use `fp[1]` for exception transport. It is reserved for future
      native pc state.
- [x] Do not introduce pointer tagging or opaque encoded return-target objects
      in this stage.

Deliverable: the frame header remains easy for stack scanning and debugging to
interpret, while ordinary setup/return code has one documented slot contract.

## Stage 2: Pending Exception State

- [x] Add pending exception state to `ThreadState`.
- [x] Start with a conservative shape:

```cpp
enum class PendingExceptionKind
{
    None,
    Object,
    StopIteration,
};
```

- [x] Add helpers to set, inspect, clear, and assert pending exception state.
- [x] Add separate helpers for `StopIteration()` and `StopIteration(value)` so
      callers do not need to remember the `Value::not_present()` convention.
- [x] Add a `set_pending_exception_string` helper with the intended API shape,
      but keep it explicitly unsupported until exception objects exist.
- [x] Ensure pending exception state is never used for normal returns.

Deliverable: the VM has one canonical location for a Python exception in flight.

## Stage 3: Exceptional Frame Exit

- [ ] Add an exceptional frame-exit helper distinct from `op_return`.
- [ ] Make the helper restore or pop the current frame using the frame-layout
      helpers.
- [ ] Consult managed unwind metadata when it exists, and otherwise continue
      unwinding to the caller frame.
- [ ] At top-level or native test-harness boundaries, temporarily convert
      unhandled pending exceptions back into the current C++ error mechanism.
- [ ] Keep normal `Return` unchanged and free of exception-marker checks.

Deliverable: pending exceptions can cross Clover/Python frame boundaries without
using C++ unwinding through the interpreter dispatch loop.

## Stage 4: Managed Return Adapters

- [ ] Add a `ReturnOrRaiseException` opcode or equivalent thunk return adapter.
- [ ] Keep its contract local:

```text
accumulator != Value::exception_marker():
  normal Return

accumulator == Value::exception_marker():
  pending exception must be set
  enter managed exceptional unwind
```

- [ ] Use managed thunks/adapters rather than frame return-mode tagging.
- [ ] Do not use `fp[1]` or tagged return `CodeObject` pointers for exception
      transport.
- [ ] Add a code-object flag such as `HideFromTraceback` later if thunk frames
      should be hidden from user-visible tracebacks.

Deliverable: native and protocol implementation details can return marker plus
pending exception to managed adapter code, while ordinary return stays fast.

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

## Stage 7: Fast Iterator Protocol For `range`

- [ ] Define the local fast protocol convention:

```text
value:
  accumulator contains yielded item

completion:
  pending StopIteration is set
  accumulator contains Value::exception_marker()
```

- [ ] Split generic `FOR_ITER` into a protocol call and continuation shape.
- [ ] Teach the continuation to consume marker + pending `StopIteration` by
      clearing it and jumping to the loop exit/else target.
- [ ] Treat marker + any other pending exception as managed exceptional unwind.
- [ ] Make only `RangeIterator` participate at first.
- [ ] Decide the iterator plan during loop setup where possible, not on every
      iteration.
- [ ] Ensure ordinary `for` discards `StopIteration.value`.

Deliverable: the existing `range` iterator can use pending `StopIteration` plus
`Value::exception_marker()` as a local fast completion result, without making
ordinary opcodes marker-aware.

## Stage 8: Exception Tables And For-Loop Fallback

- [ ] Add exception table metadata to `CodeObject`.
- [ ] Add compiler tracking for protected bytecode ranges.
- [ ] Add synthetic exception-table handlers for `for` loops so real
      `StopIteration` from generic `__next__` exits the loop through ordinary
      exception handling.
- [ ] Keep the fast `FOR_ITER` continuation and the real-exception handler
      targeting the same loop exit/else block.
- [ ] Add `RAISE_UNWIND` for raise sites that may be covered by a local handler.
- [ ] Keep `RAISE_FAST` valid only outside all protected regions.
- [ ] Preserve the distinction between protocol `StopIteration` completion and
      ordinary exceptions: fast-protocol participants may still use exception
      tables, and exceptions leaking from callees must stay on the managed
      exceptional path.

Deliverable: fast protocol completion is an optimization; correctness for
generic iterators comes from ordinary exception tables.

## Stage 9: Native Exception Normalization

- [ ] Convert fixed-arity native thunk bodies to end in
      `ReturnOrRaiseException`.
- [ ] Make native failure set pending exception state and place
      `Value::exception_marker()` in the accumulator.
- [ ] Keep native/C calling conventions inside managed thunks; do not make
      native boundaries a first-order unwinder frame kind.
- [ ] Add outer C API sentinel conversion only at actual external C API
      boundaries.

Deliverable: native failures no longer need to unwind C++ exceptions through
the VM call stack, and managed unwinding still sees ordinary Clover frames.

## Stage 10: Local Handlers

- [ ] Add table lookup and stack/register trimming for local handler entry.
- [ ] Add parser, AST, codegen, and interpreter support for a first useful
      `try` / `except` slice.

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
2. document frame-header slots and route access through named constants/helpers
3. add pending exception state
4. add exceptional frame exit
5. add managed return adapters
6. convert a few selected interpreter slow errors

That milestone gives the VM an exception transport backbone without also taking
on `try`, exception object materialization, tracebacks, `yield from`, or full
native normalization in the same step.
