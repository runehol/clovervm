# Python Opcode Design Notes

| Field | Value |
|---|---|
| Document type | Investigation |
| Status | Speculative |
| Implementation | Not started |
| Scope | Candidate opcode and inline-cache structures for Python protocol operations |
| Owning layers | Codegen, bytecode, interpreter, inline caches, and runtime protocols |
| Validated against | N/A |
| Supersedes | N/A |

This note captures design pressure exposed by the subscript set/delete
experiment. It is not an implementation plan yet. The goal is to make the
opcode and inline-cache tradeoffs explicit before we commit to a broader VM
shape.

## Context

The current subscript getitem path moved toward a compact V8-style inline cache:

- the bytecode arranges call-frame-shaped operands
- the opcode owns a compact IC
- the IC caches method lookup, key shape, and an optional trusted handler
- the slow path can fall through into a Python function call

That design is attractive because the cache is small and one representation can
serve native handlers and Python calls.

The problem is that V8's model is shaped by JavaScript, where there is a
stronger semantic boundary between primitive operations and function calls.
Python has a much weaker boundary. Many primitive-looking operations are
protocol dispatch points and may become Python calls.

Examples:

```python
obj[key]          # __getitem__
obj[key] = value  # __setitem__
del obj[key]      # __delitem__
a + b             # __add__, __radd__
a < b             # rich comparison methods
len(x)            # __len__
iter(x), next(x)  # __iter__, __next__
obj.attr          # descriptors
```

The same syntax point may be either:

- one cheap exact-builtin operation
- a trusted native handler
- a descriptor/protocol resolution
- a full Python call
- a sequence of calls, for example `__add__` followed by `__radd__` after
  `NotImplemented`

This means "operation" and "function call" cannot be treated as cleanly
separate categories, but it also does not mean every operation should be forced
into call-frame layout.

## What The StoreSubscript Experiment Showed

The setitem/delitem experiment changed `StoreSubscript` from a direct
receiver/key/accumulator operation into a call-frame-shaped opcode, matching
`LoadSubscript`:

```text
Mov r0, receiver
Mov r1, key
Star2              # value
StoreSubscript r0, operator_ic[...]
```

This made it easy to reuse the existing getitem special-method call machinery.
It also made exact list stores slower.

Measured `BM_StoreItemList/100000` after trusted list handlers:

```text
old direct store path: about 0.907 ms CPU
new call-shaped path: about 1.136 ms CPU
```

Sampling showed the actual trusted list mutation handler was not the main cost.
The hot frames were mostly interpreter overhead around the new shape:

```text
op_ldar1
op_store_subscript
op_lda_smi
op_add_smi
op_mov
trusted_list_setitem_smi_handler
op_star2
```

The important result is not the exact percentage. The important result is that
making the opcode call-shaped up front made a primitive exact-builtin operation
pay for operand shuffling and call-oriented dispatch even when no Python call
was needed.

## Design Tension

The core tension is:

- **Compact call-shaped ICs** make protocol-call fallback easy and keep one
  representation for native handlers and Python functions.
- **Natural operation-shaped opcodes** keep primitive fast paths cheap and avoid
  unnecessary register movement.

Python's semantics require both. Builtin exact operations must be fast, but the
same bytecode must also be able to become a Python call.

The mistake in the experiment was coupling the opcode operand layout to the
slowest and most general execution mode. The bytecode was shaped like a call
even when the hot path was a direct list mutation.

## Possible Direction: Operation ICs With Multiple Plan Kinds

A Python-native design may need opcodes whose operands match the operation's
natural dataflow, while the IC chooses an execution plan.

For example:

```text
LoadSubscript dst, receiver, key, ic
StoreSubscript receiver, key, value, ic
DelSubscript receiver, key, ic
BinaryAdd dst, lhs, rhs, ic
```

The IC could cache a plan kind:

```text
DirectBuiltin
TrustedNativeHandler
PythonFunctionCall
DescriptorDispatch
GenericSlowPath
```

The direct builtin and trusted native plans use the opcode's natural operands.
Only a Python-call plan materializes call-frame arguments.

This keeps exact list/dict store fast:

```text
StoreSubscript receiver, key, value, ic
  cache hit: exact list + smi key -> direct/trusted store
  cache hit: user __setitem__ -> set up Python call
  miss: resolve special method / builtin plan
```

The IC remains the semantic dispatch point. The opcode operands are not forced
to mimic function-call layout.

## All-Register Opcode Design

The accumulator may be making these protocol boundaries harder.

With an accumulator design, many operations have split inputs:

```text
StoreSubscript receiver_reg, key_reg, accumulator_value
BinaryAdd lhs_reg, accumulator_rhs
```

If the operation becomes a call, arguments often need to be copied into a
different layout. That is exactly what happened for storeitem.

An all-register design could make dataflow explicit:

```text
LoadSubscript dst, receiver, key, ic
StoreSubscript receiver, key, value, ic
BinaryAdd dst, lhs, rhs, ic
Call dst, callee, first_arg, argc, ic
```

Potential benefits:

- fewer accumulator load/store shuffles at protocol boundaries
- easier direct use of operation operands by native handlers
- clearer def/use information for optimization and future tiering
- easier continuation opcodes because results have explicit destinations

Costs:

- larger bytecode
- more operand decode
- more register allocation pressure in codegen
- less compact simple arithmetic/load/store code

The question is whether Python's frequent protocol-call boundary makes explicit
register operands a better global tradeoff than accumulator compactness.

## Continuation Opcodes

Continuation opcodes are worth considering seriously.

Many Python operations are not just "do primitive or call once". They have
operation-specific post-call rules:

- `__setitem__` and `__delitem__` ignore the return value.
- `__len__` must validate the result.
- binary operators may need to handle `NotImplemented`.
- reflected binary operators may need a second method call.
- comparisons have their own result and fallback rules.

Instead of making every protocol operation look like a call up front, the
operation opcode can perform fast plans inline and enter a call only when the IC
selects a call plan. The return address can point at an explicit continuation:

```text
BinaryAdd dst, lhs, rhs, ic
BinaryAddAfterLeftCall dst, lhs, rhs, ic
BinaryAddAfterRightCall dst, lhs, rhs, ic

StoreSubscript receiver, key, value, ic
StoreSubscriptAfterCall receiver, key, value, ic
```

The continuation owns post-call semantics:

- ignore setitem/delitem return values
- check for `NotImplemented`
- decide whether to call reflected methods
- raise the final `TypeError`
- validate protocol result types

This keeps C++ control flow explicit and compatible with the interpreter's
tail-call dispatch model. The VM should not hide these continuations on the C++
stack.

## Open Questions

- Should clovervm move from accumulator bytecode to all-register bytecode, or
  only introduce register-shaped opcodes for protocol-heavy operations?
- Should each protocol operation have a specialized IC layout, or should there
  be a shared operation-IC header with operation-specific payloads?
- How much bytecode size are we willing to trade for fewer runtime moves?
- Should exact builtin plans be represented as direct plan enums rather than
  trusted function pointers?
- How should continuation opcodes be encoded so they are explicit but not noisy
  in disassembly and codegen?
- Can binary-op ICs express `__add__` / `__radd__` and `NotImplemented`
  handling without becoming too large?
- Which operations should be first-class protocol opcodes rather than lowered
  through generic special-method call machinery?
- What release-build hot-path frame constraints should apply to new operation
  and continuation handlers?

## Working Principle

For Python, bytecode should probably model Python operations, not primitive
machine operations and not function calls exclusively.

The IC should decide how an operation executes. The opcode's operand layout
should match the operation's natural inputs and outputs. Function-call layout
should be paid only when the selected plan is actually a Python call.
