# Slice Support Design

This note describes the first slice-object design for clovervm. The main
semantic goal is to support Python slice objects and slice syntax. The main VM
design goal is to make slice keys useful to the existing trusted subscript
handler resolver by giving bounded slice categories distinct Shapes.

The implementation should be built in three phases:

1. Add the `slice` builtin object and syntax support so `a[:c]` creates a
   slice key and user-defined `__getitem__` can observe it.
2. Add Python-facing `slice.indices(length)` using the internal Slice
   normalizer.
3. Teach builtin sequences to consume slice keys, then add trusted handlers for
   the two slice key Shapes.

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

All slice objects use the same native C++ layout. Because `.start`, `.stop`,
and `.step` are Shape-visible inline slots, `Slice` should derive from
`SlotObject`, not directly from `Object`:

```cpp
class Slice : public SlotObject {
public:
    static constexpr NativeLayoutId native_layout = NativeLayoutId::Slice;
    static constexpr uint32_t kStartSlot = 0;
    static constexpr uint32_t kStopSlot = 1;
    static constexpr uint32_t kStepSlot = 2;
    static constexpr uint32_t kInlineSlotCount = 3;

    Slice(ClassObject *cls, Value start, Value stop, Value step)
        : SlotObject(cls, native_layout), start(start), stop(stop), step(step)
    {
    }

    Member<Value> start;
    Member<Value> stop;
    Member<Value> step;

    CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(Slice, SlotObject, 3);
    CL_DECLARE_STATIC_OBJECT_SIZE(Slice);
};

static_assert(CL_OFFSETOF(Slice, start) ==
              sizeof(SlotObject) + Slice::kStartSlot * sizeof(Value));
static_assert(CL_OFFSETOF(Slice, stop) ==
              sizeof(SlotObject) + Slice::kStopSlot * sizeof(Value));
static_assert(CL_OFFSETOF(Slice, step) ==
              sizeof(SlotObject) + Slice::kStepSlot * sizeof(Value));
```

The three fields are always present in the native layout and are the slice
object's fixed inline slot storage. The slice Shapes should expose them with
stable read-only inline-slot descriptors:

```text
start -> inline slot 0
stop  -> inline slot 1
step  -> inline slot 2
```

The descriptors should use `DescriptorFlag::StableSlot` and
`DescriptorFlag::ReadOnly`. `NativeLayoutId::Slice` must also be included in
`native_layout_has_slots()` so generic inline-slot access treats `Slice` as a
slot-backed object.

This keeps `.start`, `.stop`, and `.step` on the ordinary Shape-backed
attribute path while still storing the values directly in the fixed native
`Slice` layout.

Writes and deletes for `.start`, `.stop`, and `.step` must fail as read-only
attribute operations.

The object is immutable after construction: the field references do not change,
and the Shape chosen by the factory remains stable for the object's lifetime.
The referenced field values can themselves be mutable Python objects.

## VM-Owned Shapes

The VM should own canonical slice Shapes:

```cpp
ClassObject *slice_class_;
Shape *slice_step_none_shape_;
Shape *slice_step_value_shape_;
```

with corresponding accessors:

```cpp
ClassObject *slice_class() const;
Shape *slice_step_none_shape() const;
Shape *slice_step_value_shape() const;
```

Both Shapes must report the same class:

```text
shape->get_class() == vm->slice_class()
```

Both Shapes must describe the same Python-visible attributes. The only reason
they are separate Shapes is dispatch specialization.

Use the builtin class's installed instance root Shape as
`slice_step_none_shape_`. Create `slice_step_value_shape_` as a second root
Shape with the same descriptor table, same class, same slot counts, same native
layout assumptions, and same fixed-attribute flags. Both Shapes must map
`start`, `stop`, and `step` to the same stable read-only inline slot indexes.

The slice class installer should build the `ShapeRootDescriptor` array once and
use it for both root Shapes. Do not rely on recovering the descriptor table from
`slice_class_->get_instance_root_shape()` later. The builder/helper can be
structured however the implementation prefers, but the descriptor source should
be shared at construction time.

```cpp
slice_step_none_shape_ = slice_class_->get_instance_root_shape();
slice_step_value_shape_ = Shape::make_root_with_descriptors(
    TValue<ClassObject>::from_oop(slice_class_), descriptors, descriptor_count,
    next_slot_index, present_count, inline_slot_capacity,
    fixed_attribute_shape_flags());
```

The exact helper can differ, but the invariant is not optional: the two Shapes
must differ only by identity. They must not expose different attributes,
different class identity, or different storage requirements.

Slice instances should be born with one of the two specialized Shapes. It is
acceptable for the C++ constructor to initialize through the class root first,
but the factory must set the final Shape before returning the object.

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

The factory contract should be explicit:

```cpp
[[nodiscard]] TValue<Slice> make_slice(Value start, Value stop, Value step);
```

The factory stores the three values unchanged, chooses the Shape solely from
`step.is_none()`, and returns an owned/retained handle appropriate for the
existing allocation helpers.

The builtin constructor uses the factory with these argument rewrites:

```text
slice(stop)              -> make_slice(None, stop, None)
slice(start, stop)       -> make_slice(start, stop, None)
slice(start, stop, step) -> make_slice(start, stop, step)
```

It does not coerce or validate any of the three field values.

## Subscript Dispatch

The trusted method resolver already receives the receiver and key shape keys:

```cpp
TrustedHandlerResolution (*resolver)(
    VirtualMachine *, ShapeKey receiver_key, ShapeKey key_key, ShapeKey unused);
```

Builtin sequence resolvers should use the key shape to split handlers:

```text
SMI key                 -> integer element access
slice_step_none_shape   -> no-step slice handler
slice_step_value_shape  -> explicit-step slice handler
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

Resolver checks should compare `key_key` directly against the two VM-owned
slice shape keys. The resolver may also ask `vm->shape_for_key(key_key)` for
the class when it needs a conservative class check, but the optimized branch
should be shape-specific:

```cpp
if(key_key == ShapeKey::from_shape(vm->slice_step_none_shape())) {
    ...
}
```

If `ShapeKey` does not yet have a helper for VM-owned Shapes, add one rather
than reconstructing a fake `Value`.

That helper should be a small explicit API on `ShapeKey`:

```cpp
static ALWAYSINLINE ShapeKey from_shape(Shape *shape);
```

It should encode the same bits as a heap object's shape key. This keeps
resolver code from depending on `ShapeKey`'s private representation.

## Parser And Codegen

The AST should distinguish ordinary subscription keys from slice keys. The
parser needs a subscript-key representation that can express:

```text
ordinary expression key
slice lower:upper
slice lower:upper:step
tuple of subscript keys for multidimensional syntax
```

The recommended AST shape is a dedicated expression node:

```text
EXPRESSION_SLICE(lower?, upper?, step?)
```

Represent the three positions with exactly three children. Use `-1` for an
omitted position:

```text
a[:c]    -> EXPRESSION_SLICE(-1, c, -1)
a[b:]    -> EXPRESSION_SLICE(b, -1, -1)
a[b:c]   -> EXPRESSION_SLICE(b, c, -1)
a[::s]   -> EXPRESSION_SLICE(-1, -1, s)
a[:]     -> EXPRESSION_SLICE(-1, -1, -1)
```

Codegen is responsible for loading `None` for omitted fields. This keeps the
parser from inventing source locations for missing syntax while still
guaranteeing the runtime `Slice` stores explicit `None` values.

Subscript remains:

```text
EXPRESSION_BINARY/SUBSCRIPT(receiver, key_expression)
```

where `key_expression` may now be an `EXPRESSION_SLICE`.

The initial lowering can be simple:

```text
evaluate receiver
evaluate lower or load None
evaluate upper or load None
evaluate step or load None
construct Slice
emit existing get/set/del item opcode
```

Slice construction must have explicit bytecodes or native helper calls. Lowering
slice syntax as a normal Python call to the builtin `slice` would preserve many
semantics, but it would also make syntax sensitive to a rebinding of the builtin
name `slice`, which would be wrong.

Add a dedicated opcode for the common no-step slice form. As with other
accumulator-shaped bytecodes, the last evaluated operand lives in the
accumulator:

```text
CreateBinarySlice start_reg
```

`CreateBinarySlice` constructs `slice(start, accumulator, None)` through the
shared slice factory. Missing lower or upper bounds are lowered by loading
`None` before the opcode:

```text
a[:c]  -> start_reg = None; accumulator = c;    CreateBinarySlice start_reg
a[b:]  -> start_reg = b;    accumulator = None; CreateBinarySlice start_reg
a[b:c] -> start_reg = b;    accumulator = c;    CreateBinarySlice start_reg
a[:]   -> start_reg = None; accumulator = None; CreateBinarySlice start_reg
```

The name "binary" describes the bytecode operand shape: two explicit slice
bounds plus implicit `step=None`. It must not mean a distinct Python type or a
syntax-proven category. Explicit `a[b:c:None]` still constructs a
`slice_step_none_shape` object, but it goes through the three-operand slice
construction path because the syntax has an explicit step expression.

Full three-field slice syntax uses a separate opcode or helper:

```text
CreateTernarySlice start_reg, stop_reg
```

`CreateTernarySlice` constructs `slice(start_reg, stop_reg, accumulator)`
through the same factory. If `step` evaluates to `None`, the resulting object
still receives `slice_step_none_shape`.

`CreateBinarySlice` is only a fast slice-object constructor. Subscript dispatch
still goes through the normal get/set/del item opcodes with the constructed
slice key.

Use normal bytecode naming conventions:

```cpp
Bytecode::CreateBinarySlice
Bytecode::CreateTernarySlice
CodeObjectBuilder::emit_create_binary_slice(...)
CodeObjectBuilder::emit_create_ternary_slice(...)
```

Do not constant-fold slice syntax in the first implementation. Even fully
literal forms such as `a[1:2]` and `a[:]` should lower through
`CreateBinarySlice` or `CreateTernarySlice`. Constant slice objects can be added
later as a compiler optimization.

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
- non-`None` start, stop, and step eventually use the integer-index protocol.
- negative bounds are adjusted by sequence length.
- bounds are clipped to the valid range.
- negative steps use the reverse-slice default and clipping rules.

Full `__index__` support is out of scope for the first implementation, so the
initial normalizer implements the Python rules only for `None` and SMI field
values. Slice construction remains permissive. Unsupported field values fail at
consumption/`indices()` time.

The shared internal helper for normalization belongs with `Slice`, in
`builtin_types/slice.{h,cpp}`, so Python-facing `slice.indices` and later
builtin sequence slicing do not diverge. The helper should return a normalized
record, not allocate a Python tuple:

```cpp
struct NormalizedSlice {
    int64_t start;
    int64_t stop;
    int64_t step;
    size_t length;
};

[[nodiscard]] Expected<NormalizedSlice>
normalize_slice_for_length(TValue<Slice> slice, int64_t sequence_length);
```

`length` is the number of selected elements. Computing it once avoids each
sequence implementation re-deriving the extended-slice result size.

For the first implementation, the helper's accepted field values are:

```text
None
SMI
```

Any other field value should raise a `TypeError` at consumption time with a
message consistent enough for tests to identify the failure class. The design
should leave a clear internal boundary where full `__index__` support can be
added later.

`slice.indices(length)` is useful Python-facing API and should be implemented
with this helper. It should allocate and return the Python tuple
`(start, stop, step)`, and it must reject negative `length`. `length` is also
SMI-only until general `__index__` support exists.

Builtin sequence fast paths should not call the Python-visible
`slice.indices` method. They should call the shared internal helper directly
when slice consumption is implemented, avoiding tuple allocation and method
dispatch.

## Implementation Checklist

Phase 1, slice object and syntax:

- Add `NativeLayoutId::Slice`.
- Add `NativeLayoutId::Slice` to `native_layout_has_slots()`.
- Add native layout release and object-size descriptors for `Slice`.
- Add `ShapeKey::from_shape(Shape *)`.
- Add `builtin_types/slice.h` and `builtin_types/slice.cpp`.
- Add `Slice` native layout with `start`, `stop`, and `step` `Member<Value>`
  fields, deriving from `SlotObject`.
- Register the public `slice` builtin class in VM builtin initialization.
- Map `NativeLayoutId::Slice` to `slice_class_` through the builtin class
  registry/native-layout mapping path.
- Add `slice_class_`, `slice_step_none_shape_`, and
  `slice_step_value_shape_` to `VirtualMachine`.
- Install `slice_step_none_shape_` as the class instance root and create
  `slice_step_value_shape_` with the same Python-visible descriptors.
- Implement the single `make_slice(start, stop, step)` factory.
- Implement `slice.__new__`/constructor arity behavior and reject keywords.
- Implement `.start`, `.stop`, `.step`, and `__repr__`.
- Add the AST representation for slice key expressions.
- Teach the parser to parse omitted lower/upper/step fields inside `[]`, using
  `-1` for omitted `EXPRESSION_SLICE` children.
- Preserve ordinary expression keys for `a[x]`.
- Lower slice key expressions through the dedicated slice-construction path.
- Add `CreateBinarySlice` for omitted-step slices and `CreateTernarySlice` or
  an equivalent three-field constructor path for explicit-step slices.
- Use builder method names `emit_create_binary_slice` and
  `emit_create_ternary_slice`.
- Preserve RHS-first evaluation for slice assignment.
- Add codegen tests for omitted fields and assignment ordering.

Phase 2, Python-facing normalization:

- Add shared SMI-only slice normalization in `builtin_types/slice.{h,cpp}`.
- Implement Python-facing `slice.indices(length)`.
- Do not install a placeholder `slice.indices` before it is implemented.
- Do not implement slice equality or hashing in this phase.
- Do not implement constant folding of literal slice objects in this phase.

Phase 3, sequence operations and trusted handlers:

- Add list, tuple, and str slice `__getitem__`.
- Add list slice `__setitem__` and `__delitem__` if assignment/deletion syntax
  is in scope for the same implementation pass.
- Update builtin sequence type-error messages to allow slice keys, for example
  "list indices must be integers or slices" rather than only "integers".
- Add resolver branches for `slice_step_none_shape` and
  `slice_step_value_shape`.
- Add interpreter-level tests for user-defined `__getitem__` receiving slices
  and builtin sequence slicing behavior.

## Non-Goals

- Do not create separate Python classes for no-step and with-step slices.
- Do not use different native layouts for the two slice categories.
- Do not encode a shape per exact start/stop/step value.
- Do not treat `slice_step_value_shape` as proof that the step is an integer,
  nonzero, or otherwise valid for slicing.
- Do not add a separate `[:]` Shape initially. The `slice_step_none_shape`
  handler can detect the full-copy case cheaply after the shape guard.
- Do not use `CreateBinarySlice` as a special subscript operation. It only
  constructs a `slice(start, stop, None)` object.
- Do not lower slice syntax through a lookup of the Python builtin name
  `slice`.
- Do not support subclassing `slice`; CPython rejects it, and the shape split
  assumes exact immutable slice instances.
- Do not implement slice equality or hashing in the initial milestones.
- Do not constant-fold literal slice objects initially.
- Do not implement multidimensional tuple keys in the first pass. Leave
  `a[1, 2:3]` unsupported while ordinary single slice keys are implemented.

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
- C++ tests for the shape invariant:
  `slice(1, 2)` and `slice(1, 2, None)` use `slice_step_none_shape`, while
  `slice(1, 2, 3)` uses `slice_step_value_shape`.
- `slice.indices(length)` with SMI field values and SMI length, including
  negative stop normalization such as `slice(0, -1).indices(5) == (0, 4, 1)`.
- list, tuple, and str contiguous slicing.
- extended slicing, including negative step and zero-step error.
- invalid slice field values failing when consumed, not when constructed.

Codegen tests should stay focused on structural guarantees, such as omitted
fields lowering to `None` and slice assignment preserving RHS-first evaluation
order.
