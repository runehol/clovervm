# Lazy Exceptions And Tracebacks

## Goal

Define an exception substrate that keeps normal exception semantics correct while
making iterator exhaustion and traceback capture cheap on hot paths.

The main performance motivation is `StopIteration`. Today, for-loop support can
avoid Python-visible `StopIteration` by treating exhaustion as an internal VM
result. That is a good first step, but it is not enough for the long-term VM:

- Python-authored `raise StopIteration(value)` should be fast.
- The eventual JIT should be able to see and optimize `StopIteration` control
  flow directly.
- `range()` and other iterator machinery should be able to move toward Python
  implementations without forcing hot-loop exception allocation.
- If `StopIteration` is raised from Python code and becomes observable, it must
  still have correct exception and traceback behavior.

The design therefore treats `StopIteration` as a special lazy exception while
keeping arbitrary user exceptions conservative.

## Core Model

All real Python exceptions propagate through the interpreter as:

```cpp
Value::exception_marker()
```

The marker is only a return value. The actual exception state lives on
`ThreadState`.

Conceptually:

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
    Value object;

    // Used when kind == StopIteration. Value::not_present() means no value.
    Value stop_iteration_value;

    LazyTraceback traceback;
};
```

Most exceptions are represented by an already-realized exception object plus a
lazy traceback.

`StopIteration` is represented by a compact payload plus a lazy traceback:

```text
pending.kind = StopIteration
pending.stop_iteration_value = value
pending.traceback = lazy segment from the raise site
```

This is still a real pending Python exception. It can be caught, reraised,
materialized, and observed like a normal `StopIteration`. The optimization is
only its representation while it remains internal to the VM.

## Non-Goals

This design does not make arbitrary exception construction lazy by default.

For a general exception class, construction may run Python code through
`__new__` or `__init__`, mutate state, observe globals, or raise another
exception. Those side effects are part of raising the exception and must not be
silently delayed.

General lazy exception objects can be considered later only for whitelisted
built-in classes whose construction is known to be side-effect-free or otherwise
semantically acceptable to defer.

## StopIteration

`StopIteration` gets a special pending-exception representation because it is
both semantically important and performance-critical.

When Python code executes:

```python
raise StopIteration(value)
```

the VM can lower that to:

```text
set pending exception kind to StopIteration
store value
record lazy traceback segment
return Value::exception_marker()
```

No exception object is required on the hot path.

`StopIteration` may be raised with or without a value. The compact pending form
uses:

```text
pending.stop_iteration_value = Value::not_present()
```

to represent the no-value case. This keeps "no value was supplied" distinct
from a supplied `None` value.

Consumers that understand iterator protocol can test the pending kind directly:

```text
FOR_ITER sees Value::exception_marker()
  if pending.kind == StopIteration:
    clear pending exception
    jump to loop exit
  else:
    propagate exception marker
```

An `except StopIteration` handler can also match the pending kind without
materializing the object.

Materialization is required when the exception object itself becomes observable,
for example:

- binding the exception as `except StopIteration as e`, unless the VM later adds
  a lazy bound-exception handle
- accessing exception attributes
- exposing the exception through APIs
- formatting, debugging, or inspecting the traceback
- escaping into runtime code that requires a concrete object

At materialization, the VM constructs a normal `StopIteration` object whose
observable state matches the compact pending payload.

## Iterator Exhaustion Versus StopIteration

There are two related but distinct concepts:

```text
internal iterator exhaustion:
  a VM protocol result
  not an exception
  no traceback
  must not escape into user-visible value flow

Python StopIteration:
  a real exception
  represented lazily while pending
  has a lazy traceback if raised from Python-visible code
  materializes into a normal exception object when observed
```

The VM may keep an internal exhaustion result for built-in iterators such as a
range iterator. That result should not be named or treated as an exception
marker. It is simply a fast local protocol between iterator helpers and
`FOR_ITER`.

Python-raised `StopIteration` must use the real exception path:

```text
Value::exception_marker()
ThreadState::pending_exception.kind = StopIteration
```

This split keeps fast built-in iterators cheap while still allowing
Python-authored iterators to become JIT-visible and allocation-free on the
common exhaustion path.

## Lazy Tracebacks

Traceback construction should also be lazy.

At a raise site, the VM records enough state to reconstruct the traceback later:

```text
code object
interpreter pc
current frame pointer
previous traceback chain, if any
```

During propagation, the VM can walk frame metadata directly instead of executing
cleanup or setup bytecode while searching for a handler. Exception tables should
drive this unwinding once `try` / `except` support exists.

The traceback remains lazy until it becomes observable or until the VM is about
to invalidate the stack memory needed by the lazy segment.

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

## Frame-Stack Constraint

CloverVM frames currently have a known layout:

- each `CodeObject` has a fixed register footprint
- the frame header links to the previous frame pointer
- the frame header stores the return code object and return pc
- function calls reinterpret an existing call window as the callee frame

That layout is useful for lazy traceback capture because the VM can walk frames
from `fp`.

The important constraint is that dead callee-frame storage may be reused by
later calls from the handler frame. Therefore lazy traceback segments may refer
to stack memory only while the VM can prove that memory will not be overwritten.

Before stack reuse, the VM must either:

- materialize the traceback segment into heap-owned traceback/frame records, or
- copy the minimal traceback metadata needed to reconstruct the segment later

This differs from CPython's ability to promote interpreter frames into frame
objects on demand, but it follows the same broad principle: stack-backed frame
state can stay lazy only while its lifetime is protected.

## Reraise

A bare `raise` preserves the current logical exception but starts a new
traceback segment at the reraise site.

Before creating the new segment, the VM should materialize or otherwise
preserve the existing traceback chain. This keeps the old chain independent of
the current stack lifetime, then allows the reraise site to be represented by a
fresh lazy segment.

Conceptually:

```text
materialized existing traceback chain
+ new lazy segment from reraise site
```

For a pending lazy `StopIteration`, reraise keeps the compact stop-iteration
payload, preserving `Value::not_present()` if no value was supplied, and starts
the new lazy traceback segment from the reraise location. If later materialized,
the result must look like a normal `StopIteration` object with the complete
traceback chain.

The storage order can be append-only or prepend-oriented internally, as long as
traceback materialization and formatting produce Python-compatible ordering.

## JIT Direction

The JIT should be able to lower known `StopIteration` raises and catches without
crossing an opaque C helper boundary.

Useful eventual lowerings:

```text
raise StopIteration(value)
  write pending.kind = StopIteration
  write pending.stop_iteration_value = value
  write lazy traceback metadata
  branch to unwind path

FOR_ITER catch point
  if return != exception_marker:
    consume yielded value
  else if pending.kind == StopIteration:
    clear pending
    exit loop
  else:
    propagate
```

This makes Python-implemented iterators and eventually Python-implemented
`range()` viable without making every normal loop allocate and throw an
exception object at exhaustion.

## Staging

Recommended implementation order:

1. Add pending exception state to `ThreadState`.
2. Convert existing runtime failures from C++ `throw` to
   `Value::exception_marker()` plus a realized pending exception object.
3. Add lazy traceback metadata for pending exceptions.
4. Add the compact pending `StopIteration` representation.
5. Teach iterator protocol consumers to recognize pending `StopIteration`.
6. Add materialization helpers for lazy `StopIteration` and lazy traceback
   segments.
7. Add exception tables and handler unwinding for `try` / `except`.
8. Add reraise support that preserves the existing traceback chain and starts a
   fresh lazy traceback segment.

The first working version does not need arbitrary lazy exception objects.
`StopIteration` is the intentional special case.

## Invariants

- `Value::exception_marker()` means a real pending exception exists on
  `ThreadState`.
- A compact pending `StopIteration` is a real Python exception, not an internal
  exhaustion result.
- Internal iterator exhaustion is not an exception and must not escape into
  user-visible value flow.
- General user exception classes are constructed immediately.
- Lazy traceback segments may reference frame-stack memory only while that
  memory is protected from reuse.
- Before a pending lazy traceback crosses a stack-lifetime boundary, the VM must
  preserve or materialize the segment.
- `StopIteration` with no supplied value is represented as
  `Value::not_present()`, not `None`.
- Reraise preserves the logical exception, materializes or preserves the
  existing traceback chain, and starts a new lazy traceback segment.
