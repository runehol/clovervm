# Float Design

This document describes the first `float` implementation for clovervm. The
initial target is not complete Python numeric behavior. The target is enough
correct, explicit float support to run an adapted Benchmarks Game `nbody`
benchmark while preserving the existing small-integer fast paths.

Status: this design has been implemented through the adapted `nbody` benchmark.
This document is retained as rationale and future-work context; the completed
execution checklist lives in `doc/float-staging-plan.md`.

## Goals

- Represent Python `float` values as heap objects backed by C++ `double`.
- Keep the existing `Value` representation unchanged. Do not add NaN tagging,
  boxed-inline doubles, or other alternate numeric encodings.
- Preserve the current SMI interpreter fast paths for integer arithmetic.
- Support float literals and the numeric operations needed by `nbody`.
- Support float truthiness.
- Support enough printing for debugging, tests, and benchmark output.
- Keep constructor conversion out of scope until general `__new__` semantics
  exist.

## Non-Goals

- No `float(...)` constructor behavior in the first implementation.
- No int/string/object-to-float conversion helpers hidden behind the `float`
  builtin.
- No NaN tagging or immediate float representation.
- No attempt at complete Python numeric tower behavior.
- No import, `math` module, `sys.argv`, or Benchmarks Game-compatible command
  line harness work as part of the float type itself.
- No broad generic numeric dispatch layer in front of common opcodes unless
  measurements later justify it.

The line for construction is intentionally sharp: float literals create floats;
constructor conversion waits for real `__new__`.

## Runtime Representation

`Float` should be a Python-visible heap object:

```cpp
class Float : public Object
{
public:
    static constexpr NativeLayoutId native_layout = NativeLayoutId::Float;

    Float(ClassObject *cls, double _value)
        : Object(cls, native_layout), value(_value)
    {
    }

    double value;

    CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(Float, Object, 0);
    CL_DECLARE_STATIC_OBJECT_SIZE(Float);
};
```

The object has no float-specific child `Value`s, so the native release span only
extends the inherited `Object` span. The class object should be registered like
other builtin classes and inserted into the builtin scope as `float`, but
calling that class is not part of the first feature.

`Value::is_integer()` should remain SMI-only. A float is a pointer value whose
native layout is `NativeLayoutId::Float`.

## Literal Support

Float values initially enter the runtime through literals. The tokenizer and
parser need to distinguish integer and floating numeric forms.

Required literal forms for the first pass:

- `1.0`
- `1.`
- `.5`
- `1e3`
- `1E3`
- `1.2e-3`
- forms with Python-valid underscores where the tokenizer already accepts or is
  extended to accept them consistently

Integer literals should continue to produce SMIs where possible. Float literals
should allocate `Float` objects and place them in the constant table. Codegen can
then use the existing `LdaConstant` path for non-SMI constants.

The exact accepted numeric grammar should be tested at the tokenizer and parser
levels. It should not silently parse a prefix of an invalid numeric literal in a
way that changes Python's token boundaries.

## Truthiness

Float truthiness is required. Python semantics are:

- `0.0` is false.
- `-0.0` is false.
- Every other float is true, including NaN.

The current `JumpIfTrue` and `JumpIfFalse` handlers handle only inline values
directly and send pointer values to a slow path. That is a good boundary. Add a
cold truthiness slow path that recognizes `Float` and applies `value != 0.0`.

Do not implement general Python object truthiness in this change unless that is
already part of a separate object-protocol step. Float truthiness can be an
explicit case behind the existing pointer slow path.

## Arithmetic Required For NBody

The first arithmetic set should be:

- `+`
- `-`
- `*`
- `/`
- unary `-`
- unary `+`

Each binary operator must support:

- `float op float`
- `int op float`
- `float op int`

For `/`, implement Python 3 true division semantics:

- `int / int` returns a `Float`
- `int / float` returns a `Float`
- `float / int` returns a `Float`
- `float / float` returns a `Float`

Division by zero should raise `ZeroDivisionError`. If that builtin exception
does not exist yet, add it as part of the division work rather than reporting a
generic VM exception.

Unsupported numeric operations should fail explicitly rather than falling into a
generic C++ runtime failure. Operations deliberately out of scope for the first
float slice:

- `//`
- `%`
- `**`
- bit shifts
- bitwise operators
- `divmod`
- `round`
- `abs`
- numeric constructor conversions

If the adapted `nbody` benchmark later proves it needs one of these operations,
add the operation deliberately with tests.

## Comparisons

The first comparison set should be:

- `<`
- `<=`
- `>`
- `>=`
- `==`
- `!=`

Each comparison must support:

- `float cmp float`
- `int cmp float`
- `float cmp int`

Float comparisons should use C++ `double` comparison behavior, which gives the
expected Python-visible behavior for ordinary finite values and for NaN in basic
comparisons:

- comparisons involving NaN are false except `!=`
- `NaN != x` is true

This is enough for numeric benchmark code. Rich comparison protocol behavior for
arbitrary objects is separate work.

## Opcode Strategy

Keep the hot SMI paths as direct as they are today. For `Add`, `Sub`, and `Mul`,
the existing handlers should continue to:

1. Load operands.
2. Check for all-SMI operands.
3. Run the encoded-SMI fast path.
4. Tail-call a cold path for non-SMI operands or overflow.

Float handling belongs after the SMI fast path, not in a broad dispatch layer in
front of it. However, float arithmetic is expected to be common enough that the
first tail-called destination should be a narrow numeric continuation, not the
fully generic object fallback.

The preferred shape is:

1. Opcode handler handles all-SMI operands inline.
2. On non-SMI, tail-call an operator-specific numeric path.
3. The numeric path recognizes only the supported `float`/SMI combinations,
   promotes both operands to `double`, and performs pure double arithmetic.
4. On numeric miss, tail-call the generic unsupported-operation or protocol path.

For example, `op_add` should tail-call something like `op_add_numeric` when
either operand is not an SMI. `op_add_numeric` should quickly handle:

- `float + float`
- `float + int`
- `int + float`

by extracting or promoting both operands to `double`, computing the operation as
double arithmetic, and returning a new heap `Float`. It should tail-call onward
if neither operand shape matches this first numeric tier. This keeps the common
integer path obvious while avoiding an unnecessarily expensive generic fallback
for semi-hot float arithmetic.

The numeric continuation should not preserve an integer result type for mixed
operations. Once the path has accepted a float operand, the operation is a float
operation. For example, `1 + 2.0` and `1.0 + 2` both compute as `double` and
return `Float`.

`Div` is different because Python 3 division is float-producing even for
`int / int`. It should not mirror integer `Add`/`Sub`/`Mul` semantics. The `Div`
handler can either directly tail-call the division helper or keep only minimal
operand setup before entering a cold numeric division path.

The numeric continuations should be explicit about which operator they
implement. Avoid a large generic numeric operation function unless duplication
becomes a measured maintenance problem.

## Sqrt

The adapted `nbody` benchmark needs square root. Since import support and the
`math` module are out of scope, provide a VM builtin native function:

```python
sqrt(x)
```

Initial behavior:

- Accept `int` and `float`.
- Return `float`.
- Use C++ `std::sqrt`.

Domain behavior should be explicit. Matching CPython means `sqrt(-1.0)` raises
`ValueError: math domain error`; using raw `std::sqrt` and returning NaN would
be easier but would leak C library behavior into Python-visible semantics. The
better first implementation is to raise `ValueError` for negative inputs.

This builtin is benchmark support, not constructor conversion plumbing.

## Printing And Repr

Float needs `__str__` and `__repr__` so that:

- `print(1.5)` works.
- `repr(1.5)` works.
- debugging and tests can inspect float results.
- adapted benchmark output can print energies if desired.

The first implementation should prefer a stable, Python-like shortest
round-trippable representation over `std::to_wstring`, because
`std::to_wstring(1.5)` produces noisy fixed precision output such as
`1.500000`.

Acceptable first behavior:

- finite values print in a compact decimal/scientific form;
- integer-valued finite floats include `.0`, e.g. `1.0`;
- negative zero prints as `-0.0`;
- infinity and NaN use Python spellings: `inf`, `-inf`, `nan`.

Exact CPython formatting parity is not required for the first slice, but tests
should pin representative behavior so output does not drift accidentally.

## NBody Benchmark Shape

The repo benchmark should be named `nbody`, matching the Benchmarks Game name:

- `benchmark/nbody.py`
- `benchmark/nbody.cpp`
- `BM_NBody`

The Clover benchmark should be adapted to the current VM rather than copied
verbatim from the Benchmarks Game. In particular:

- define `run(n)` like the existing benchmarks;
- avoid imports;
- call builtin `sqrt`;
- avoid `sys.argv`;
- avoid relying on `%` string formatting unless string formatting is added
  separately;
- choose a harness result strategy independently from the float runtime design.

The current benchmark harness assumes integer results. That is a harness
convenience, not a VM constraint. We can later decide whether `nbody` returns a
`Value`, returns no meaningful result and uses `benchmark::DoNotOptimize`, or
uses a benchmark-specific validation path.

## Future Work

Future steps can add:

- `float.__new__` once general `__new__` semantics are implemented;
- `float(...)` conversions;
- richer math builtins or a real `math` module;
- `%`, `//`, `**`, `abs`, and other numeric operations as real use cases demand;
- optimized float opcode fast paths if benchmarks show heap-float arithmetic is
  important enough to justify the interpreter complexity.
