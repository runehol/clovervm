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

```text
exceptional frame exit:
  pending exception is set on ThreadState
  consult this frame's managed unwind metadata
  jump to a handler if one applies
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
  enter managed exceptional unwind
```

Only explicit adapter or protocol continuation opcodes observe
`Value::exception_marker()`. Ordinary opcodes and ordinary returns do not.

Native functions participate through thunks. A native success writes a normal
`Value` to the accumulator. A native failure sets pending exception state and
writes `Value::exception_marker()`. The thunk's `ReturnOrRaiseException`
converts that marker into managed exceptional unwind.

The same shape adapts fast-protocol entry points back into ordinary calls. If a
caller is not participating in the fast protocol but the implementation wants to
reuse the fast convention, the adapter can select the fast entry's `CodeObject`
explicitly:

```text
LoadConst fast_protocol_code_object
CallCodeObject
ReturnOrRaiseException
```

`CallCodeObject` is an internal direct-code call. It assumes the surrounding
thunk has already prepared the argument/frame window, and it enters exactly the
supplied `CodeObject` without resolving the owning `Function`'s normal entry or
adaptation policy again. The adapter then converts any
`Value::exception_marker()` result into the ordinary managed exception path.

## StopIteration And Fast Protocol Return

`StopIteration` is special only at protocol boundaries.

A fast iterator/generator protocol producer may report completion as:

```text
pending.kind = StopIteration
pending.stop_iteration_value = Value::not_present() or supplied value
accumulator = Value::exception_marker()
```

This is not the ordinary call return convention. It is a protocol return that
must be consumed by a protocol continuation or raised by an adapter.

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

For a user-visible `next(it)` call, the adapter does not consume the compact
completion. It promotes/materializes it as a real `StopIteration` exception and
enters ordinary exceptional unwind:

```text
NEXT_CONTINUE:
  if accumulator != Value::exception_marker():
    return value

  if pending.kind == StopIteration:
    materialize/promote StopIteration if needed
    enter managed exceptional unwind

  enter managed exceptional unwind
```

Thus the same fast protocol producer can serve both:

```text
for x in it:     consumes StopIteration as completion
next(it)         raises StopIteration as a real exception
```

## Iterator Entry Points

Fast protocol support uses the same iterator object with two VM entry
conventions. `iter(range_obj)` still returns a `RangeIterator`; it does not
return a separate `FastRangeIterator` depending on who will consume it.

For a fast-capable iterator type:

```text
normal next entry:
  ordinary iterator protocol
  value -> return value
  exhaustion -> real StopIteration through managed exceptional unwind

fast next entry:
  VM-internal protocol entry
  value -> return value
  exhaustion -> pending StopIteration + Value::exception_marker()
  other exception -> pending exception + Value::exception_marker()
  no traceback segment is generated for the fast protocol frame itself
```

The consumer chooses the entry convention. A user-visible `next(it)` uses the
ordinary next entry, or an adapter that converts fast completion into real
`StopIteration`. A `for` loop may choose the fast entry when the iterator type
advertises one, while still installing the ordinary exception-table fallback.

For `Function` objects, the two entries may be represented as separate entry
descriptors on the same logical function rather than separate functions:

```text
normal_entry:
  CodeObject for ordinary call convention
  ordinary call convention

fast_protocol_entry:
  optional
  CodeObject for fast protocol convention
  fast protocol convention
```

When the conventions differ, the preferred representation is two `CodeObject`s
attached to one logical `Function`, rather than two offsets in the same
`CodeObject`. The existing function-call inline cache already stores the
selected `CodeObject`, so a `FOR_ITER` cache can keep the decision to call the
fast protocol entry without changing the logical iterator object or function.

Separate `CodeObject`s also make traceback policy cleaner. If one entry is a
managed adapter or thunk that should be hidden, and the other is user-visible
code, the hide flag can remain per `CodeObject` instead of becoming a per-entry
special case.

Native functions are already wrapped in managed `Function` thunks, so a native
fast-capable function can have both a normal thunk code object and a
fast-protocol thunk code object. The normal thunk may itself be the adapter
shape:

```text
LoadConst fast_protocol_code_object
CallCodeObject
ReturnOrRaiseException
```

This lets the fast implementation be the single low-level body while
non-participating callers still observe ordinary exception semantics.

The fast entry is not a Python-visible `__fast_next__` attribute. CloverVM's
current object model stores objects with shapes, so the open implementation
question is where this VM-internal second entry point should live: exact
builtin-class metadata, shape metadata, a side dispatch table keyed by iterator
class/shape, or another equivalent internal descriptor.

## Fast Protocol Eligibility

Fast protocol participation is a capability of the iterator/resumable target,
not a global exception rule.

Near term, only VM-known iterator objects such as `RangeIterator` need to
participate. `FOR_ITER` can decide the plan once during loop setup and then run
the selected plan on each iteration.

Later, Python-authored generators and iterator adapters can participate if their
compiled code is safe for compact protocol completion. A conservative rule for
Python code is:

- it is a generator/resumable iterator body, or an explicitly recognized
  iterator-protocol function
- it may complete by generator return or recognized `StopIteration`
- any `StopIteration` it completes with is produced by the function's own
  iterator/generator protocol completion, not by an arbitrary escaping callee
- its exception table metadata can distinguish protocol completion from ordinary
  exception propagation

Fast protocol eligibility does not mean the function cannot raise. It may still
have `try`, `with`, `finally`, cleanup ranges, and exception table entries. It
may also call arbitrary Python code whose exceptions leak back through the
function. Those exceptions always use the ordinary managed exception path.

Only protocol `StopIteration` completion is allowed to leave the function as
pending compact `StopIteration` plus `Value::exception_marker()`. If a
`StopIteration` is raised as an ordinary exception, is thrown by a callee, or
would pass through a user-visible handler/cleanup range before becoming loop
completion, it must remain on the ordinary exception path. That means user
handlers can catch it and cleanup tables run.

This keeps the fast path local and conservative: compact `StopIteration` may not
skip over user-visible handlers or cleanup, while other exceptions are never
encoded as fast protocol completion.

## For Loop Belt And Braces

Long term, `for` loop lowering should include both a fast protocol continuation
and a normal exception-table fallback:

```text
fast protocol path:
  marker + pending StopIteration -> FOR_ITER_CONTINUE exits the loop

ordinary exception path:
  real StopIteration -> synthetic exception-table handler exits the loop
```

This lets fast-protocol iterators avoid materialization while preserving normal
Python semantics for generic `__next__` calls. The same loop exit/else target is
used by both paths.

Until generic `__next__`, exception tables, and generators exist, the first
implementation can remain narrow: `RangeIterator` participates in the fast
protocol, and unsupported iterator objects keep the current explicit error path.

## Iterator Exhaustion Versus StopIteration

There are related but distinct representations:

```text
internal iterator exhaustion:
  private VM helper result
  not a Python exception
  no traceback
  must not escape into user-visible value flow

fast protocol StopIteration:
  pending compact StopIteration + exception_marker
  no traceback
  valid only for protocol continuations or adapters
  consumed by for/yield-from-style machinery or promoted to a real exception

real Python StopIteration:
  pending Python exception
  follows managed exceptional unwind
  materializes into a normal exception object when observed
```

`RangeIterator` and other VM-known iterators may use internal helper results
inside their implementation, but the external fast protocol speaks in terms of
pending `StopIteration` plus `Value::exception_marker()`.

## Table Unwinding

Within a managed frame, exception handling is table-driven. An opcode that
raises from inside a protected region enters table unwinding at the current pc:

```text
find handler covering current pc in this frame's exception metadata
if found:
  trim frame state to the handler depth
  arrange pending exception state expected by the handler
  jump to the handler target in the same frame
else:
  the exception escapes this frame
```

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

Fast iterator protocol execution does not generate traceback segments. If a
fast entry returns `Value::exception_marker()` with pending `StopIteration`, the
completion has no traceback. If the same compact completion is promoted to a
real `StopIteration`, traceback handling resumes through the ordinary
exceptional path from the promotion site. The fast protocol frame itself is not
retroactively added. If promotion happens in a thunk or adapter, that adapter is
the technical raise point, and its code object should be marked
`HideFromTraceback` so user-visible tracebacks start at the caller-facing frame.

The same rule applies to other exceptions reported by a native or VM-owned fast
entry: the fast protocol boundary does not synthesize traceback frames. This is
close to CPython's behavior for exceptions crossing C implementation code. If a
callee has already raised through ordinary managed exception machinery, its
existing traceback state is preserved, but the fast protocol frame does not add
another traceback segment.

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
6. Add a fast protocol path for `RangeIterator`.
7. Split `FOR_ITER` into a fast protocol call/continuation shape that consumes
   marker + pending `StopIteration`.
8. Add exception tables and synthetic `for` loop handlers so real
   `StopIteration` from generic `__next__` exits the loop through the ordinary
   exception path.
9. Add parser/codegen/runtime support for `raise`, using `RAISE_FAST` only
   outside protected regions.
10. Add materialization helpers for compact `StopIteration` and lazy traceback
    segments.
11. Add Python generators and mark eligible generator code objects as fast
    protocol participants when codegen can distinguish their own protocol
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
- Fast protocol participants may still raise ordinary exceptions through managed
  exception tables and may let exceptions leak from callees.
- Only protocol `StopIteration` completion uses the fast protocol marker path;
  all other exceptions, including callee exceptions, use managed exceptional
  unwind.
- Fast iterator protocol execution does not generate traceback segments for the
  fast protocol frame itself.
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
