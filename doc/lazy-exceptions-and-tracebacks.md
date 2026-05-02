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

Real Python exceptions live as pending exception state on `ThreadState`.
The pending exception lives there regardless of how it crosses a frame boundary.
The return mode only decides whether the VM keeps unwinding frames until it
finds a handler, or returns a sentinel to the immediate caller.

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

When an exception escapes a frame, the caller decides how that exceptional exit
is delivered. There are two return modes for the same pending exception:

```text
ViaUnwind:
  continue unwinding bytecode frames
  consult exception tables until a handler is found or the exception escapes

ViaResult:
  stop unwinding at this boundary
  return Value::exception_marker()
  the caller resumes at the saved return pc and may consume protocol exceptions
```

Normal function return does not participate in this classification. A successful
return restores the caller frame and jumps to the saved return pc with the return
value in the accumulator. `ViaUnwind` versus `ViaResult` matters only after the
callee has failed to handle a pending exception locally.

The return mode is encoded in the tagged return target stored in `fp[-1]`.
Ordinary call sites tag the callee frame for `ViaUnwind`. Protocol call sites
tag the callee frame for `ViaResult` and set the saved return pc to a
continuation opcode, such as the second half of `FOR_ITER`. Native/C boundaries
use the same result-style convention: stop VM unwinding at the native boundary
and return a C sentinel while the pending exception remains in thread-local
storage.

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
enter the exceptional unwind path
```

No exception object is required on the hot path.

`StopIteration` may be raised with or without a value. The compact pending form
uses:

```text
pending.stop_iteration_value = Value::not_present()
```

to represent the no-value case. This keeps "no value was supplied" distinct
from a supplied `None` value.

For an ordinary `for` loop, the `StopIteration` value is ignored and
`FOR_ITER2` jumps to the loop exit in both StopIteration cases. Delegating
iteration machinery, such as `yield from`, uses the same shape but takes
different paths for no supplied value versus a supplied value. `Value::not_present()`
represents a `StopIteration` raised with no value, distinct from a supplied
`None`.

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
ThreadState::pending_exception.kind = StopIteration
exceptional unwind path
```

This split keeps fast built-in iterators cheap while still allowing
Python-authored iterators to become JIT-visible and allocation-free on the
common exhaustion path.

## Exceptional Return Modes

The VM distinguishes normal return from exceptional frame exit.

```text
normal return:
  restore caller fp, code object, and return pc from the frame header
  keep the returned value in the accumulator
  dispatch at the restored return pc

exceptional frame exit:
  restore the caller frame
  inspect the return target
  either keep unwinding or return Value::exception_marker() / a native sentinel
```

`ViaUnwind` is the ordinary path for function calls. If the callee raises and no
handler in the callee catches the exception, table unwinding continues in the
caller using the saved return pc.

`ViaResult` is for protocol boundaries that need to classify a specific
exception before ordinary propagation. The callee still uses its own exception
table first. `ViaResult` only changes what happens after the exception escapes
the callee frame: unwinding stops, the same pending exception remains on
`ThreadState`, and the caller resumes at its saved return pc with
`Value::exception_marker()` in the accumulator.

The important initial `ViaResult` user is iteration:

```text
FOR_ITER1:
  performs the iterator-next call
  saves return_pc = FOR_ITER2

FOR_ITER2:
  if accumulator != Value::exception_marker():
    consume yielded item
    continue into the loop body
  else if pending.kind == StopIteration:
    clear pending exception
    goto loop_exit
  else:
    promote to ordinary propagation
```

This keeps `StopIteration` special only at the protocol boundary that asked for
the next item. An ordinary `for` loop treats both `StopIteration()` and
`StopIteration(value)` as loop exhaustion and discards the payload. Delegating
iteration machinery such as `yield from` can inspect the same pending
`stop_iteration_value` before clearing the exception. A user-visible call to
`it.__next__()` uses the ordinary call path; if it raises `StopIteration`, an
enclosing `except StopIteration` can catch it.

Because instructions are variable length, the VM should not recover the call
site by subtracting from `return_pc`. The tagged return target says whether an
exceptional frame exit keeps unwinding or returns a marker. For `ViaResult`, the
VM restores the caller, writes `Value::exception_marker()` to the accumulator,
and jumps to the saved return pc. Splitting protocol operations into a call
opcode and a continuation opcode makes this cheap and local.

## Table Unwinding

Within a bytecode frame, exception handling is table-driven. An opcode that
raises from inside a protected region enters table unwinding at the current pc:

```text
find handler covering current pc in this CodeObject's exception table
if found:
  trim the frame's register/temporary stack to the handler depth
  arrange the pending exception state expected by the handler
  jump to the handler target in the same frame
else:
  the exception escapes this frame
```

When no local handler is found, the VM pops the frame and inspects the saved
return target in the callee's frame header:

```text
if return target is BytecodeViaUnwind:
  restore the caller frame
  continue table unwinding in the caller at the saved return pc

if return target is BytecodeViaResult:
  restore the caller frame
  stop table unwinding
  put Value::exception_marker() in the accumulator
  dispatch the saved return pc normally

if return target is Native:
  stop bytecode table unwinding
  convert the pending exception to the native/C result convention
```

This is the key interaction between exception tables and protocol continuations.
Table unwinding owns ordinary Python exception propagation. `ViaResult` is an
explicit boundary where table unwinding stops so the caller's continuation can
classify the exception first. If the continuation declines to consume the
exception, it promotes it back into ordinary `ViaUnwind` propagation at that
bytecode site.

For `FOR_ITER`, this means a `StopIteration` escaping `__next__` unwinds through
the callee's own handlers, then stops at the `BytecodeViaResult` return target
and returns `Value::exception_marker()` to `FOR_ITER2`. The caller's surrounding
`try` table is not consulted unless `FOR_ITER2` decides the exception is not
protocol exhaustion.

## Raise Opcodes

The compiler can avoid table lookup at raise sites that are known not to be
inside any local exception-protected range. This gives two explicit raise paths:

```text
RAISE_UNWIND:
  used when the instruction may be covered by this frame's exception table
  creates/sets the pending exception
  enters table unwinding at the current pc

RAISE_FAST:
  used only when the instruction is outside all local exception handlers
  creates/sets the pending exception
  skips local table lookup
  immediately exits the current frame and inspects the saved return target
```

`RAISE_FAST` is a compiler invariant, not a dynamic guess. It is only valid for
bytecode offsets that no `try`, `with`, `finally`, or other exceptional cleanup
range can cover. If the instruction may be protected, codegen must emit
`RAISE_UNWIND`.

The fast path is still semantically complete because it does not ignore caller
state. It simply knows there is no handler in the current frame:

```text
RAISE_FAST:
  pending exception is set
  pop current frame
  inspect tagged return target
  BytecodeViaUnwind -> continue table unwinding in caller
  BytecodeViaResult -> return Value::exception_marker() to caller
  Native -> return native/C sentinel
```

Other opcodes that can fail can use the same idea through a per-instruction
protected bit or protected opcode variant. The important distinction is the same:
protected failures enter local table unwinding, while unprotected failures can go
straight to return-target classification.

## Native And C API Boundaries

Calls that return through C or the Python C API cannot rely on bytecode
continuation opcodes. They must use a result/sentinel convention compatible with
C:

```text
success:
  return a normal C result

failure:
  leave the pending Python exception on ThreadState / PyErr state
  return NULL, -1, or another API-specific sentinel
```

This is the native analogue of `BytecodeViaResult`. Both forms return a sentinel
and leave the pending exception in thread state. The exception is not delivered
to a bytecode exception table until a continuation chooses to promote it.

The frame header should keep the saved return pc as an honest bytecode pc. To
avoid fake pc sentinels, tag the saved return target in `fp[-1]`, which currently
stores the interpreter return code object:

```text
fp[-1] tag = BytecodeViaUnwind:
  masked pointer is a CodeObject*
  fp[-2] is a raw bytecode return pc

fp[-1] tag = BytecodeViaResult:
  masked pointer is a CodeObject*
  fp[-2] is a raw bytecode return pc that will receive Value::exception_marker()

fp[-1] tag = Native:
  masked pointer is a NativeReturnDescriptor*
  fp[-2] is native return metadata, a cookie, or otherwise descriptor-defined
```

Object and descriptor pointers are aligned, so low-bit tagging is safer here
than tagging `fp[-2]`: bytecode return pcs are byte-addressed and variable-length
instructions do not guarantee spare low bits.

All frame-header access should go through helpers that decode the return target:

```cpp
enum class ReturnTargetKind
{
    BytecodeViaUnwind,
    BytecodeViaResult,
    Native,
};

struct ReturnTarget
{
    ReturnTargetKind kind;
    union {
        CodeObject *code_object;
        NativeReturnDescriptor *native;
    };
};
```

On exceptional frame exit, native targets can be handled without first loading a
`CodeObject`. Bytecode targets use the tag first, then the saved pc:

```text
if return target is Native:
  convert pending exception to the native/C result convention
else if return target is BytecodeViaResult:
  restore the caller with Value::exception_marker() in the accumulator
  dispatch the opcode at fp[-2] normally
else:
  enter ordinary bytecode unwinding for the caller
```

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

If an exception is returned through `ViaResult` and consumed by the continuation,
the caller frame is not attached to the traceback. That path is protocol control
flow, not user-visible exception propagation. If the continuation declines to
consume the exception marker, it is promoted to ordinary propagation and
traceback attachment proceeds from that bytecode site.

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

FOR_ITER1 / FOR_ITER2
  use the ViaResult iterator continuation described above
```

This makes Python-implemented iterators and eventually Python-implemented
`range()` viable without making every normal loop allocate and throw an
exception object at exhaustion.

## Staging

Recommended implementation order:

1. Add pending exception state to `ThreadState`.
2. Add an exceptional frame-exit path distinct from normal `Return`.
3. Add `ViaUnwind` handling for ordinary bytecode calls.
4. Split iterator bytecode into call and continuation opcodes, such as
   `FOR_ITER1` and `FOR_ITER2`.
5. Add `ViaResult` handling keyed by the tagged return target in `fp[-1]`.
6. Extend the `fp[-1]` tag so bytecode `ViaUnwind`, bytecode `ViaResult`, and
   native/C return targets are distinguishable without fake pc sentinels.
7. Add `RAISE_UNWIND` and `RAISE_FAST`, with codegen emitting `RAISE_FAST` only
   outside local exception-protected regions.
8. Add lazy traceback metadata for pending exceptions.
9. Add the compact pending `StopIteration` representation.
10. Teach iterator protocol continuations to recognize pending `StopIteration`.
11. Add materialization helpers for lazy `StopIteration` and lazy traceback
   segments.
12. Add exception tables and handler unwinding for `try` / `except`.
13. Add reraise support that preserves the existing traceback chain and starts a
   fresh lazy traceback segment.

The first working version does not need arbitrary lazy exception objects.
`StopIteration` is the intentional special case.

## Invariants

- Real Python exceptions live as pending exception state on `ThreadState`; they
  do not require normal returns to carry an exception marker.
- Normal return uses the accumulator and saved return pc only; exception
  classification happens only on exceptional frame exit.
- The tagged return target in `fp[-1]` determines whether an exceptional return
  is bytecode `ViaUnwind`, bytecode `ViaResult`, or native/C result return.
- For bytecode `ViaResult`, the saved return pc names the continuation opcode
  that receives `Value::exception_marker()` in the accumulator.
- Native/C return targets are represented by the same tagged `fp[-1]` return
  target, not by fake bytecode pc values in `fp[-2]`.
- Table unwinding stops at `ViaResult` and native return targets. It resumes
  ordinary caller table search only across `BytecodeViaUnwind` targets.
- `RAISE_FAST` is valid only for bytecode offsets outside all local
  exception-protected ranges; protected raise sites must use `RAISE_UNWIND`.
- A `ViaResult` exception consumed by a protocol continuation does not attach the
  caller frame to the traceback.
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
