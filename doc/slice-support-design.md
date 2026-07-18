# Slice Support

Status: accepted and implemented, except for builtin list slice assignment and
deletion.

This document records CloverVM's slice contracts. It describes the
Python-visible behavior and the internal invariants that other runtime work may
rely on. Parser staging, opcode inventories, implementation checklists, and
historical alternatives have been removed now that the core design is landed.

## Python-Visible Contract

`slice` is an immutable builtin type with three read-only attributes:

```text
start
stop
step
```

Construction stores its arguments without coercion or validation:

```python
slice(stop)          # slice(None, stop, None)
slice(start, stop)   # slice(start, stop, None)
slice(start, stop, step)
```

`slice()` with no arguments and calls with more than three arguments raise
`TypeError`. Keyword arguments are not accepted. Values such as
`slice("a", "b", 0)` are valid slice objects; conversion and the zero-step
error occur only when a consumer normalizes the slice.

Slice syntax constructs the same Python-visible object and passes it through
the normal subscript protocol:

```python
obj[a:b:c]          # obj.__getitem__(slice(a, b, c))
obj[a:b:c] = value  # obj.__setitem__(slice(a, b, c), value)
del obj[a:b:c]      # obj.__delitem__(slice(a, b, c))
```

Python 3 has no separate `__getslice__`, `__setslice__`, or `__delslice__`
protocol. Builtin slice mutation is therefore an extension of the existing
`__setitem__` and `__delitem__` implementations, not a new syntax or opcode
family.

## Evaluation Order And Lowering

Slice syntax must construct a slice directly. It must not perform a Python
global lookup of the name `slice`, because rebinding that name does not affect
subscript syntax.

Omitted fields become `None`, and explicit fields are evaluated from left to
right. Assignment preserves Python's target-assignment order:

```text
evaluate RHS
evaluate target receiver
evaluate lower bound
evaluate upper bound
evaluate step
construct slice
call __setitem__
```

Deletion evaluates the receiver and slice fields from left to right, constructs
the slice, and calls `__delitem__`. These rules are observable when any
expression has side effects or raises.

The parser represents a slice expression with distinct lower, upper, and step
positions. Missing positions remain structurally absent in the AST; codegen is
responsible for supplying `None`. This preserves source structure without
inventing source locations for omitted expressions.

## Runtime Representation

Every slice uses the same `Slice` native layout. Its `start`, `stop`, and `step`
fields are fixed inline `Member<Value>` slots exposed through stable read-only
attribute descriptors. The fields and the object's shape do not change after
construction, although the referenced values may themselves be mutable.

All allocation flows through one factory. The factory stores the three values
unchanged and selects one of two VM-owned shapes:

```text
slice_step_none_shape  => step is exactly None
slice_general_shape    => no invariant about the field values
```

The shapes have the same Python class, attributes, slot indexes, native layout,
and mutability rules. They differ only in identity so trusted subscript handlers
can specialize the common nonstrided case without inspecting the `step` field.

The general shape does not prove that `step` is non-`None`, integer, nonzero, or
valid. Every general-shape consumer must perform full normalization.

## Normalization

Slice fields remain unvalidated until a sequence or `slice.indices()` consumes
them. Normalization implements these visible rules:

- `None` step means `1`;
- zero step raises `ValueError`;
- negative bounds are adjusted relative to sequence length;
- bounds are clipped according to the direction of the step;
- negative steps use reverse-slice defaults;
- the normalized result includes the number of selected elements.

CloverVM keeps separate internal normalized records for the two shape cases:

- the nonstrided record contains the contiguous start and selected length;
- the general record contains start, stop, step, and selected length.

Builtin consumers use these records directly rather than calling the
Python-visible `slice.indices()` method and allocating a tuple.

Current normalization accepts `None` and integer-like values that fit the SMI
range, including `bool` and fitting heap `BigInt` values. An oversized integer
raises `OverflowError`. Arbitrary objects implementing `__index__` are not yet
supported; unsupported field values raise `TypeError` when consumed, not when
the slice is constructed.

`slice.indices(length)` uses the same general normalizer, returns
`(start, stop, step)`, and rejects a negative length.

## Subscript Dispatch

Slice operations use the ordinary guarded special-method dispatch path. The
presence of a slice key does not bypass lookup of `__getitem__`, `__setitem__`,
or `__delitem__`, and user-defined containers receive the actual slice object.

For an exact trusted builtin implementation, the subscript cache may use the
key's shape to select among:

```text
integer key             -> element handler
slice_step_none_shape   -> contiguous slice handler
slice_general_shape     -> strided/general slice handler
```

That specialization is valid only after the normal method lookup and cache
guards prove that the trusted builtin method is the selected operation.

## Implemented Surface

The implemented slice surface includes:

- `slice` construction, attributes, representation, and `indices()`;
- two- and three-field slice syntax with omitted fields;
- slice keys delivered to user-defined `__getitem__`, `__setitem__`, and
  `__delitem__` methods;
- Python-compatible assignment evaluation order;
- list, tuple, and string slice reads;
- trusted nonstrided and general read handlers for builtin sequences;
- positive and negative steps, clipping, reverse slices, and consumption-time
  validation for the supported integer range.

Tuple and string slice mutation is correctly unsupported because those types
are immutable.

## Remaining Work: Builtin List Slice Mutation

Builtin `list.__setitem__` and `list.__delitem__` currently accept integer keys
only. Consequently, syntax and generic dispatch work for user-defined
containers, but these operations remain unsupported for exact lists:

```python
items[1:3] = replacement
items[::2] = replacement
del items[1:3]
del items[::2]
```

This work belongs in the builtin list implementation and its trusted handler
resolver. It does not require new parser, AST, bytecode, or generic operator
machinery.

### Assignment Contract

For a contiguous slice (`step == 1`), the right-hand iterable may contain any
number of elements. Assignment replaces the selected range and may grow or
shrink the list.

For an extended slice (`step != 1`), the replacement must contain exactly the
same number of elements as the normalized slice selects. A length mismatch
raises `ValueError` without mutating the list.

The replacement accepts a general iterable, not only a list or tuple. It must be
fully materialized before structural mutation begins. This is required for:

- iterator failures without partial list mutation;
- extended-slice length validation before mutation;
- aliasing cases such as `items[:] = items`;
- iterators that read from the destination list.

Storage mutation must preserve `ValueArray` ownership, release overwritten or
removed elements exactly once, retain inserted elements, and leave the list
valid if replacement materialization fails.

### Deletion Contract

Contiguous deletion removes the normalized range. Extended deletion removes all
selected indexes as one logical operation, including negative-step slices,
without index shifting changing which original elements are selected.

Deletion requires no RHS materialization, but it must preserve ownership and
compact the survivors correctly for every normalized direction and stride.

### Dispatch And Tests

The ordinary list methods must recognize slice keys on their uncached path, and
the trusted resolver may then install shape-specific nonstrided and general
mutation handlers under the existing operator guards.

Interpreter-level tests should cover:

- contiguous replacement that grows, shrinks, and empties a list;
- extended replacement for positive and negative steps;
- extended replacement length mismatch with no mutation;
- arbitrary iterable replacement and failure during materialization;
- self-assignment and iterators over the destination list;
- contiguous and extended deletion, including negative steps;
- empty selections and clipped bounds;
- zero step and unsupported/oversized field errors;
- cache hits for the trusted builtin handlers;
- preservation of RHS-first assignment evaluation order.

## Required Invariants

- Slice construction stores fields without coercion or validation.
- Syntax is independent of rebinding the builtin name `slice`.
- Both slice shapes expose identical Python-visible layout and behavior.
- Only `slice_step_none_shape` proves a field-value invariant.
- General consumers always normalize and validate all relevant fields.
- User-defined subscript methods receive a normal slice object.
- Trusted handlers run only under the ordinary special-method lookup guards.
- Slice assignment evaluates the RHS before the target and slice fields.
- Consumer failures propagate through pending exception state without exposing
  VM sentinels as Python values.

## Related Documents

- [Fast Operator Dispatch](fast-operator-dispatch.md) owns guarded subscript
  method lookup and trusted handler selection.
- [Known Python Semantic Deviations](python-deviations.md) tracks remaining
  integer-index protocol limitations.
- [Development Priorities](development-priorities.md) records when builtin list
  slice mutation should be scheduled.
