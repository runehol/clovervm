# Bytes Type Bringup Plan

This note defines a first implementation slice for a builtin `bytes` type. The
goal is to unblock stdlib modules that need raw byte strings without quietly
implementing partial protocol machinery in the wrong layer.

## Python-Visible Contract

The first slice should make these operations work:

```python
bytes() == b""
bytes(b"abc") is b"abc"
bytes([65, 66, 67]) == b"ABC"
bytes((0, 255)) == b"\x00\xff"
len(b"abc") == 3
b"abc"[0] == 97
b"abc"[-1] == 99
b"abc"[1:] == b"bc"
b"ab" + b"cd" == b"abcd"
b"b" in b"abc"
98 in b"abc"
b"abc".startswith(b"ab")
b"abc".endswith(b"bc")
b"banana".find(b"na") == 2
repr(bytes([0, 9, 10, 13, 34, 39, 92, 65, 255]))
```

Error behavior to pin down in tests:

```python
bytes([256])      # ValueError: bytes must be in range(0, 256)
bytes([-1])       # ValueError: bytes must be in range(0, 256)
bytes([1.0])      # TypeError, no implicit float conversion
b"abc"[3]         # IndexError
b"abc" + "x"      # TypeError via NotImplemented operator path
```

The constructor should accept exact CloverVM integer values only in this slice:
SMI and `CLInt` values that are already integers. It should not call
`__index__` yet. `__index__` is a shared Python protocol and should be added as
a runtime helper used consistently by `bytes`, slicing, `range`,
`operator.index`, and other index consumers.

## Explicitly Out Of Scope

- `bytearray` and `memoryview`.
- `bytes(str, encoding, errors)` and general encode/decode policy.
- `__index__` on arbitrary objects.
- Bytes `%` formatting, `.hex()`, `.fromhex()`, and the full method surface.
- Path-like protocol integration.
- Native module public API changes beyond small raw-byte accessors needed by
  follow-up stdlib work.

## Object Model

Add a `Bytes` heap object parallel to `String`:

- New `NativeLayoutId::Bytes`.
- `src/builtin_types/bytes.h` and `src/builtin_types/bytes.cpp`.
- Immutable inline storage using `uint8_t data[1]`.
- `Member<TValue<SMI>> count`, matching `String`.
- `size_for(size_t)`, `object_size_in_bytes`, `CL_DECLARE_*` metadata, and
  bootstrap constructors matching the `String` pattern.
- No null terminator requirement for semantics. Allocating one extra byte and
  writing `0` is acceptable only as a C convenience; the byte count is always
  authoritative.

Prefer typed helpers:

```cpp
std::span<const uint8_t> bytes_view(TValue<Bytes> value);
uint64_t bytes_hash(TValue<Bytes> value);
TValue<SMI> bytes_hash_normalized(TValue<Bytes> value);
bool bytes_eq(TValue<Bytes> left, TValue<Bytes> right);
int bytes_compare(TValue<Bytes> left, TValue<Bytes> right);
```

If `std::span` is avoided for a narrow API surface, use `std::string_view` over `char` or a
small local view struct with `const uint8_t *data` and `size_t size`.

## Builtin Class Wiring

Follow the existing `str` class registration path:

- Add `make_bytes_class(VirtualMachine *)`.
- Add `install_bytes_class_methods(VirtualMachine *)`.
- Add `VirtualMachine::bytes_class()` accessor analogous to `str_class()`.
- Register `bytes` in `initialize_builtin_types()` after `str`/`tuple` are safe
  enough for bootstrap metadata, before stdlib bootstrap code can reference it.
- Add `bytes.cpp`/`bytes.h` to `src/CMakeLists.txt`.
- Export `bytes` as a public builtin class.

`bytes.__new__` should be a native intrinsic because it needs to distinguish
raw bytes and exact integer elements. Use defaults so `bytes()` works without
arguments, following `str.__new__`.

## Methods For First Slice

Native methods:

- `__new__(cls, source=b"")`
- `__repr__`
- `__str__` returning `__repr__` result, as CPython displays bytes as `b'...'`
- `__len__`
- `__hash__`
- `__add__`
- `__eq__`, `__ne__`, `__lt__`, `__le__`, `__gt__`, `__ge__`
- `__getitem__` with int index returning an int and slice returning bytes
- `__contains__` accepting either int byte values or bytes subsequences
- `startswith`, `endswith`, `find`, `index`, `count`

Trusted handlers should mirror the `str` handlers where they already exist:

- exact bytes/bytes comparison and equality
- exact bytes/bytes addition
- exact bytes integer/slice getitem
- exact bytes containment
- exact bytes hash

Do not add a helper solely to share two local call sites with `String`. Share
only where it removes real duplicated complexity, such as slice normalization
or hash byte scanning.

## Literal Support

Add `b"..."` and `br"..."` parsing as a separate compiler-facing slice:

- Extend tokenizer prefix validation to accept `b`/`B` combinations that CPython
  accepts for bytes literals.
- Keep the AST as a constant expression if no new AST node is needed.
- Decode bytes escapes to raw bytes, not Unicode code points. Reject non-ASCII
  literal bytes unless they are escaped, matching Python-visible behavior.
- Codegen should load a bytes constant exactly as it loads string constants.

This is parser/compiler-owned syntax work. Runtime `bytes()` support should not
depend on bytes literal support landing in the same patch, though tests become
more readable once literals exist.

## Constructor Details

Recommended first-slice behavior:

- `bytes()` returns an empty bytes object.
- `bytes(existing_bytes)` returns the same immutable object.
- `bytes(list_or_tuple)` iterates elements and accepts exact integer values in
  `[0, 255]`.
- If an element is not an exact integer, raise `TypeError`.
- If an integer is outside `[0, 255]`, raise `ValueError`.

Defer `bytes(n)` producing `n` zero bytes. It is useful, but it overlaps with
integer size conversion and allocation-limit policy. Add it after the iterable
constructor and tests are stable.

## Repr Rules

Implement enough CPython-compatible `repr` for debugging and tests:

- Prefix with `b`.
- Prefer single quotes unless the payload contains a single quote and no double
  quote; matching CPython exactly can be a follow-up if needed.
- Escape `\\`, selected quotes, `\t`, `\n`, `\r`.
- Printable ASCII bytes `0x20..0x7e` render as characters.
- Other bytes render as lowercase `\xhh`.

`value_to_str_string()` and generic object display should treat bytes through
its `__repr__`/`__str__`, not by adding a cross-layer special case.

## Native API Follow-Up

After the object exists, add raw-byte accessors to the native module API only
when a consumer needs them:

- `clover_bytes_as_buffer(ctx, value, const uint8_t **data, size_t *size)` or
  equivalent.
- `clover_make_bytes(ctx, data, size)`.

Keep these APIs explicitly fallible and typed. Do not overload UTF-8 string
helpers for raw bytes.

## Test Plan

Use focused C++ tests for object layout and native helpers:

- `can_convert_to<Bytes>` and `TValue<Bytes>` checked construction.
- object size and value span metadata.
- hash/equality helper behavior.
- GC/refcount scanning treats only `count` as a member and not raw data.

Use interpreter/Python tests for visible behavior:

- constructor cases and error types.
- literal decoding, raw prefixes, invalid bytes literals.
- indexing, negative indexing, slicing.
- concatenation and comparison.
- containment with int and bytes needles.
- `repr` escape cases.
- method receiver checks and unsupported mixed-type operations.

Use codegen/parser tests only for structural guarantees around bytes literals
as constants.

## Stdlib Follow-Ups

Once the first slice lands, revisit:

- `fnmatch` bytes names and patterns.
- `glob` bytes paths.
- `os` bytes path conversion.
- `base64`, `struct`, `hashlib`, `hmac`, and compression modules.
