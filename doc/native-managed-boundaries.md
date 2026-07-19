# Native/Managed Boundary Contracts

| Field | Value |
|---|---|
| Document type | Architecture contract |
| Status | Accepted |
| Implementation | Interpreter/native boundary implemented; JIT stack-transition extension proposed |
| Scope | Contracts for managed-to-native calls and native re-entry into managed execution |
| Owning layers | Managed frames, interpreter, runtime calls, native APIs, exceptions, and root publication |
| Validated against | `ad0a158` (2026-07-18) |
| Supersedes | N/A |

This document defines the contracts for crossing between CloverVM-managed
execution and native C or C++ execution. It intentionally does not specify
particular opcode families, supported fixed arities, helper overloads, or
adapter bytecode layouts. Those are implementation details and may change while
the contracts below remain stable.

The boundary has two directions:

- managed code calls a native implementation;
- native code enters or re-enters managed CloverVM code.

Both directions preserve one managed frame chain, one pending-exception model,
and one root-publication model.

## Core Execution Model

CloverVM uses two distinct stack roles:

- the **Clover stack** stores VM-managed frames, registers, call windows, and
  frame metadata;
- the **native machine stack** stores the C++ interpreter implementation,
  runtime helpers, native builtin calls, extension code, and host ABI state.

The interpreter itself runs on the native stack while accessing Clover frames
through an explicit managed frame pointer. Native implementations also run on
the native stack. During JIT bring-up, generated Python code uses the Clover
stack as its architectural managed stack but crosses to the native stack before
calling any C or C++ target. Applying this rule even to certified leaf targets
keeps re-entry into the hand-written interpreter uniform; selected native calls
may remain on a future mixed stack only after that runtime exists.

Only VM-controlled frame contents belong on the Clover stack. Arbitrary native
frames contain return addresses, spills, untagged integers, temporary pointers,
and other words that cannot safely be treated as managed `Value` slots.
Conversely, the runtime must not depend on conservative scanning of the native
machine stack to keep managed objects alive.

## Managed To Native

Python-visible native callables participate in the ordinary managed `Function`
call path. Call adaptation, defaults, argument errors, frame construction, and
return behavior remain owned by the managed call machinery. The native target
is reached through a managed adapter rather than through call-site-specific
knowledge of native implementations.

Before invoking native code, the boundary must:

- leave the managed frame chain in a walkable state;
- publish the newest live managed frame through the thread's Clover frame
  frontier;
- ensure every managed value needed across a safepoint is visible through a
  managed root, native handle, explicit root region, or owning native handle;
- pass the active thread context explicitly to VM-native implementations that
  may allocate, raise, re-enter managed code, or otherwise use runtime state.

The native implementation returns through the native result convention:

```text
success:
  return a normal Value (or Value::None() for no Python result)
  leave no pending exception

failure:
  leave pending exception state on ThreadState
  return Value::exception_marker()
```

The managed adapter converts that local native failure convention into ordinary
managed exception propagation. Python-visible callers must not observe
`Value::exception_marker()` as data.

Native code must not use C++ exceptions as Python exception transport. A
fallible native/interpreter boundary reports failure through its return type and
pending exception state.

## Native To Managed

Native code enters managed execution through a boundary adapter that links a
new managed frame to the current Clover frame frontier. It must use the normal
managed call path rather than duplicating Python argument binding, defaults,
constructor behavior, or exception handling in C++.

The native caller receives the same result convention used by native
implementations:

```text
success:
  return a normal Value
  leave no pending exception

failure:
  return Value::exception_marker()
  preserve the pending exception for the native caller
```

Before returning to native code, the adapter must pop its completed managed
frame and restore the previous Clover frame frontier. It must do this on both
normal and exceptional returns.

The native caller may propagate the marker, handle and clear the pending
exception explicitly, or translate it at another defined boundary. It must not
return a normal result while silently leaving a pending exception.

### Function And Method Entry

Calling a known `Function` and performing Python method lookup are separate
operations:

- function entry receives an already-resolved managed `Function` plus its
  arguments and delegates binding to the managed call path;
- method entry performs Python-visible lookup and receiver binding, then enters
  the resolved callable through the appropriate managed call path.

Keeping those operations separate prevents a low-level function-entry helper
from accumulating attribute, descriptor, or callable-protocol semantics.

### Code-Object Entry

A module or startup `CodeObject` is not thereby a Python-callable `Function`.
Host/runtime startup may use an internal adapter to execute such a code object,
but that adapter must preserve the same frame-frontier, rooting, and result
contracts as ordinary native-to-managed entry. The internal adapter does not
make raw code objects public callables.

## Clover Frame Frontier

Each thread maintains a Clover frame frontier: the newest live managed frame
that native code can use as the anchor of the managed frame chain.

The frontier contract is:

- while the interpreter or generated managed code is active, its current frame
  pointer identifies the newest managed frame;
- before control enters native code, that frame pointer is published as the
  frontier;
- native-to-managed entry links its boundary frame to the published frontier;
- native-to-managed return restores the frontier to the previous live frame;
- a thread with no active user frame retains a terminated sentinel frame rather
  than an ambiguous uninitialized frontier.

The frontier is a frame-chain anchor, not a substitute for a complete safepoint
scan record. Safepoint publication remains responsible for describing the live
stack extent and any live accumulator or out-of-frame values required by the
memory manager.

An initial JIT stack-transition record logically preserves:

```text
previous transition record
managed SP and FP
host SP
published managed frontier
continuation for the suspended side
```

Its physical encoding is private to the boundary implementation. The record
remains active until the native activation returns, allowing native code to
re-enter interpreted or compiled Python and create a nested transition without
losing either enclosing stack position.

Completed boundary frames are not traceback history and must not remain live as
roots. Traceback state is recorded separately from the active frame chain.

## Rooting And Ownership Across The Boundary

`Value` and `TValue<T>` are borrowed handles. A borrowed value held only in an
arbitrary C++ local is not made live merely because its bits happen to be on the
native stack.

A managed value that must survive allocation, a safepoint, or managed re-entry
must be protected by one of the mechanisms owned by that boundary:

- a live slot in the published managed frame chain;
- an explicit VM root region or managed handle slot;
- an `Owned<...>` native owner where stable retained ownership is appropriate;
- an opaque extension handle governed by the Clover C API.

The choice must match the layer and lifetime. Short-lived VM-native helpers
should not invent persistent extension-handle machinery, and external native
code must not retain borrowed raw VM values beyond the lifetime guaranteed by
its active boundary.

Heap stores performed while crossing the boundary retain, release, trace, or
apply barriers according to the active memory model. Boundary code must use the
ordinary owning store paths rather than bypassing those rules.

## Reentrant Execution

Native code may re-enter managed code, which may call native code again:

```text
managed A -> native f -> managed B -> native g -> managed C
```

At every point, the managed frames form one walkable chain. Each native-to-
managed entry links to the frontier it observed, and each return restores that
frontier before control resumes in its native caller. Re-entry must not replace,
detach, or hide the still-live managed frames below it.

During dual-stack JIT bring-up, the corresponding transition records also form
a strict stack. `managed A -> native f -> managed B -> native g -> managed C`
alternates architectural stack positions, and each return restores the SP, FP,
frontier, and continuation recorded by the immediately enclosing transition.

The same nesting rule applies to pending exceptions and roots: an inner
boundary may propagate or explicitly handle its own failure, but it must not
corrupt the outer boundary's frame linkage or leave its temporary roots
published after return.

## Layer Ownership

- The call subsystem owns Python argument adaptation and managed `Function`
  entry.
- The interpreter and boundary adapters own transitions between managed frames
  and native execution.
- `ThreadState` owns the active frontier, safepoint publication, and pending
  exception state for its thread.
- Runtime object helpers own allocation, heap stores, and object semantics used
  by native implementations.
- The Clover C API owns external extension-handle representation and lifetime;
  VM-internal boundary code should depend only on its public contract.

This separation keeps Python-visible semantics out of stack-transition helpers
and keeps low-level native call mechanics out of object and module code.

## Required Invariants

- Native C and C++ code executes on the native machine stack.
- Only VM-managed frame/register state is stored on the Clover stack.
- Every managed-to-native transition publishes a walkable live managed frame
  chain before native code can safepoint or re-enter managed execution.
- Every native-to-managed transition links to the current frontier and restores
  it on every return path.
- A normal boundary result has no pending exception.
- A failed boundary result is `Value::exception_marker()` with pending exception
  state left intact.
- `Value::exception_marker()` never becomes Python-visible data.
- C++ exceptions are not used for Python exception transport.
- Values live across safepoints are rooted or owned explicitly; the native stack
  is not a managed root source.
- Completed boundary frames and temporary roots are removed before returning to
  the caller that created them.
- Native-to-managed calls reuse managed call semantics rather than reimplementing
  argument binding or constructor policy.

## Related Documents

- [CloverVM Function Calling Convention](function-calling-convention.md) owns
  managed frame layout and call-window mechanics.
- [Exception Transport And Protocols](exception-transport-and-protocols.md) owns
  managed unwinding and protocol-specific exception behavior.
- [Refcounting and Reclamation](refcounting-and-reclamation.md) owns the current
  root-scanning and lifetime model.
- [Clover C API](clover-c-api.md) owns the external extension API and opaque
  handle contract.
- [Switchable Indirect Native Handles](indirect-native-handles.md) records the
  current handle representation and migration constraints.
