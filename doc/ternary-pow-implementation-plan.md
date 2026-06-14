# Ternary Pow Implementation Plan

This document stages the remaining work to implement Python-visible
`pow(a, b, modulo)` semantics while keeping binary power dispatch separate.

## Target Semantics

`builtins.pow` should choose the dispatch shape before special-method lookup:

```python
def pow(a, b, modulo=None):
    if modulo is None:
        return a ** b
    return __clover_ternary_pow__(a, b, modulo)
```

This means:

- `a ** b`, `pow(a, b)`, and `pow(a, b, None)` use binary power dispatch.
- `pow(a, b, modulo)` with `modulo is not None` uses ternary power dispatch.
- binary-only `__pow__(self, other)` works for the binary cases and fails for
  non-`None` modulo through ordinary call arity rules.
- ternary/defaulted `__pow__(self, other, modulo)` receives the non-`None`
  modulo value through the ternary path.

## Stage 1: Two-Key Ternary Cache Guards

The operator cache should only guard on operand0 and operand1 shape. Ternary
operand2 is runtime call state, not a cache key.

Rationale:

- `__setitem__(receiver, key, value)` should not churn caches based on value
  shape.
- ternary power is rare, and useful modulo specializations are expected to be
  integer-family cases where handlers can inspect the actual modulo value.
- trusted ternary handlers must not depend on operand2 shape. If such a
  specialization is ever needed, it should be added as a deliberate cache
  design extension rather than an accidental third guard.

Implementation:

- remove `operand_shape_keys[2]` from `OperatorInlineCache`;
- make ternary cache matching compare only operand0 and operand1;
- keep operand2 as saved continuation/call state;
- remove operand2 shape from the trusted resolver contract so resolvers cannot
  accidentally specialize on it.

## Stage 2: Ternary Continuation Read Support

`operator_frame.h` already has setup helpers for ternary continuation prefixes.
Add the matching read helper:

```text
table id, resume index, operand0, operand1, operand2
```

Continuation paths should treat operand2 as root-scanned runtime state, not a
cache guard.

## Stage 3: Ternary Dispatch Table Actions

Extend `OperatorStepAction` with ternary call layouts:

```text
CallTernary          lookup type(operand0).dunder_name
                     call args: operand0, operand1, operand2
CallTernaryReflected lookup type(operand1).dunder_name
                     call args: operand1, operand0, operand2
```

Preserve the existing odd/even reflectedness convention and add construction
helpers on `OperatorStep`.

## Stage 4: Ternary Table Walking

Extend `walk_operator_table` to handle ternary call actions:

- normal receiver: operand0;
- reflected receiver: operand1;
- user arguments: operand1 and operand2 for normal calls, operand0 and
  operand2 for reflected calls;
- requested trusted handler arity: `TrustedHandlerArity::Ternary`;
- cache validity: operand0 and operand1 lookup validity only.

The generic call path should use ordinary function arity/default handling, just
as binary operator dispatch does.

## Stage 5: Ternary Pow Table

Add `OperatorDispatchTableId::TernaryPow` with the table shape already
specified by the fast operator dispatch design:

```text
TernaryPow
    0: CallTernaryReflected("__rpow__",
                            IfArithmeticReflectedPriority,
                            else +2 to normal_first)
    1: CallTernary("__pow__", IfMethodFound)
    2: RaiseUnsupported(Always)

normal_first:
    3: CallTernary("__pow__", IfMethodFound)
    4: CallTernaryReflected("__rpow__",
                            IfMethodFoundAndOperands01TypesDiffer)
    5: RaiseUnsupported(Always)
```

There is no in-place ternary table. `**=` remains binary augmented assignment.

## Stage 6: Ternary Pow Bytecode

Add `Bytecode::TernaryPow` and `op_ternary_pow`.

The opcode should be used only by trusted intrinsic lowering. It should use the
operator table with three semantic operands `(a, b, modulo)`, cache only
operand0/operand1 shapes, and call trusted ternary handlers when the resolver
returns one for `TrustedHandlerArity::Ternary`.

## Stage 7: Trusted Intrinsic Lowering

Add trusted helper recognition for:

```python
__clover_ternary_pow__(a, b, modulo)
```

Requirements:

- only available in trusted Clover extension mode;
- exactly three positional arguments;
- no keyword arguments;
- lower directly to `TernaryPow`.

Untrusted code using the same name remains an ordinary function call or name
lookup.

## Stage 8: Builtin `pow`

Implement `pow` in `src/bootstrap/builtins.py`:

```python
def pow(a, b, modulo=None):
    if modulo is None:
        return a ** b
    return __clover_ternary_pow__(a, b, modulo)
```

Use parameter names `a`, `b`, and `modulo`.

## Stage 9: Move Subscription Operators To Tables

After ternary pow has proved the ternary table path, move `__getitem__`,
`__setitem__`, and `__delitem__` onto operator dispatch tables instead of their
bespoke opcode slow paths.

The intended split is:

- `GetItem`: receiver-owned binary table for `__getitem__(receiver, key)`;
- `SetItem`: receiver-owned ternary table for
  `__setitem__(receiver, key, value)`;
- `DelItem`: receiver-owned binary table for `__delitem__(receiver, key)`.

These tables should preserve receiver-only lookup semantics, not reflected
operator semantics. Cache guards remain operand0/operand1 only; for `SetItem`,
the value operand is saved call/continuation state and is not a cache key.

## Stage 10: Tests

Add tests for:

- setitem cache entries not depending on value shape;
- `pow(a, b)` and `pow(a, b, None)` calling binary `__pow__`;
- non-`None` modulo calling ternary `__pow__`;
- binary-only `__pow__` failing for non-`None` modulo;
- reflected ternary `__rpow__` behavior;
- trusted intrinsic lowering to `TernaryPow`;
- untrusted `__clover_ternary_pow__` remaining ordinary user code;
- ternary trusted resolver calls requesting `TrustedHandlerArity::Ternary`.

## Stage 11: Documentation And Verification

Update `doc/fast-operator-dispatch.md` after implementation to reflect:

- `BinaryPow` as the binary operator table;
- `TernaryPow` as the non-`None` modulo table;
- requested trusted handler arity in resolver signatures;
- two-shape cache guards for ternary operations.

Run `clang-format` on touched C++ files and verify with:

```text
ninja -C build-debug all check
```
