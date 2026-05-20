# Typed Values, Optional, Expected, And Ownership

This note documents the current direction for CloverVM's C++ value wrappers.
`Value` remains the compact runtime word and ABI representation. The wrapper
types describe what a C++ API has already proved about that word: semantic type,
optional absence, pending-exception propagation, and ownership.

The motivating problem is that raw `Value` can represent too many states:

- a value of the expected semantic type
- `None`
- `not_present`
- `exception_marker`
- a value of the wrong semantic type

That flexibility is useful at VM boundaries and in interpreter registers, but it
is too dynamically typed for most internal C++ APIs. Code that has proved "this
is a string" should say `TValue<String>`. Code that may raise while proving that
fact should say `Expected<TValue<String>>`.

## Goals

- Keep `Value` as the compact storage and ABI representation.
- Make C++ APIs express which checks are still owed:
  - semantic type check
  - `None` check
  - pending-exception propagation
  - ownership/lifetime management
- Avoid C++ exceptions on VM-semantic paths, with an eventual `-fno-exceptions`
  build in mind.
- Prefer factory functions over fallible constructors.
- Keep traits narrow and local to representation facts.
- Preserve niche-backed representations where `Value` already has spare states.

## Non-Goals

- This is not a change to the `Value` word layout.
- This is not a plan to make all VM allocation recoverable.
- This is not a plan to model host failure, corrupt invariants, or VM-internal
  out-of-memory as Python exceptions.

Python-semantic allocation requests can fail with `MemoryError`; VM-internal
allocation failure may still be fatal. For example, `[0] * (2**50)` should be
rejected before raw allocation and should raise `MemoryError`.

## Core Vocabulary

### `Value`

`Value` is the untyped runtime word. It can hold normal Python values, inline
sentinels, and VM control-flow markers. Use it for interpreter registers,
heterogeneous storage, low-level runtime boundaries, and places where a value is
genuinely not known more precisely.

`Value` has `raw_value()` returning itself. That lets generic code compare or
refcount handle-like objects without first erasing typed handles through an
implicit conversion.

### `TValue<T>`

`TValue<T>` is a borrowed typed handle:

```text
definitely semantic T
not None unless T is None
not exception_marker
not not_present
```

It stores a `Value`, but its public type records the proof that the underlying
word satisfies `TValueTraits<T>::is_instance(value)`.

Common construction APIs:

```cpp
TValue<T>::from_value_checked(value)     // checked, sets TypeError on mismatch
TValue<T>::from_value_or_raise(value, type_name, message)
TValue<T>::from_value_assumed(value)     // assert-only checked conversion
TValue<T>::from_value_unchecked(value)   // raw internal escape hatch
TValue<T>::from_oop(ptr)
TValue<T>::from_smi(n)
TValue<Bool>::from_bool(b)
TValue<None>::None()
```

`from_value_checked()` and `from_value_or_raise()` return
`Expected<TValue<T>>`. They are the VM-semantic conversions: a mismatch sets
pending exception state and returns the exception path. `from_value_assumed()`
is for code that has already proved the type. `from_value_unchecked()` exists
for representation plumbing and should remain visibly scary.

`extract()` returns the useful C++ view for the semantic type:

```cpp
TValue<String>::extract()  // String *
TValue<SMI>::extract()     // int64_t
TValue<Bool>::extract()    // bool
TValue<None>::extract()    // void
TValue<CLInt>::extract()   // Value
```

`raw_value()` returns the backing `Value` for refcounting, low-level storage,
and APIs that intentionally cross back to the untyped representation.

### `Optional<T>`

`Optional<T>` is the VM-level optional wrapper:

```text
None or T
```

It stores a raw `Value` and uses `Value::None()` as its absence niche. It is
therefore compact for value-backed payloads:

```cpp
Optional<TValue<String>>
Optional<TValue<Tuple>>
```

The API mirrors `std::optional` where useful:

```cpp
Optional<T>::none()
Optional<T>::some(value)

bool has_value() const;
bool is_none() const;
explicit operator bool() const;

T value() const;      // asserts has_value()
T operator*() const;
Value raw_value() const;
```

`Optional<TValue<None>>` is rejected by design. `None` is already the absence
state, so allowing it as the contained semantic type would make both branches
look identical.

### `Expected<T>`

`Expected<T>` is the VM-level analogue of `std::expected<T, PendingException>`.
The error object itself is not stored in the wrapper; it lives in
`ThreadState` as pending exception state.

```text
Expected<T> = either T, or a pending VM exception
```

Core API:

```cpp
Expected<T>::ok(value)
Expected<T>::raise_exception(type_name, message)
Expected<T>::propagate_exception()

bool has_value() const;
bool has_exception() const;
explicit operator bool() const;

T value() const;      // asserts has_value()
T operator*() const;
```

`value()` is semantic unwrapping, following `std::expected` and
`std::optional`. Low-level access to the backing VM representation is named
`raw_value()`, and only exists for value-backed `Expected<T>` specializations.

There are two storage modes:

- `Expected<T, true>` for value-like payloads that can be reconstructed from a
  raw `Value`. This includes `Value`, `TValue<T>`, and wrappers such as
  `Optional<TValue<T>>`. It stores one word and uses
  `Value::exception_marker()` as the exception niche.
- `Expected<T, false>` for ordinary C++ payloads such as `int32_t`. It stores an
  explicit success flag plus manually managed `T` storage.

Examples:

```cpp
Expected<TValue<String>>
Expected<Optional<TValue<String>>>
Expected<TValue<None>>
Expected<int32_t>
```

`Expected<TValue<None>>` is the "success with no payload" form for VM-semantic
APIs whose successful result is Python `None`. For parser or compiler internals
that naturally return indexes, `Expected<int32_t>` is now supported directly.
If a domain-specific index type would make the meaning clearer, it can still be
introduced later.

### `CL_TRY`

`CL_TRY(expr)` unwraps an `Expected`-like result or propagates the currently
pending exception from the enclosing function:

```cpp
TValue<String> name = CL_TRY(TValue<String>::from_value_checked(value));
```

The enclosing function must return either `Value` or `Expected<T>`. Propagation
uses a small marker object that converts to either `Value::exception_marker()` or
`Expected<T>::propagate_exception()`.

Do not use `CL_TRY` inside interpreter opcode handlers. Interpreter handlers
need the interpreter-specific exception-table dispatch path instead of ordinary
native propagation.

## Conversion Ladder

The API distinguishes predicates, assert-only conversion, unchecked conversion,
and VM-semantic raising conversion.

Native-layout predicates:

```cpp
can_convert_to<T>(value)       // non-raising predicate
try_convert_to<T>(value)       // T * or nullptr
assume_convert_to<T>(value)    // assert-only pointer conversion
```

Typed handle conversion:

```cpp
TValue<T>::from_value_checked(value)
    -> Expected<TValue<T>>

TValue<T>::from_value_or_raise(value, type_name, message)
    -> Expected<TValue<T>>

TValue<T>::from_value_assumed(value)
    -> TValue<T>

TValue<T>::from_value_unchecked(value)
    -> TValue<T>
```

Use the checked forms when the caller receives arbitrary Python data. Use the
assumed form when an earlier branch or invariant has already proved the type.
Use the unchecked form only at representation boundaries where preserving the
proof in the type is the whole point of the surrounding code.

## Traits

`TValueTraits<T>` is intentionally small. It describes only semantic
classification and extraction:

```cpp
static bool is_instance(Value value);
static extract_type extract_unchecked(Value value);
```

For native-layout heap objects, the generic specialization checks exact native
layout:

```cpp
value.is_ptr() &&
value.get_ptr<Object>()->native_layout_id() == T::native_layout
```

Inline semantic types such as `SMI`, `CLInt`, `Bool`, and `None` provide their
own specializations. The trait does not describe ownership or refcounting.

`Exception` is a semantic type for the exception-object family, rather than one
exact native layout. It accepts `ExceptionObject` and its exception subclasses.

## Ownership

`Value` and `TValue<T>` are borrowed handles. Use them for parameters and locals
whose lifetime is managed elsewhere.

`Owned<T>` and `Member<T>` are ownership wrappers over handle-like `T`:

```cpp
Owned<Value>
Owned<TValue<String>>
Owned<Optional<TValue<String>>>

Member<Value>
Member<TValue<String>>
Member<Optional<TValue<String>>>
```

Both store a `T` directly and retain/release `T::raw_value()` where necessary.
`Owned<T>` releases on destruction and is suitable for local RAII ownership.
`Member<T>` is for direct members of Clover heap objects; it retains on
construction and assignment, releases overwritten values, and does not release
on destruction so reclamation can observe the stored member.

`HandleRefcountTraits<T>` is the separate refcount classification trait. It
uses the wrapper's recursive `semantic_type` to elide refcount work for fully
inline states such as `SMI`, `Bool`, and `None`. Extra wrapper states such as
`None` and `exception_marker` are inline sentinels, so this remains correct for
`Optional<T>` and value-backed `Expected<T>`.

`Member<T>::release_ref()` exists only for custom heap-object deallocation
paths. It decrefs the stored reference but leaves the member value unchanged;
the containing object must not use that member afterward. `Owned<T>` does not
have a release operation.

`OwnedHeapPtr<T>` and `MemberHeapPtr<T>` remain separate for VM-internal heap
pointer ownership where no `Value` handle exists or where pointer-nullability is
the intended representation.

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

Expected<TValue<Foo>> Foo::make(Value name, Value args)
{
    TValue<String> typed_name =
        CL_TRY(TValue<String>::from_value_checked(name));
    TValue<Tuple> typed_args =
        CL_TRY(TValue<Tuple>::from_value_checked(args));

    return Expected<TValue<Foo>>::ok(make_object_value<Foo>(
        typed_name, typed_args));
}
```

This avoids half-constructed heap objects and keeps the exception path explicit.

## Parser And Other Non-Value Results

`Expected<T>` is no longer limited to value-backed handles. Parser and compiler
code may return ordinary C++ payloads:

```cpp
Expected<int32_t> expression();
Expected<int32_t> statement();
Expected<int32_t> block();
```

The non-value-backed representation carries a separate `has_value_` flag. That
is an acceptable cost for control-plane results such as AST indexes. Where a
domain-specific type would make invalid states clearer, a named wrapper can be
introduced later without changing the propagation model.

## Current Design Rules

- Prefer `TValue<T>` over `Value` when the semantic type is known.
- Use `Optional<T>` when `None` is a valid absence state.
- Use `Expected<T>` when the operation can set or propagate pending exception
  state.
- Use `Expected<TValue<None>>` for fallible VM operations whose success result
  is Python `None`.
- Use `Expected<int32_t>` or another ordinary C++ payload for non-VM values such
  as parser indexes.
- Use `raw_value()` only at low-level representation, refcounting, storage, and
  boundary sites.
- Keep exception markers transient. Heap object state should almost never store
  `Expected<T>`.
- Keep constructors non-fallible; put validation and pending-exception behavior
  in factories.
