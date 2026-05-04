# JIT Calling Convention Notes

This is a loose design note, not a committed ABI specification. It records the
current direction for a future JIT calling convention so the constraints stay
visible while the implementation is still taking shape.

## Split the Calling Conventions

CloverVM should treat these as distinct conventions:

- bytecode/interpreter calls, which use the current stack-backed outgoing
  argument window;
- exact JIT-to-JIT calls, where a warmed call site has selected a concrete call
  plan and can pass normalized positional parameters through a native-like
  convention;
- runtime/native transitions, where compiled Python code publishes roots,
  switches away from the Clover stack as needed, calls C++/native code, and
  returns to managed execution.

The bytecode convention remains the canonical interpreter materialization
format. Exact JIT calls should not be forced to use the bytecode argument
transport when the target and call adaptation are already known.

## Accumulator Discipline

Current `Call*` bytecodes do not read the incoming accumulator. They read their
callable, receiver, and arguments from explicit frame slots, then leave the call
result in the accumulator when control returns to the caller.

Future call bytecodes should preserve this rule:

```text
Call instructions must not consume the incoming accumulator as an implicit
argument or hidden input.
```

This keeps the call boundary as a clean accumulator lifetime break:

```text
before call:  old accumulator is dead
during call:  argument registers/slots belong to the call
after call:   return value is the new accumulator
```

On AArch64, that makes `x0` a good preferred accumulator register for
straight-line compiled code. At an exact JIT call, `x0` can naturally become
`p0`, `x1` can become `p1`, and so on. After the call, the normal ABI return in
`x0` is again the accumulator.

This should be a lowering preference, not a hard global pin. The register
allocator may move the accumulator when pressure, helper calls, or deopt state
make another location cheaper.

## Exact AArch64 Call Shape

A plausible exact JIT call convention is:

```text
x0      p0 / return Value / preferred accumulator
x1      p1
x2      p2
x3      p3
x4      p4
x5      p5
x6      p6
x7      p7
stack   p8, p9, ...
```

The call site must already have normalized the public Python call into logical
parameters:

```text
p0, p1, p2, ...
```

That normalization may include method receiver binding, defaults, constructor
thunks, or other call-plan-specific adaptation. Generic public calls still need
an adapter path.

## Clover Stack and Native Stack

If compiled Python uses the Clover stack as the active native stack pointer, the
scanner can rely on ordinary frame extent information in the same spirit as a
native ABI:

```text
fp anchors the frame
sp marks the lowest active frame-local extent
```

That only works if arbitrary native/C++ frames are not interleaved into the
Clover stack. Runtime/native calls therefore need transition stubs:

```text
publish live Value roots
save managed continuation state
switch to the C++ native stack
call runtime/native target
switch back to the Clover stack
return Value in x0, or exception_marker in x0 with pending exception state
```

The existing bytecode native thunk model can remain useful as interpreter
scaffolding, but it should not be assumed to be the long-term optimized JIT ABI.

## Exit and Safepoint State

At JIT exits, deopts, safepoints, exception edges, and interpreter handoffs, the
VM must be able to materialize:

- the Clover frame pointer;
- bytecode resume pc;
- current `CodeObject`;
- accumulator `Value`;
- interpreter-visible frame slots;
- pending exception state when the returned value is `exception_marker`.

Any live `Value` not already in a materialized Clover frame slot must be
described by safepoint/deopt metadata. Unboxed temporaries may exist inside JIT
code, but every interpreter-visible value must be materializable as a normal
`Value` at an exit.

## Open Questions

- exact overflow argument layout after `x0` through `x7`;
- whether `x29` is always the Clover frame pointer in compiled Python;
- how much native unwindability compiled frames should preserve;
- eager frame materialization versus lazy frame maps;
- exact runtime transition record format;
- cross-architecture register mapping, especially x86-64.
