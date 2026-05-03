# Exception Transport And Protocols

## Goal

Define an exception substrate that keeps ordinary call/return fast, makes real
exception propagation explicit and slow, and keeps iterator completion cheap on
hot protocol paths.

The central performance motivation is `StopIteration`. Python uses one surface
exception class for both user-visible exceptions and iterator/generator
completion. CloverVM should preserve that surface behavior without forcing every
ordinary loop exhaustion to allocate and throw a Python exception object.

## Core Model

Normal return remains the fast path:

```text
normal return:
  accumulator contains a normal user value
  restore caller fp, code object, and return pc from the frame header
  dispatch at the restored return pc
```

Normal opcode completion does not test for `Value::exception_marker()` and does
not inspect frame flags.

Real Python exceptions live as pending exception state on `ThreadState`:

```cpp
enum class PendingExceptionKind
{
    None,
    Object,
    StopIteration,
};

struct PendingException
{
    PendingExceptionKind kind;

    // Used when kind == Object.
    OwnedValue object;

    // Used when kind == StopIteration. Value::not_present() means no value.
    OwnedValue stop_iteration_value;

    LazyTraceback traceback;
};
```

Most exceptions are represented by an already-realized exception object plus a
lazy traceback. General user exception construction remains eager: constructing
an arbitrary exception may run Python code, mutate state, or raise another
exception.

Early VM-originated exceptions use an internal, non-reentrant construction path
for simple builtin exception objects. That path does not call Python code or
user-overridable constructors; allocation failure is treated as fatal or as a
separate out-of-memory path.

User-authored `raise SomeUserException("x")` first evaluates and constructs the
exception object through ordinary execution. If that construction raises a
different exception, that different exception propagates; the original
`SomeUserException` raise never begins. Only after construction succeeds does the
`raise` operation attach the raise-site pc/fp state and set the pending
exception.

`StopIteration` may use a compact pending representation:

```text
pending.kind = StopIteration
pending.stop_iteration_value = Value::not_present() or supplied value
pending.traceback = lazy segment if/when this becomes a real exception
```

`Value::not_present()` represents `StopIteration()` with no supplied value,
which is distinct from `StopIteration(None)`.

## Exceptional Paths

Real exception propagation uses a slow exceptional path, not ordinary return.
The exceptional path consults managed unwind metadata such as bytecode exception
tables or future JIT unwind tables.

The cold unwind helper resolves metadata into an executable managed target rather
than returning raw table entries:

```cpp
struct ExceptionalTarget
{
    Value *fp;
    CodeObject *code_object;

    // Always valid: bytecode landing-pad pc in code_object.
    const uint8_t *interpreted_pc;

    // Optional compiled landing-pad entry. Null means enter the interpreter at
    // interpreted_pc.
    const void *jit_pc = nullptr;
};
```

```text
exceptional frame exit:
  pending exception is set on ThreadState
  consult this frame's managed unwind metadata
  return an ExceptionalTarget if one applies
  otherwise pop/restore the caller frame and continue exceptional unwinding
```

The design intentionally does not tag the return `CodeObject*`, does not use
`fp[1]` for exception transport, and does not make native/C boundaries a
first-order frame kind. Native and protocol calls are adapted through managed
thunks.

## Adapter Thunks

Managed thunks normalize implementation-specific result conventions into the VM
exception model. A thunk is an ordinary managed Clover frame for unwinding, but
its code object may later be marked hidden from tracebacks.

The common adapter shape is:

```text
CallNativeN / CallFastProtocol / other implementation-specific call
ReturnOrRaiseException
```

`ReturnOrRaiseException` has a narrow contract:

```text
if accumulator != Value::exception_marker():
  normal Return

if accumulator == Value::exception_marker():
  pending exception must be set
  if pending.kind == StopIteration:
    materialize/promote StopIteration to an ordinary exception object
  enter managed exceptional unwind
```

Marker with no pending exception is an internal VM error.

Only explicit adapter or protocol continuation opcodes observe
`Value::exception_marker()`. Ordinary opcodes and ordinary returns do not.

Native functions participate through thunks. A native success writes a normal
`Value` to the accumulator. A native failure sets pending exception state and
writes `Value::exception_marker()`. The thunk's `ReturnOrRaiseException`
converts that marker into managed exceptional unwind.

The same shape adapts `stop_returning_code_object` back into ordinary calls. If
a caller is not participating in the stop-returning convention but the
implementation wants to reuse that body, the adapter can select
`stop_returning_code_object` explicitly:

```text
LoadConst stop_returning_code_object
CallCodeObject
ReturnOrRaiseException
```

`CallCodeObject` is an internal direct-code call. It assumes the surrounding
thunk has already prepared the argument/frame window, and it enters exactly the
supplied `CodeObject` without resolving the owning `Function`'s ordinary
code-object selection or adaptation policy again. The adapter then converts any
`Value::exception_marker()` result into the ordinary managed exception path.

## StopIteration And Stop-Returning Code

`StopIteration` is special only at protocol boundaries.

A stop-returning iterator/generator protocol producer may report its own
protocol completion as:

```text
pending.kind = StopIteration
pending.stop_iteration_value = Value::not_present() or supplied value
accumulator = Value::exception_marker()
```

This is not the ordinary call return convention. It is a protocol-completion
return that must be consumed by a protocol continuation or raised by an adapter.

For an ordinary `for` loop, the continuation consumes both `StopIteration()` and
`StopIteration(value)` as loop exhaustion and discards the payload:

```text
FOR_ITER_CONTINUE:
  if accumulator != Value::exception_marker():
    consume yielded item
    continue into the loop body

  if pending.kind == StopIteration:
    clear pending exception
    goto loop_exit_or_else

  enter managed exceptional unwind
```

For a user-visible `next(it)` call, the adapter does not consume
stop-returning completion. It promotes/materializes it as a real
`StopIteration` exception and enters ordinary exceptional unwind:

```text
NEXT_CONTINUE:
  if accumulator != Value::exception_marker():
    return value

  if pending.kind == StopIteration:
    materialize/promote StopIteration if needed
    enter managed exceptional unwind

  enter managed exceptional unwind
```

Thus the same stop-returning body can serve both:

```text
for x in it:     consumes StopIteration as completion
next(it)         raises StopIteration as a real exception
```

## Iterator Code Objects

Stop-returning support uses the same iterator object with two code-object
conventions. `iter(range_obj)` still returns a `RangeIterator`; it does not
return a separate `FastRangeIterator` depending on who will consume it.

Every `Function` has `ordinary_code_object`. A few iterator/generator protocol
functions also have optional `stop_returning_code_object`:

```text
ordinary_code_object:
  always present
  ordinary iterator protocol
  value -> return value
  exhaustion -> real StopIteration through managed exceptional unwind
  ordinary exceptions -> managed exceptional unwind

stop_returning_code_object:
  optional
  VM-internal protocol-completion convention
  value -> return value
  own protocol completion -> pending StopIteration + Value::exception_marker()
  ordinary exceptions -> managed exceptional unwind
  stop-returning completion itself generates no traceback
```

The consumer chooses the entry convention. A user-visible `next(it)` uses
`ordinary_code_object`, or an adapter that calls `stop_returning_code_object` and
converts marker completion into real `StopIteration`. A `for` loop may choose
`stop_returning_code_object` when the iterator type advertises one, while still
installing the ordinary exception-table fallback.

For `Function` objects, the two conventions are represented as separate
`CodeObject`s on the same logical function rather than separate functions:

```text
ordinary_code_object:
  always present
  ordinary call convention

stop_returning_code_object:
  optional
  stop-returning protocol-completion convention
```

This replaces the current single `Function::code_object` member with
`ordinary_code_object` and `stop_returning_code_object`. Both code objects use
the same argument calling convention, so arity checks, defaults, and call-window
layout do not depend on which one is selected. Ordinary call lookup and inline
caches select `ordinary_code_object`. A later call-site policy such as
`PreferStopReturning` may select `stop_returning_code_object` when it exists and
fall back to `ordinary_code_object` otherwise.

When the conventions differ, the preferred representation is two `CodeObject`s
attached to one logical `Function`, rather than two offsets in the same
`CodeObject`. The existing function-call inline cache already stores the
selected `CodeObject`, so a `FOR_ITER` cache can keep the decision to call the
`stop_returning_code_object` without changing the logical iterator object or
function.

Separate `CodeObject`s also make traceback policy cleaner. If
`ordinary_code_object` is a managed adapter or thunk that should be hidden, and
`stop_returning_code_object` is the real implementation, the hide flag can remain
per `CodeObject` instead of becoming a per-entry special case.

Native functions are already wrapped in managed `Function` thunks, so a native
stop-returning function can have both an `ordinary_code_object` thunk and a
`stop_returning_code_object` thunk. These are sibling thunk bodies that both call
the same native implementation directly. The ordinary thunk normalizes native
failure and stop-returning completion into ordinary managed exceptional unwind;
the stop-returning thunk leaves its own protocol completion as pending
`StopIteration` plus `Value::exception_marker()`.

```text
ordinary native thunk:
  CallNativeN
  ReturnOrRaiseException

stop-returning native thunk:
  CallNativeN
  ReturnStopIterationOrRaiseException
```

This keeps the native implementation as the shared low-level body without making
one thunk variant call the other.

`ReturnStopIterationOrRaiseException` is the stop-returning sibling of
`ReturnOrRaiseException`:

```text
if accumulator != Value::exception_marker():
  normal Return

if accumulator == Value::exception_marker() and pending.kind == StopIteration:
  return marker normally for a protocol continuation to consume

if accumulator == Value::exception_marker() and pending.kind != StopIteration:
  pending exception must be set
  enter managed exceptional unwind
```

Marker with no pending exception is an internal VM error.

`stop_returning_code_object` is not a Python-visible `__fast_next__`
attribute. CloverVM's current object model stores objects with shapes, so the
open implementation question is where this VM-internal second code object should
live: exact builtin-class metadata, shape metadata, a side dispatch table keyed
by iterator class/shape, or another equivalent internal descriptor.

## Stop-Returning Eligibility

Stop-returning participation is a capability of the iterator/resumable target,
not a global exception rule.

Near term, only VM-known iterator objects such as `RangeIterator` need to
participate. `FOR_ITER` can decide the plan once during loop setup and then run
the selected plan on each iteration.

Later, Python-authored generators and iterator adapters can participate if their
compiled code is safe for stop-returning completion. A conservative rule for
Python code is:

- it is a generator/resumable iterator body, or an explicitly recognized
  iterator-protocol function
- it may complete by generator return or recognized `StopIteration`
- any `StopIteration` it completes with is produced by the function's own
  iterator/generator protocol completion, not by an arbitrary escaping callee
- its exception table metadata can distinguish protocol completion from ordinary
  exception propagation

Stop-returning eligibility does not mean the function cannot raise. It may still
have `try`, `with`, `finally`, cleanup ranges, and exception table entries. It
may also call arbitrary Python code whose exceptions leak back through the
function. Those exceptions always use the ordinary managed exception path.

Only protocol `StopIteration` completion is allowed to leave the function as
pending compact `StopIteration` plus `Value::exception_marker()`. If a
`StopIteration` is raised as an ordinary exception, is thrown by a callee, or
would pass through a user-visible handler/cleanup range before becoming loop
completion, it must remain on the ordinary exception path. That means user
handlers can catch it and cleanup tables run.

This keeps the stop-returning path local and conservative: compact
`StopIteration` may not skip over user-visible handlers or cleanup, while other
exceptions are never encoded as stop-returning completion.

## For Loop Belt And Braces

Long term, `for` loop lowering should include both a stop-returning continuation
and an ordinary exception-table fallback:

```text
stop-returning path:
  marker + pending StopIteration -> FOR_ITER_CONTINUE exits the loop

ordinary exception path:
  real StopIteration -> synthetic exception-table handler exits the loop
```

This lets stop-returning iterators avoid materialization while preserving ordinary
Python semantics for generic `__next__` calls. The same loop exit/else target is
used by both paths.

Until generic `__next__`, exception tables, and generators exist, the first
implementation can remain narrow: `RangeIterator` participates in the
stop-returning loop path, and unsupported iterator objects keep the current
explicit error path.

## Iterator Exhaustion Versus StopIteration

There are related but distinct representations:

```text
internal iterator exhaustion:
  private VM helper result
  not a Python exception
  no traceback
  must not escape into user-visible value flow

stop-returning protocol StopIteration:
  pending compact StopIteration + exception_marker
  no traceback
  valid only for protocol continuations or adapters
  consumed by for/yield-from-style machinery or promoted to a real exception

real Python StopIteration:
  pending Python exception
  follows managed exceptional unwind
  materializes into an ordinary exception object when observed
```

`RangeIterator` and other VM-known iterators may use internal helper results
inside their implementation, but the external stop-returning convention speaks
in terms of pending `StopIteration` plus `Value::exception_marker()`.

## Table Unwinding

Within a managed frame, exception handling is table-driven. An opcode that
raises from inside a protected region enters table unwinding at the current pc:

The first bytecode form is just an interpreted-pc triple:

```cpp
struct ExceptionTableEntry
{
    uint64_t start_pc;    // inclusive, local to this CodeObject
    uint64_t end_pc;      // exclusive, local to this CodeObject
    uint64_t handler_pc;  // landing pad pc in this CodeObject
};
```

There is no handler kind or stack-depth field in the initial shape. A frame
without protected regions has no entries. The compiler owns register liveness at
the landing pad; handler code treats protected-region temporaries as dead unless
codegen explicitly preserves them.

Entries may overlap. Lookup is priority ordered: the unwinder returns the first
entry whose half-open range covers the lookup pc. Codegen emits entries in
priority order, with innermost handlers before enclosing handlers. A later table
builder may flatten ranges for binary search, but that is an optimization rather
than the initial semantic model.

```text
find handler covering current pc in this frame's exception metadata
if found:
  unwind fp to this managed frame
  accumulator = active exception object
  pending exception remains active
  jump to handler_pc in the same CodeObject
else:
  the exception escapes this frame
```

Handler bytecode performs Python-level decisions such as `except NameError as e`
matching, handler binding, synthetic `for` `StopIteration` checks, cleanup, or
continuing exceptional unwind. The table itself is structural metadata; it does
not encode arbitrary Python exception classes.

A matching handler clears the pending exception when it takes ownership of the
exception. Reraise, `finally`, and cleanup paths can re-enter the unwinder
without modifying pending exception state.

For an exception raised by the current instruction, table lookup uses that
instruction's pc. When an exception escapes a callee and unwinding continues in
an interpreted caller, the saved return pc points to the next instruction after
the call. The caller-side lookup uses `return_pc - 1`, which is guaranteed to be
inside the variable-length call instruction. Codegen must make protected call
instruction ranges include that byte so half-open table lookup finds the handler
attached to the call.

For source-level handlers, names in exception expressions are evaluated by the
handler code, so rebinding globals such as `NameError` affects explicit
`except NameError` semantics. Synthetic VM handlers such as `for` loop
`StopIteration` handling use runtime-known protocol semantics instead.

When no local handler is found, the VM pops/restores the caller frame and
continues the exceptional path there. Future JIT frames participate with their
own managed unwind metadata; the distinction is managed Clover frame versus
native implementation detail, not bytecode versus native code object.

## Raise Opcodes

The compiler can avoid local table lookup at raise sites that are known not to
be inside any local exception-protected range:

```text
RAISE_UNWIND:
  used when the instruction may be covered by this frame's exception metadata
  creates/sets the pending exception
  enters table unwinding at the current pc

RAISE_FAST:
  used only when the instruction is outside all local protected ranges
  creates/sets the pending exception
  exits this frame on the slow exceptional path
```

`RAISE_FAST` is a compiler invariant, not a dynamic guess. It is only valid for
bytecode offsets that no `try`, `with`, `finally`, or other exceptional cleanup
range can cover.

## Lazy Tracebacks

Traceback construction should be lazy.

At a raise site, the VM records enough state to reconstruct the traceback later:

```text
code object
interpreter pc or compiled pc
current frame pointer or preserved frame metadata
previous traceback chain, if any
```

Protocol completion consumed by `FOR_ITER` is not user-visible exception
propagation and does not attach the caller frame to a traceback.

The no-traceback rule belongs to stop-returning protocol completion, not to
every frame that has `stop_returning_code_object`. If
`stop_returning_code_object` returns `Value::exception_marker()` with pending
`StopIteration` as its own completion signal, that signal has no traceback. If
the same stop-returning completion is promoted to a real `StopIteration`,
traceback handling resumes through the ordinary exceptional path from the
promotion site. The completed stop-returning activation is not retroactively
added.

Stop-returning managed frames may still raise ordinary exceptions or let
ordinary callee exceptions escape. Those exceptions use managed exceptional
unwind and contribute traceback segments under the normal rules. If a callee has
already raised through ordinary managed exception machinery, its existing
traceback state is preserved.

Adapter and thunk frames remain visible to the unwinder, but can be hidden from
user-visible tracebacks. If stop-returning completion is promoted in a thunk or
adapter, that adapter is the technical raise point, and its code object should be
marked `HideFromTraceback` so user-visible tracebacks start at the caller-facing
frame. The same hiding policy applies to native adapter failures; this is close
to CPython's behavior for exceptions crossing C implementation code.

The traceback remains lazy until it becomes observable or until the VM is about
to invalidate stack memory needed by the lazy segment.

Observation boundaries include:

- `e.__traceback__`
- traceback formatting
- debugger or profiler hooks
- APIs that expose frame or traceback objects

Stack-lifetime boundaries include:

- calling Python code from an exception handler
- invoking a builtin that may re-enter the VM or inspect exception state
- any operation that may reuse the frame-stack region referenced by a pending
  lazy traceback segment

Entering an exception handler does not by itself require formatting or fully
materializing the traceback. However, before that handler calls out, the VM must
preserve any stack-backed traceback segment that could be overwritten.

Thunk code objects should remain visible to the unwinder but may later carry a
flag such as `HideFromTraceback` so adapter frames do not pollute user-visible
tracebacks.

## Frame-Stack Constraint

CloverVM frames currently have a known layout:

- each `CodeObject` has a fixed register footprint
- the frame header links to the previous frame pointer
- the frame header stores the return code object and return pc
- function calls reinterpret an existing call window as the callee frame

That layout is useful for lazy traceback capture because the VM can walk frames
from `fp`.

Dead callee-frame storage may be reused by later calls from the handler frame.
Therefore lazy traceback segments may refer to stack memory only while the VM can
prove that memory will not be overwritten.

Before stack reuse, the VM must either:

- materialize the traceback segment into heap-owned traceback/frame records, or
- copy the minimal traceback metadata needed to reconstruct the segment later

## Reraise

A bare `raise` preserves the current logical exception but starts a new
traceback segment at the reraise site.

Before creating the new segment, the VM should materialize or otherwise
preserve the existing traceback chain. This keeps the old chain independent of
the current stack lifetime, then allows the reraise site to be represented by a
fresh lazy segment.

For a compact pending `StopIteration`, reraise promotes it into the ordinary
exception path if needed, preserves `Value::not_present()` versus supplied
`None`, and starts the new lazy traceback segment from the reraise location.

## JIT Direction

The JIT should see Python-authored iterator adapters as ordinary generator or
bytecode bodies where possible. Builtins such as `enumerate`, `zip`, and `map`
should move toward Python-shaped implementations so the JIT can analyze through
nested loops and protocol boundaries.

Useful eventual lowerings:

```text
eligible generator return value:
  write pending.kind = StopIteration
  write pending.stop_iteration_value = value
  return Value::exception_marker() to the protocol continuation

FOR_ITER_CONTINUE:
  consume marker + pending StopIteration as loop completion

ReturnOrRaiseException:
  convert marker + pending exception into managed exceptional unwind
```

## Staging

Recommended implementation order:

1. Add pending exception state to `ThreadState`.
2. Add an exceptional frame-exit path distinct from normal `Return`.
3. Add `ReturnOrRaiseException` for managed thunks/adapters.
4. Convert native thunk bodies to normalize native failures through
   `ReturnOrRaiseException`.
5. Add the compact pending `StopIteration` representation and helpers.
6. Add a stop-returning path for `RangeIterator`.
7. Split `FOR_ITER` into a stop-returning call/continuation shape that consumes
   marker + pending `StopIteration`.
8. Add exception tables and synthetic `for` loop handlers so real
   `StopIteration` from generic `__next__` exits the loop through the ordinary
   exception path.
9. Add parser/codegen/runtime support for `raise`, using `RAISE_FAST` only
   outside protected regions.
10. Add materialization helpers for compact `StopIteration` and lazy traceback
    segments.
11. Add Python generators and mark eligible generator code objects as
    stop-returning participants when codegen can distinguish their own protocol
    completion from ordinary exceptions and callee failures.
12. Add reraise support that preserves the existing traceback chain and starts a
    fresh lazy traceback segment.

## Invariants

- Normal return uses the accumulator and saved return pc only.
- Ordinary opcodes do not inspect the accumulator for
  `Value::exception_marker()`.
- `Value::exception_marker()` is only observed by explicit protocol
  continuations and managed return adapters.
- Real Python exceptions live as pending exception state on `ThreadState`.
- Managed exception propagation is table-driven and slow.
- The frame header is not tagged for exception return modes.
- `fp[1]` remains reserved for native pc / future compiled-frame metadata.
- Managed thunks adapt native/protocol result conventions into normal return or
  managed exceptional unwind.
- A compact pending `StopIteration` may be consumed only by protocol
  continuations that explicitly requested iterator/generator completion.
- Stop-returning participants may still raise ordinary exceptions through
  managed exception tables and may let exceptions leak from callees.
- Only protocol `StopIteration` completion uses the stop-returning marker path;
  all other exceptions, including callee exceptions, use managed exceptional
  unwind.
- Stop-returning protocol completion does not generate traceback segments.
- Stop-returning managed frames still contribute traceback segments for ordinary
  exceptions.
- If compact `StopIteration` escapes a protocol continuation or must be visible
  to user exception handling, it is promoted/materialized and follows the
  ordinary exception path.
- Ordinary `for` discards `StopIteration.value`; `yield from`-style delegation
  may inspect it.
- `StopIteration()` uses `Value::not_present()`, distinct from
  `StopIteration(None)`.
- User exception handlers and cleanup ranges must not be skipped by compact
  protocol completion.
- General user exception classes are constructed immediately.
- Lazy traceback segments may reference frame-stack memory only while that
  memory is protected from reuse.
- Reraise preserves the logical exception, materializes or preserves the
  existing traceback chain, and starts a new lazy traceback segment.
