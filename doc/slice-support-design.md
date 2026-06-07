# Slice Support Design

This note describes the first slice-object design for clovervm. The main
semantic goal is to support Python slice objects and slice syntax. The main VM
design goal is to make slice keys useful to the existing trusted subscript
handler resolver by giving bounded slice categories distinct Shapes.

## Python Semantics

`slice` is a Python-visible builtin type. A slice object has three read-only
attributes:

```text
start
stop
step
```

Construction fills unspecified fields with `None`:

```python
slice(stop)          # slice(None, stop, None)
slice(start, stop)   # slice(start, stop, None)
slice(start, stop, step)
```

`slice()` with no arguments and `slice(...)` with more than three arguments
raise `TypeError`. Keyword arguments are not accepted.

Slice construction does not normalize or validate the field values. These are
valid slice objects:

```python
slice("a", "b", "c")
slice(1, 2, 0)
slice(1, 2, object())
```

Coercion, bounds adjustment, and the zero-step error happen when a sequence
consumes the slice, for example through `slice.indices(length)` or a builtin
sequence `__getitem__`.

Slice syntax passes a slice object as the subscription key:

```python
a[:c]      # a.__getitem__(slice(None, c, None))
a[b:]      # a.__getitem__(slice(b, None, None))
a[b:c]     # a.__getitem__(slice(b, c, None))
a[b:c:s]   # a.__getitem__(slice(b, c, s))
a[::s]     # a.__getitem__(slice(None, None, s))
```

For assignment, Python evaluates the right-hand side before the target and
slice bounds:

```python
a[b:c] = value
```

evaluates `value`, then `a`, then `b`, then `c`, then calls
`a.__setitem__(slice(b, c, None), value)`.

## Shape Design

clovervm's subscript inline cache stores a `ShapeKey` for the key. For heap
objects, `ShapeKey::from_value(value)` uses the object's `Shape *`. This means
slice objects can select different trusted native handlers without changing the
cache representation, as long as useful slice categories have distinct Shapes.

The initial slice categories are:

```text
slice_step_none_shape
slice_step_value_shape
```

The invariants are:

```text
slice_step_none_shape   => slice.step is exactly None
slice_step_value_shape  => slice.step is any Python value other than None
```

These are not different Python types and not different native layouts. They are
two Shapes for objects of the same `slice` class.

The term `value` is intentional. It avoids the word `present`, which collides
with the VM's internal `Value::not_present()` sentinel. A
`slice_step_value_shape` object may still contain a step value that is invalid
for sequence slicing, such as `0` or a non-indexable object. The shape only
proves that `.step` is not `None`.

## Slice Object Layout

All slice objects use the same native C++ layout:

```cpp
class Slice : public Object {
    Member<Value> start;
    Member<Value> stop;
    Member<Value> step;
};
```

The three fields are always present. Attribute reads for `.start`, `.stop`, and
`.step` should use the ordinary Shape-backed builtin attribute mechanism.
Writes and deletes should fail as read-only attribute operations.

The object is immutable after construction: the field references do not change,
and the Shape chosen by the factory remains stable for the object's lifetime.
The referenced field values can themselves be mutable Python objects.

## VM-Owned Shapes

The VM should own canonical slice Shapes:

```cpp
Shape *slice_step_none_shape_;
Shape *slice_step_value_shape_;
```

with corresponding accessors:

```cpp
Shape *slice_step_none_shape() const;
Shape *slice_step_value_shape() const;
```

Both Shapes must report the same class:

```text
shape->get_class() == vm->slice_class()
```

Both Shapes must describe the same native fields and same Python-visible
attributes. The only reason they are separate Shapes is dispatch
specialization.

Slice allocation should go through a single factory that chooses the Shape from
the stored `step` value:

```cpp
Shape *shape = step.is_none()
    ? vm->slice_step_none_shape()
    : vm->slice_step_value_shape();
```

Both slice syntax lowering and the builtin `slice(...)` constructor should use
this factory. In particular, `slice(1, 2)` and `slice(1, 2, None)` must produce
objects with the same `slice_step_none_shape`.

## Subscript Dispatch

The trusted method resolver already receives the receiver and key shape keys:

```cpp
TrustedHandlerResolution (*resolver)(
    VirtualMachine *, ShapeKey receiver_key, ShapeKey key_key, ShapeKey unused);
```

Builtin sequence resolvers can use the key shape to split handlers:

```text
SMI key                 -> integer element access
slice_step_none_shape   -> contiguous slice path
slice_step_value_shape  -> extended slice path
```

For list, tuple, and str, the `slice_step_none_shape` handler can skip the
`step is None` branch and normalize only start/stop for a contiguous copy. It
may still need to handle missing bounds and out-of-range clipping.

The `slice_step_value_shape` handler may skip only the `step is None` branch.
It must still coerce `step` through Python's integer-index protocol, reject a
zero step, coerce start/stop as needed, normalize bounds, and produce the
extended slice result.

The shape split must not bypass Python-visible special-method dispatch. A
trusted native handler should run only after the IC has proven that normal
dunder lookup selected the trusted builtin `__getitem__`, `__setitem__`, or
`__delitem__` implementation, matching the existing fast operator dispatch
model.

## Parser And Codegen

The parser needs a subscript-key representation that can express:

```text
ordinary expression key
slice lower:upper
slice lower:upper:step
tuple of subscript keys for multidimensional syntax
```

Missing slice fields should be represented as `None` at runtime. The parser or
codegen may encode omission structurally, but the constructed `Slice` object
must store explicit `None` values in omitted slots.

The initial lowering can be simple:

```text
evaluate receiver
evaluate lower or load None
evaluate upper or load None
evaluate step or load None
construct Slice
emit existing get/set/del item opcode
```

clovervm does not need a CPython-style `BINARY_SLICE` opcode for the first
implementation. That opcode is an optimization for common two-bound slices, not
a semantic requirement.

Assignment lowering must preserve Python's evaluation order:

```text
RHS first, then target receiver, then slice fields, then __setitem__
```

Delete lowering evaluates the target receiver and slice fields before
`__delitem__`.

## Builtin Sequence Consumption

List, tuple, and str `__getitem__` should recognize slice keys in addition to
integer keys.

Construction-time permissiveness is important. A slice with invalid field
values must be constructible. The error belongs in the consumer:

```python
s = slice("a", "b", "c")  # ok
[1, 2, 3][s]              # TypeError
```

The normalizer used by builtin sequences should follow Python's visible rules:

- `step == None` defaults to `1`.
- `step == 0` raises `ValueError`.
- non-`None` start, stop, and step use the integer-index protocol.
- negative bounds are adjusted by sequence length.
- bounds are clipped to the valid range.
- negative steps use the reverse-slice default and clipping rules.

Full `__index__` support may depend on a broader integer-index protocol in the
VM. If that protocol is not implemented yet, slice consumption should fail
honestly for unsupported non-SMI field values rather than pretending construction
validated them.

## Non-Goals

- Do not create separate Python classes for no-step and with-step slices.
- Do not use different native layouts for the two slice categories.
- Do not encode a shape per exact start/stop/step value.
- Do not treat `slice_step_value_shape` as proof that the step is an integer,
  nonzero, or otherwise valid for slicing.
- Do not add a separate `[:]` Shape initially. The `slice_step_none_shape`
  handler can detect the full-copy case cheaply after the shape guard.
- Do not add a CPython-style `BINARY_SLICE` opcode until there is measured
  pressure for that optimization.

## Test Plan

Interpreter-level Python tests should cover:

- `slice(1)`, `slice(1, 2)`, `slice(1, 2, None)`, and
  `slice(1, 2, 3)` field values.
- `slice()` and four-argument `slice(...)` arity errors.
- rejection of keyword arguments to `slice`.
- `.start`, `.stop`, and `.step` read behavior and read-only write/delete
  errors.
- user-defined `__getitem__`, `__setitem__`, and `__delitem__` receiving actual
  slice objects.
- omitted-bound syntax: `a[:c]`, `a[b:]`, `a[:]`, `a[::s]`.
- assignment evaluation order for `a[b:c] = value`.
- list, tuple, and str contiguous slicing.
- extended slicing, including negative step and zero-step error.
- invalid slice field values failing when consumed, not when constructed.

Codegen tests should stay focused on structural guarantees, such as omitted
fields lowering to `None` and slice assignment preserving RHS-first evaluation
order.
