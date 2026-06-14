# BigInt Design

This document describes the planned arbitrary-precision integer implementation
for clovervm. The design keeps the existing SMI representation as the fast path
and adds a heap `BigInt` representation for integer values outside the SMI
range.

Status: accepted staged design, partially implemented. Representation,
conversion, formatting, parsing, source literal bodies, and comparisons are in
place. Basic arithmetic and opcode overflow routing are the next slices.

## Goals

- Keep SMI arithmetic as the common fast path.
- Represent non-SMI integers as heap objects whose Python-visible class is
  still `int`.
- Store BigInt digits inline in one variable-sized heap allocation.
- Canonicalize VM-created integer results to SMI whenever the value fits the
  SMI range.
- Support `bool`, SMI, and BigInt as Python integer operands where Python
  semantics require it.
- Route SMI fast-path overflow through ordinary operator dispatch so inline
  caches can record the resulting trusted BigInt path.
- Keep BigInt allocation and arithmetic out of hot opcode handlers in the first
  implementation.

## Non-Goals For The First Slice

- No full division or modulo.
- No power.
- No bitwise operations.
- No BigInt-aware `range`.
- No BigInt-to-float conversion or mixed BigInt/float arithmetic.
- No BigInt indexing support for large containers. List, tuple, string, and
  slice internals remain SMI-sized for now.
- No BigInt hashing while `hash()` and non-string dictionary keys are not
  exposed.
- No dependency on `rqm`. Clovervm should copy and adapt the relevant design
  decisions, not import the library.

## Runtime Representation

`BigInt` should live with builtin numeric types, under `src/builtin_types/`.
It is an implementation detail of Python `int`, not a new public builtin type.
`NativeLayoutId::BigInt` should map to the existing `int` class.

The representation is sign-magnitude with little-endian 32-bit digits:

```cpp
using digit_t = uint32_t;
using double_digit_t = uint64_t;
static constexpr uint32_t kDigitBits = sizeof(digit_t) * 8;

class BigInt : public Object
{
public:
    static constexpr NativeLayoutId native_layout = NativeLayoutId::BigInt;

    digit_t digits[1];
};
```

The exact field layout should satisfy native-layout alignment and object-size
requirements, but the object should contain:

- a raw integer digit count, not a `Value` cell;
- a `signum` of `-1`, `0`, or `1`;
- trailing inline digit storage.

`BigInt` contains no owned `Value` cells beyond the inherited `Object` shape
field. Digit count, sign, and digits are raw native metadata.

Canonical representation rules:

- Zero is `signum == 0` and `n_digits == 0`.
- Nonzero values have `signum == -1` or `signum == 1`.
- Nonzero values have no high zero digits.
- Normal public result paths return SMI for representable values instead of a
  heap BigInt.
- Returned heap BigInts are exact-sized to the normalized digit count.

Small boxed BigInts may eventually be needed for extension compatibility, but
that should be a separate compatibility-object decision. Normal arithmetic,
parsing, and VM conversion paths should preserve SMI canonicalization.

## Views, Small Operands, And Scratch Storage

Arithmetic kernels should operate on borrowed views rather than owning objects:

```cpp
using digit_t = uint32_t;
using double_digit_t = uint64_t;
using signum_t = int16_t;

struct ConstBigIntView
{
    size_t n_digits;
    signum_t signum;
    const digit_t *digits;
};

struct MutableBigIntView
{
    size_t capacity;
    size_t n_digits;
    signum_t signum;
    digit_t *digits;

    ConstBigIntView view() const
    {
        return ConstBigIntView{n_digits, signum, digits};
    }

    operator ConstBigIntView() const { return view(); }
};
```

`ConstBigIntView` is read-only and has no capacity. `MutableBigIntView` is for
destination storage, includes capacity, and can convert to `ConstBigIntView`.
`BigInt::view()` must return only `ConstBigIntView`; only scratch or construction
code should expose `MutableBigIntView`.

Mixed SMI/BigInt operations should not allocate temporary BigInts just to read
SMI operands. Use a stack-backed small BigInt owner:

```cpp
class SmiBigInt
{
public:
    explicit SmiBigInt(int64_t decoded_smi_range_int);
    operator ConstBigIntView() const;
    ConstBigIntView view() const;

private:
    size_t n_digits_;
    int16_t signum_;
    digit_t digits_[2];
};
```

`SmiBigInt` is literally a stack-backed BigInt value sized for a decoded
SMI-range integer. It owns enough digit storage for any SMI magnitude and can
be converted to `ConstBigIntView` for arithmetic.

Important naming rule: `SmiBigInt` takes a decoded SMI-range integer, not tagged
SMI bits. In clovervm there are two different "SMI" concepts:

- tagged SMI bits: the encoded `Value` form, `decoded << value_tag_bits`;
- decoded SMI-range integer: an ordinary `int64_t` known to fit the SMI range.

Helpers should make this distinction obvious. Avoid names such as
`from_smi(int64_t)`. Prefer names like:

- `from_int64` for full signed 64-bit decoded integers;
- `from_smi_range_int` for decoded integers with SMI-range preconditions;
- `from_smi_value` only when the parameter is a `Value` or `TValue<SMI>`.

`from_smi_range_int` should assert the SMI range. Bool normalization belongs in
the `int` operand adapters before constructing a `SmiBigInt`.

Mutable arithmetic results should use explicit scratch storage rather than
allocating a Python heap `BigInt` as a temporary work buffer:

```cpp
class BigIntScratch
{
public:
    explicit BigIntScratch(size_t capacity);
    MutableBigIntView mutable_view();
    ConstBigIntView view() const;

private:
    static constexpr uint32_t kInlineDigits = 8;

    size_t capacity_;
    size_t n_digits_;
    signum_t signum_;
    digit_t *digits_;
    digit_t inline_digits_[kInlineDigits];
    std::vector<digit_t> overflow_;
};
```

`BigIntScratch` should keep common small results in inline native storage and
fall back to overflow backing for larger temporaries. This keeps Python heap
`BigInt` objects exact-sized and canonical while still avoiding heap BigInt
allocation when a result normalizes back into the SMI range.

This intentionally differs from current `rqm::znum`, which can allocate an
oversized result object first and then trim its visible digit count. In
clovervm, heap `BigInt` objects are Python-visible values, not mutable scratch
buffers. Oversized temporary storage belongs in `BigIntScratch`; heap `BigInt`
allocation happens only after the normalized result size is known.

Scratch digit storage is uninitialized. Kernels that need zeroed destination
digits must explicitly initialize the range they use.

## Conversion

The BigInt layer should provide full `int64_t` conversion and a narrower
SMI-range path:

- Full `int64_t` conversion must handle `INT64_MIN`.
- Do not use `std::abs(int64_t)` for magnitude extraction.
- SMI-range conversion can rely on the stronger SMI bounds and avoid the
  `INT64_MIN` edge.
- Result finalization promotes a normalized `ConstBigIntView` into a
  Python-visible integer `Value`. It should return `Expected<Value>`: return
  SMI if the value is representable, otherwise allocate an exact-sized heap
  BigInt and copy the normalized digits from scratch storage. A helper named
  `make_uninitialized_bigint_for_digits` should allocate the exact-sized heap
  object used by finalization and low-level allocation tests.
- Decimal formatting should support BigInt `str()` and `repr()`.
- Decimal `int(str)` parsing should preserve the existing accepted/rejected
  grammar while changing overflow behavior. The implemented shape is an
  allocation-free SMI fast path and a BigInt-backed slow path that finalizes
  back to SMI when the result is representable.
- Source-code integer literal bodies should use the same decimal parsing and
  finalization path. A leading `-` is still unary negation, so negative
  out-of-SMI literals require the Stage 4 negation work before they fully
  promote.

The `rqm` `znum` design has two pitfalls that should not be copied:

- `std::abs(int64_t)` is not safe for `INT64_MIN`.
- `znum::minus_one()` appears to set a positive sign.

Both cases should have direct tests in clovervm.

## Integer Operand Categories

After BigInt exists, integer checks must be explicit. These categories should
be represented by helper functions instead of open-coded `is_integer()` plus
`get_smi()`:

- intlike: `bool`, SMI, or BigInt;
- exact int: SMI or BigInt, not `bool`;
- SMI-sized int: `bool` or SMI normalized to a decoded SMI-range `int64_t`;
- internal index/count: SMI-sized only in the first slice.

Python arithmetic treats bool as an int subclass, so int dunder handlers should
accept bool operands. The BigInt class itself should not know about bool; bool
to integer normalization is a higher-level int-method responsibility.

## Arithmetic And Dispatch

The first arithmetic slice should include:

- equality and ordering comparisons across bool, SMI, and BigInt;
- unary `+`;
- unary `-`;
- addition;
- subtraction;
- multiplication;
- decimal string formatting.

Arithmetic kernels should be representation-oriented and Python-policy-free.
They should accept `ConstBigIntView` operands and write into caller-provided mutable
result storage. `int.cpp` owns Python-visible policy: returning SMI or BigInt,
returning `NotImplemented`, and raising exceptions.

Public arithmetic kernels should assume the destination storage is distinct
from every input view. Do not carry over `rqm`'s helper-specific in-place
aliasing behavior. If a future division or GCD implementation needs in-place
work buffers, give those internal helpers explicit aliasing contracts.

SMI opcode handlers should keep the current hot path shape. On SMI overflow,
they should tail-call the ordinary operator dispatch path. The selected int
dunder handler can then specialize for BigInt promotion, and the operator inline
cache can record the trusted choice.

Both trusted and non-trusted builtin int dunder handlers should support BigInt
operands in the first arithmetic slice. Opcode handlers should not implement
BigInt arithmetic directly. Unary negation of `value_smi_min` should promote
through the dunder/trusted-handler path rather than raising overflow in the
opcode handler.

Arithmetic result construction should be scratch-first:

1. Estimate result digit capacity.
2. Create `BigIntScratch` for mutable result storage.
3. Run the kernel into `scratch.mutable_view()`.
4. Normalize the result.
5. Finalize to `Expected<Value>`: SMI if representable, otherwise an
   exact-sized heap `BigInt` with the normalized digits copied in.

Do not use heap `BigInt` objects as oversized mutable work buffers. That would
mix Python object allocation concerns into the BigInt object shape, require a
separate capacity field, and allow invisible spare digits on returned objects.

## Hashing

Hashing is deferred while `hash()` and non-string dictionary keys are not
exposed. Before either becomes Python-visible for integers, BigInt hashing must
preserve Python equality requirements:

- `True == 1` and `hash(True) == hash(1)`.
- `False == 0` and `hash(False) == hash(0)`.
- BigInts that compare equal must hash equal.
- Any noncanonical small heap integer that appears through a future edge path
  must hash the same as the equivalent SMI.

The BigInt hash should be simple, deterministic, and based on normalized signed
magnitude digits.

## SMI-Sized Boundaries

Some runtime features currently require SMI-sized integers. The first BigInt
slice should keep those boundaries explicit:

- list, tuple, and string indices remain SMI-sized;
- slice normalization remains SMI-sized;
- `range` may reject out-of-SMI BigInts in the first slice, but should become
  BigInt-aware in a later step;
- non-integers should keep existing `TypeError` behavior;
- integers that are too large for these temporary limits should raise
  `OverflowError`.

Any site that broadens a type check from SMI to intlike must not immediately
call `get_smi()` without an explicit checked conversion.

## Bitwise Semantics

BigInt storage is sign-magnitude because add, subtract, multiply, decimal
conversion, and comparison are more important early than bitwise operations.
Python bitwise semantics are nevertheless defined as if integers used infinite
two's-complement representation:

- `~x == -x - 1`;
- `x >> n == floor(x / 2**n)` for `n >= 0`;
- `x << n == x * 2**n` for `n >= 0`;
- `&`, `|`, and `^` operate with infinite sign extension.

Future bitwise implementations should translate from sign-magnitude storage to
this semantic model. CPython also stores integer magnitudes separately from the
sign and implements the two's-complement behavior at the operation layer.

## Staging

Recommended first implementation slice:

1. Add `BigInt`, `ConstBigIntView`, `MutableBigIntView`, `SmiBigInt`, and
   `BigIntScratch`.
2. Register `NativeLayoutId::BigInt` to the existing `int` class.
3. Add conversion helpers for full `int64_t`, decoded SMI-range ints, and
   result finalization.
4. Add decimal formatting for BigInt values.
5. Add comparisons across bool, SMI, and BigInt.
6. Add BigInt-aware `int(str)` parsing and source integer literal bodies.
7. Add unary plus, unary minus, addition, subtraction, and multiplication in
   int dunder handlers.
8. Change SMI overflow paths to enter operator dispatch rather than reporting
   integer overflow for operations that now promote.

Later slices:

- BigInt division and modulo with Python floor-division semantics.
- Bitwise operations.
- Power.
- BigInt-aware `range`.
- BigInt-to-float conversion and mixed BigInt/float operations.
- Any remaining source-literal follow-up needed after unary negation is
  BigInt-aware.
- Extension-compatibility boxed small-int policy if needed.

## Tests To Pin

- `-1` construction has negative sign and stringifies as `-1`.
- `INT64_MIN` converts to BigInt and back to `int64_t`.
- Public result paths return SMI for zero and other SMI-range results.
- Addition, subtraction, multiplication, and negation promote on SMI overflow.
- BigInt results that shrink back into SMI range canonicalize to SMI.
- Comparisons work across bool, SMI, and BigInt.
- Decimal formatting works for values above and below the SMI range.
- `int(str)` preserves malformed-string behavior while allowing values above
  the SMI range.
- Positive source integer literals above the SMI range produce BigInts.
- Non-SMI BigInt indices and slice fields raise `OverflowError` in v1.
