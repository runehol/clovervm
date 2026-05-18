# Typed Handles And Expected Results

This is a draft design note for reducing the current `Value` / `TValue` /
`Owned` / `Member` trait surface while adding explicit types for VM-level
exception propagation and optional values.

The immediate motivation is that raw `Value` can represent too many states:

- a valid value of the expected semantic type
- `None`
- `not_present`
- `exception_marker`
- a value of the wrong semantic type

That is good as a compact runtime representation, but it is too dynamically
typed for internal C++ APIs. In particular, a conversion from `Value` to
`TValue<String>` may need to raise `TypeError`. If the function can return
`exception_marker`, the result is not really a `TValue<String>`.

## Goals

- Keep `Value` as the compact storage and ABI representation.
- Make C++ APIs express which checks are still owed:
  - type check
  - `None` check
  - pending-exception propagation
  - ownership/lifetime
- Avoid C++ exceptions on VM-semantic paths.
- Support an eventual `-fno-exceptions` build.
- Avoid a new explosion of trait types.
- Preserve niche-backed representations where possible, so wrappers do not add
  a second boolean/tag word.

## Non-Goals

- This is not a proposal to change the `Value` word layout.
- This is not a proposal to make VM allocation generally recoverable.
- This is not a proposal to represent host failure, corrupt invariants, or true
  VM-internal OOM as Python exceptions.

Python-semantic allocation requests can fail with `MemoryError`; VM-internal
allocation failure may still be fatal. For example, `[0] * (2**50)` should be
rejected before raw allocation and should raise `MemoryError`.

## Current State

The current typed-value machinery has two overlapping conversion systems.

### Native-layout conversion helpers

`can_convert_to<T>`, `try_convert_to<T>`, and `assume_convert_to<T>` answer
questions about native object layouts:

```cpp
can_convert_to<T>(value)      // bool
try_convert_to<T>(value)      // T * or nullptr
assume_convert_to<T>(value)   // assert and return T *
```

For ordinary object subclasses, these check:

```cpp
value.is_ptr() &&
value.get_ptr<Object>()->native_layout_id() == T::native_layout
```

This mechanism is object/native-layout specific.

### `ValueTypeTraits<T>`

`ValueTypeTraits<T>` currently expresses several separate concerns:

- runtime semantic type predicate
- unchecked extraction type
- unchecked extraction operation
- refcount policy optimization

For ordinary object subclasses, it duplicates the same exact-native-layout
check as `can_convert_to<T>`. For inline semantic types, it handles cases that
`can_convert_to<T>` cannot express:

```cpp
TValue<SMI>    // value.is_smi(), extracts int64_t
TValue<CLInt>  // value.is_integer(), currently extracts no C++ payload
```

It also carries `RefcountPolicy::{Never, Maybe, Always}`. That policy is an
ownership concern, not a type-classification concern.

`Always` is not stable under composition. Even if `TValue<Foo>` always holds a
refcounted pointer today, these do not:

```cpp
OptionalValue<TValue<Foo>>
Expected<TValue<Foo>>
Expected<OptionalValue<TValue<Foo>>>
```

They may hold `None` or `exception_marker`. Also, interning can make a semantic
type's storage class `Maybe` without changing the semantic type.

### `HandleTraits<TValue<T>>`

`HandleTraits<TValue<T>>` adapts typed values to `Owned` and `Member`. It
currently expresses:

- conversion from raw `Value`
- conversion to raw `Value`
- default empty sentinel
- extraction forwarding
- retain/release behavior

This creates a second trait stack on top of `ValueTypeTraits<T>`.

## Proposed Vocabulary

### `TValue<T>`

`TValue<T>` should mean:

```text
definitely a T
not None
not exception_marker
not not_present
```

Construction should not raise. There should be only assert-only or unchecked
constructors:

```cpp
TValue<T>::from_value_assumed(value);    // asserts ValueType<T>::matches(value)
TValue<T>::from_value_unchecked(value);  // raw internal escape hatch
TValue<T>::from_oop(ptr);
TValue<T>::from_smi(n);
```

The current `from_value_checked()` name should be deprecated or removed because
it currently means "throw a C++ exception on mismatch." VM-semantic conversion
belongs in a function whose return type admits exception propagation.

### `Expected<T>`

`Expected<T>` is the VM-level analogue of `std::expected<T, E>`, except the
error payload is stored out-of-band in `ThreadState` pending exception state.

```text
Expected<T> = either T, or pending VM exception
```

The failure state is represented by `Value::exception_marker()` where the
payload type has a `Value` niche. For non-`Value` payloads, a specialized niche
may be used.

Core API:

```cpp
static Expected ok(T value);
static Expected exception();

bool has_value() const;
bool has_exception() const;

T value() const;  // asserts has_value()
```

`value()` should assert on the exceptional state. Calling it without first
propagating or checking is a C++ programmer error, not a Python runtime event.

The name `value()` is reserved for semantic unwrapping, following
`std::expected` and `std::optional`. Low-level access to the backing `Value`
representation should use `raw_value()` instead. For example,
`Expected<TValue<String>>::raw_value()` may return `Value::exception_marker()`;
`Expected<TValue<String>>::value()` must not.

Examples:

```cpp
Expected<TValue<String>>
Expected<OptionalValue<TValue<String>>>
Expected<AstIndex>
Expected<AstVector>
Expected<Unit>
```

### `OptionalValue<T>`

`OptionalValue<T>` represents:

```text
None or T
```

For VM-backed payloads, it should use `Value::None()` as the niche. For example:

```cpp
OptionalValue<TValue<String>>
```

means either Python `None` or definitely a `String`.

### `Unit`

`Unit` represents success with no meaningful payload:

```cpp
Expected<Unit>
```

This is the VM equivalent of `Result<(), PendingException>`.

For APIs whose successful VM-level value is Python `None`, `Unit::raw_value()`
can return `Value::None()`.

### `AstIndex`

Parser functions currently return raw `int32_t` AST node indices. If parser
functions become fallible, prefer:

```cpp
Expected<AstIndex>
```

over:

```cpp
Expected<int32_t>
```

This makes the `-1` niche explicit and avoids teaching arbitrary integers that
`-1` means pending exception.

## Conversion Ladder

The API should distinguish non-raising checks, assert-only conversion, and
VM-semantic raising conversion.

Object/native-layout helpers:

```cpp
can_convert_to<T>(value)       // non-raising predicate
try_convert_to<T>(value)       // T * or nullptr
assume_convert_to<T>(value)    // assert-only pointer conversion
```

Typed handle construction:

```cpp
TValue<T>::from_value_assumed(value)    // assert-only typed handle conversion
TValue<T>::from_value_unchecked(value)  // raw escape hatch
```

VM-semantic conversion:

```cpp
TValue<T>::require(value) -> Expected<TValue<T>>
```

`require()` sets a pending `TypeError` and returns `Expected<TValue<T>>::exception()`
when the value does not match.

Optional VM-semantic conversion:

```cpp
TValue<T>::require_optional(value)
    -> Expected<OptionalValue<TValue<T>>>
```

That conversion accepts `None`; otherwise it performs the same type check as
`require()`.

## Factoring Traits

The target shape should keep traits narrow.

### `ValueType<T>`

One trait should classify semantic VM value types:

```cpp
template <typename T>
struct ValueType
{
    using extracted_type = ...;

    static bool matches(Value value);
    static extracted_type extract_unchecked(Value value);
};
```

For ordinary object subclasses, it can delegate to the existing conversion
helpers:

```cpp
static bool matches(Value value)
{
    return can_convert_to<T>(value);
}

static T *extract_unchecked(Value value)
{
    return assume_convert_to<T>(value);
}
```

For inline semantic types:

```cpp
ValueType<SMI>::matches(value)   // value.is_smi()
ValueType<CLInt>::matches(value) // value.is_integer()
```

This trait should not express refcount policy.

### Handle-like VM values

Any VM-backed handle should expose a small concrete protocol:

```cpp
Value raw_value() const;
static H from_value_unchecked(Value value);
```

This should cover:

```cpp
Value
TValue<T>
OptionalValue<H>
Expected<H>       // when H is VM-backed
```

Where possible, use concrete methods and detection instead of introducing a
large `HandleTraits` hierarchy.

### Ownership

`Owned<H>` and `Member<H>` should be orthogonal to semantic wrappers. For any
VM-backed handle `H`, ownership can retain and release through generic
`Value` operations:

```cpp
incref(handle.raw_value());
decref(handle.raw_value());
```

Generic `incref(Value)` and `decref(Value)` already skip inline values,
`None`, `exception_marker`, and interned pointers. Therefore `RefcountPolicy`
is not semantically necessary.

This makes the following compositions natural:

```cpp
Owned<TValue<String>>
Owned<OptionalValue<TValue<String>>>
Owned<Expected<TValue<String>>>
Owned<Expected<OptionalValue<TValue<String>>>>

Member<TValue<String>>
Member<OptionalValue<TValue<String>>>
```

`Member<Expected<T>>` should be mechanically possible only if there is a real
use case. As a design rule, exception markers should generally be transient
control flow, not heap object state.

## Factories

Constructors should not perform VM-fallible work. They should establish object
layout from already-valid inputs.

Factories should perform:

- validation
- `Value` to `TValue<T>` conversion
- Python-level allocation limit checks
- pending-exception propagation
- final raw allocation/construction

Example:

```cpp
class Foo : public Object
{
public:
    Foo(ClassObject *cls, TValue<String> name, TValue<Tuple> args);

    static Expected<TValue<Foo>> make(Value name, Value args);
};
```

Factory body:

```cpp
auto typed_name = TValue<String>::require(name);
CL_TRY(typed_name);

auto typed_args = TValue<Tuple>::require(args);
CL_TRY(typed_args);

return Expected<TValue<Foo>>::ok(
    active_thread()->make_object_value<Foo>(
        typed_name.value(), typed_args.value()));
```

This avoids half-constructed heap objects and aligns with `-fno-exceptions`.

## Parser And Tokenizer

The parser is an important non-`Value` use case. Most parser functions naturally
return AST node indices, not VM values:

```cpp
Expected<AstIndex> expression();
Expected<AstIndex> statement();
Expected<AstIndex> block();
Expected<AstVector> parse(...);
Expected<Unit> consume(Token expected);
```

There are two possible designs for syntax errors:

1. Parser/tokenizer return explicit diagnostics, and `ThreadState::compile`
   converts them to pending `SyntaxError` / `IndentationError`.
2. Parser/tokenizer receive a `ThreadState` and directly set pending VM
   exceptions, returning `Expected<T>`.

The first keeps parser/tokenizer logic less tied to VM exception object
construction. The second makes parse failures participate directly in the same
`Expected<T>` propagation protocol. Either works with `-fno-exceptions`.

## Open Questions

- Should the public trait be named `ValueType`, `ValueTypeTraits`, or something
  else?
- Should `can_convert_to<T>` remain object-only, or should it delegate to
  `ValueType<T>` and cover inline semantic types too?
- How much compatibility should be kept for `from_value_checked()` during the
  migration?
- Should `Owned<H>` require a small `HandleOps<H>` trait for `none()` and raw
  `Value` construction, or can this be handled with concrete methods and a
  `Value` specialization?
- Should `Expected<T>` support `raw_value()` only for VM-backed `T`, or should
  that be a separate `ExpectedValue<T>` specialization?
- Should parser failures first use explicit parse diagnostics, or immediately
  set pending `SyntaxError` on `ThreadState`?
