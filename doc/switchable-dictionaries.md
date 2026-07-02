# Switchable Dictionaries

This document sketches the staged path from CloverVM's current exact-string
`dict` to a future Python-compatible general dictionary.

The motivating case is `errno.errorcode`: CPython exposes it as a real `dict`
keyed by integer errno values. CloverVM's current `dict` implementation is
string-key-only, which keeps namespace-style maps simple but blocks compatible
stdlib modules.

The first implementation step is deliberately not CPython-compatible. It adds a
separate internal Python class, backed by a separate C++ `GeneralDict` native
layout, so the general-key table semantics can be built and tested without
forcing automatic switching or a unified C++ `dict` interface immediately.

## Goals

- Preserve one public Python `dict` class.
- Bootstrap general-key dictionary mechanics in a separate internal class before
  unifying them with public `dict`.
- Keep dict literal construction on the existing string-only `dict` path during
  the bootstrap phase.
- Keep exact-string dictionaries fast and non-reentrant.
- Promote dictionaries to a general shape when non-string keys are inserted.
- Eventually route public general-dictionary hashing and equality through
  bytecode-visible calls with normal inline caches.
- For the future public-dict hot path, avoid C++ helpers that invoke
  `__hash__`, `__eq__`, descriptors, or other Python-visible behavior behind the
  interpreter's back.

## Non-Goals

- Do not add a separate public string-dict class.
- Do not make a non-dict substitute for APIs such as `errno.errorcode`.
- Do not broaden the string-only implementation with ad hoc builtin-key cases.
- Do not call arbitrary Python from raw table helpers that are intended to be
  reused by the future public-dict hot path.
- Do not use the bootstrap `GeneralDict` class to claim CPython compatibility.
- Do not make dict literals choose between `Dict` and `GeneralDict` during the
  bootstrap phase.

## Bootstrap GeneralDict Class

The initial general-key implementation should be an internal Python-visible
class named `__clover_general_dict`, backed by a new C++ class:

```cpp
class GeneralDict : public Object
{
public:
    static constexpr NativeLayoutId native_layout = NativeLayoutId::GeneralDict;

private:
    class Entry
    {
    public:
        Value key;
        Value value;
        TValue<SMI> hash;
    };

    RawArray<int32_t> hash_table;
    ValueArray<Entry> entries;
    size_t n_valid_entries;
};
```

This class should live in `dict.h` and `dict.cpp` next to the existing `Dict`
implementation. Do not introduce a shared base class, template table layer, or
separate factoring pass at this stage. Duplication is acceptable as long as the
data members stay visibly parallel to `Dict`: same hash table representation,
same entry-array shape, same stored canonical SMI hash, and same live-entry
count. That keeps later unification mechanical without making the bootstrap
step pay for a premature abstraction.

The existing `Dict` remains the class constructed by `{}` and dict displays.
Literal construction should not choose between `Dict` and `GeneralDict` yet.
`GeneralDict` is only for controlled internal construction and tests while the
semantic machinery is brought up.

The narrow bootstrap target for `GeneralDict` is:

- arbitrary `Value` keys
- `__len__`
- `__getitem__`
- `__setitem__`
- `__delitem__`
- `__contains__`
- enough `__repr__` support for debugging if it is cheap

Views, iterators, `copy`, `update`, `fromkeys`, automatic promotion, literal
lowering, and CPython-compatible `dict` exposure are later work.

`GeneralDict` operations may call Python through `ThreadState::hash_value` and
`ThreadState::test_equal`. The table mechanics themselves should remain in C++
for this bootstrap class. This is intentionally different from the future hot
public-dict path, where the general dict method bodies should contain
cache-bearing bytecode call sites.

Because these helpers may re-enter Python, `GeneralDict` methods must keep
arguments and candidate entries alive across calls. In practice, `__setitem__`
should retain `key` and `value` with `Owned<Value>` before calling
`hash_value`; probing paths should retain any candidate key before calling
`test_equal`.

## Shape Invariants

The final public-dict design should still use the existing shape system rather
than a second public class.

Every Python-visible object already has a `Shape`. Dictionary mode should be
represented through that existing mechanism.

The public class remains `dict`, but dict instances use different shapes:

- **String dict shape**: all live keys are exact `str` objects. Lookup,
  insertion, deletion, and membership may use trusted native string hashing and
  equality. These operations must not run Python bytecode.
- **General dict shape**: at least one non-string key has been inserted, or the
  dict has otherwise been promoted. Lookup, insertion, deletion, and membership
  must use the general dictionary path. The general path may run `__hash__`,
  equality, descriptors, and arbitrary Python bytecode.

The shape carries the contents invariant. Checking only the incoming key is not
enough: a lookup by string in a dictionary that already contains a custom key
can collide with that custom key and require Python-visible equality.

Promotion is one-way. Deleting the non-string key does not demote the dict back
to string shape. One-way promotion keeps cache invalidation and iterator
behavior predictable.

## Dispatch Model

This section describes the later public-dict unification target, not the initial
bootstrap `GeneralDict` class.

`dict.__getitem__`, `dict.__setitem__`, `dict.__delitem__`, and related methods
should dispatch by receiver shape:

- string dict shape plus exact string key: trusted handler
- string dict shape plus non-string key:
  - lookup/membership: promote or enter the general path, depending on the
    final implementation choice for miss semantics
  - insertion/update: promote before inserting
- general dict shape: bytecode-backed general dict method

The important split is where inline caches live:

- The outer `GetItem`/`SetItem`/`DelItem` operator inline cache chooses the dict
  method or trusted handler.
- The general dict method's bytecode contains the `__hash__` and equality call
  sites, so those calls get ordinary inline caches.

## Trusted Dict Opcodes

Trusted dict opcodes are a candidate for the later hot public-dict path. The
bootstrap `GeneralDict` class should keep table mechanics in C++ and use
`ThreadState::hash_value` / `ThreadState::test_equal` for protocol calls.

The general bytecode method needs low-level trusted opcodes for table work.
These opcodes may inspect and mutate dict storage, but must not perform
Python-visible protocol calls.

These operations should not be exposed as Python-visible methods, normal native
functions, or importable private helpers. They are too specific to dictionary
table invariants and too easy to misuse in ways that corrupt a dict. They
should be emitted only by trusted compiler paths for built-in dict method
bodies, and rejected in ordinary Python source.

Needed opcode groups, using function-like placeholder names only to describe
operands and results:

- shape and storage:
  - `_dict_is_string_shape(d)`
  - `_dict_promote_to_general(d)`
  - `_dict_probe_table_generation(d)`
- hash normalization:
  - `CanonicalizeHash(value)`
- probing:
  - `_dict_probe_start(d, hash_value)`
  - `_dict_probe_step(d, probe)`
  - `_dict_probe_advance(probe)`
  - `_dict_probe_record_tombstone(probe, entry_status)`
  - `_dict_entry_still_matches(d, table_generation, entry, candidate)`
- entry access:
  - `_dict_entry_hash(d, entry)`
  - `_dict_entry_key(d, entry)`
  - `_dict_entry_value(d, entry)`
- mutation:
  - `_dict_probe_write_new(d, probe, hash_value, key, value)`
  - `_dict_entry_write_existing(d, entry, value)`
  - `_dict_entry_delete(d, entry)`
  - `_dict_resize_general_if_needed(d)`

The exact opcode names are placeholders. The contract matters more than the
names: all Python-visible calls happen in bytecode, and all raw table
manipulation happens in dedicated trusted dict opcodes.

The current bootstrap `GeneralDict` C++ methods intentionally mix the protocol
driver and raw table work: they call `ThreadState::hash_value` /
`ThreadState::test_equal`, then probe or mutate storage directly. Before public
dict unification, that implementation should be factored into two layers:

- **Temporary semantic drivers**: C++ wrappers for `GeneralDict` bootstrap
  methods. These may call `ThreadState::hash_value` and
  `ThreadState::test_equal` while the internal class is still C++-backed. They
  exist only to keep the bootstrap class functional until trusted bytecode
  method bodies take over.
- **Raw table helpers**: protocol-free operations that inspect or mutate table
  storage. These helpers must not call `hash`, equality, descriptors,
  `ThreadState::test_equal`, `ThreadState::hash_value`, or any operation that
  can run Python. They should operate on stored SMI hashes, probe state, entry
  indexes, table generations, keys, and values.

The temporary semantic drivers should be written as direct mirrors of the later
trusted bytecode sketches: compute/call `hash`, start a raw probe, inspect a raw
candidate, call equality at the driver layer, revalidate the entry, then call a
raw write/delete/read helper. That makes the later migration mechanical: replace
the C++ driver with trusted bytecode containing cache-bearing `hash` and
equality sites, while keeping the raw table operations equivalent.

Table generation should mean probe-structure generation, not "any dictionary
mutation happened." A `table_generation` field is the preferred representation:
increment it when an operation can make an existing probe or entry index refer
to a different table structure, such as grow/rehash, clear, compaction,
representation promotion, or replacing the table backing. Do not increment it
for value overwrite, insertion into an existing empty/tombstone slot without
resize, or deletion that only invalidates the current entry and writes a
tombstone.

After equality returns, the driver should accept the equality result only if the
same probe-structure generation is still current and the same candidate key is
still present at the same entry:

```cpp
bool entry_still_matches(Dict *dict, size_t table_generation, int32_t entry,
                         Value candidate)
{
    return dict->table_generation() == table_generation &&
           entry >= 0 &&
           static_cast<size_t>(entry) < dict->entry_storage_size() &&
           dict->entry_at(entry).valid() &&
           dict->entry_at(entry).key == candidate;
}
```

The exact C++ spelling can differ. The important contract is that broad
mutation versions should not be used for this revalidation. A version that
increments for every insert/delete/value overwrite is safe but too coarse: an
unrelated mutation during equality could force unnecessary or repeated restarts
even when the candidate entry remained valid.

Do not add helpers such as `find_entry_with_python_equality` or
`lookup_with_thread_state` as the long-term interface. Any helper that takes a
`ThreadState *` in order to run equality is a semantic driver, not a raw table
primitive, and should not be the operation that public general-dict bytecode
uses for hot lookup.

The factoring target for the temporary C++ drivers should look like the
following pseudocode. These sketches are still written as Python-like code, but
the boundary is the same for the intermediate C++ refactor: calls to `hash` and
equality live in the driver, while every `_dict_*` operation is a raw helper
that cannot run Python.

```python
def _general_dict_getitem_driver(self, key):
    hash_value = hash(key)
    probe = _dict_probe_start(self, hash_value)

    while True:
        entry = _dict_probe_step(self, probe)
        if entry == DICT_PROBE_EMPTY:
            raise KeyError

        if entry >= 0 and _dict_entry_hash(self, entry) == hash_value:
            candidate = _dict_entry_key(self, entry)
            if candidate is key:
                return _dict_entry_value(self, entry)

            table_generation = _dict_probe_table_generation(self)
            equal = candidate == key
            if not _dict_entry_still_matches(
                self, table_generation, entry, candidate
            ):
                probe = _dict_probe_start(self, hash_value)
                continue

            if equal:
                return _dict_entry_value(self, entry)

        _dict_probe_advance(probe)
```

```python
def _general_dict_setitem_driver(self, key, value):
    hash_value = hash(key)
    _dict_resize_general_if_needed(self)
    probe = _dict_probe_start(self, hash_value)

    while True:
        entry = _dict_probe_step(self, probe)

        if entry == DICT_PROBE_EMPTY:
            _dict_probe_write_new(self, probe, hash_value, key, value)
            return None

        if entry == DICT_PROBE_TOMBSTONE:
            _dict_probe_record_tombstone(probe, entry)
            _dict_probe_advance(probe)
            continue

        if _dict_entry_hash(self, entry) == hash_value:
            candidate = _dict_entry_key(self, entry)
            if candidate is key:
                _dict_entry_write_existing(self, entry, value)
                return None

            table_generation = _dict_probe_table_generation(self)
            equal = candidate == key
            if not _dict_entry_still_matches(
                self, table_generation, entry, candidate
            ):
                _dict_resize_general_if_needed(self)
                probe = _dict_probe_start(self, hash_value)
                continue

            if equal:
                _dict_entry_write_existing(self, entry, value)
                return None

        _dict_probe_advance(probe)
```

Probe results should be integer-shaped, not strings or public status objects.
One convenient contract is:

- `_dict_probe_step` returns `entry >= 0` for an occupied entry index
- `DICT_PROBE_EMPTY == -1`
- `DICT_PROBE_TOMBSTONE == -2`

This matches the current table representation style and keeps trusted bytecode
branches cheap. Named constants in trusted source can carry readability; the
runtime value should remain a small integer.

Dictionary entry hashes are stored as SMI values. Public `hash(key)` must return
that same canonical SMI hash value. General dict bytecode should call public
`hash(key)` and use the result directly; it should not wrap that call in a
second canonicalization step.

`CanonicalizeHash` is the trusted intrinsic/opcode used by built-in bytecode
that implements the public hash protocol, by bytecode-backed general dict
methods that inline the hash protocol for their own inline caches, and by exact
built-in hash helpers. It is not dictionary-specific policy: it accepts `bool`,
SMI, and BigInt integer results, does not call Python, and returns a normalized
SMI hash or raises if the value is not an integer.

`CanonicalizeHash` should preserve exact small integer hashes before applying any
modular reduction:

- `False` normalizes to `0` and `True` normalizes to `1`.
- SMI values normalize to themselves.
- BigInts that fit in the SMI range normalize to the same value as the
  equivalent SMI. This keeps future noncanonical small heap integers from
  violating the equal-objects-have-equal-hash invariant.
- BigInts outside the SMI range reduce through CloverVM's integer hash modulus
  and return a signed SMI result.
- A normalized result of `-1` is remapped to `-2`.

CPython uses a `Py_hash_t`-sized integer hash modulus that is larger than
CloverVM's SMI range on 64-bit builds. CloverVM is not forced to copy that
modulus for ordinary Python semantics. The VM should choose a hash modulus whose
reduced results fit in SMI storage; a Mersenne value such as `value_smi_max`
keeps modular reduction simple and keeps every reduced result representable.
Exact SMI preservation handles the boundary value where a direct SMI hash is
outside the reduced-result range.

CPython remaps a returned hash value of `-1` to `-2` because `-1` is a C API
error sentinel for `Py_hash_t`. CloverVM does not need that remap for internal
dict correctness because failures travel through pending exception state and
hash values are distinct from probe sentinels, but adopting the remap as part
of `CanonicalizeHash` keeps the public `hash()` result, dict storage, and any
future C API compatibility path aligned.

Every exact built-in hash helper that can feed dict storage or public `hash()`
must use the same normalization policy. In particular, the string-shaped dict
fast path may call a trusted string hash helper directly, but that helper must
return a normalized SMI hash, including `-1` to `-2` remapping. Raw string
mixing helpers, if kept, should stay private and should not be callable from
dict insertion, lookup, or public `hash()` without immediately applying
`CanonicalizeHash`.

The opcodes should avoid returning raw entry pointers or references that can
survive a Python-visible call. Return entry indexes, probe indexes, or compact
probe-state objects instead, and require revalidation after any call that can
mutate the dictionary.

The revalidation opcode should follow CPython's local defense rather than a
coarse "any dict version changed" rule. CPython's `dictobject.c` holds the
candidate key alive across `PyObject_RichCompareBool`, then checks whether the
same keys table and same entry key are still present. If not, lookup reports a
key-changed sentinel and `_Py_dict_lookup` restarts from the current table.
CloverVM should preserve the same shape of defense: after equality returns,
the entry index is reusable only if the same table generation and same candidate
key are still present at that entry.

## General Dict Bytecode Sketches

The sketches below are written in Python-like pseudocode. They are not intended
as public Python source; they describe trusted Clover bytecode bodies with
ordinary call sites and inline caches.

### Hashing

```python
def _general_dict_hash_key(key):
    return hash(key)
```

This assumes public `hash()` is implemented as the shared Python-visible hash
protocol. Missing or disabled `__hash__` should fail through that shared path;
non-integer and out-of-SMI integer hash results should be handled by public
`hash()` through `CanonicalizeHash`. The dict should not have a parallel hash
protocol or re-canonicalize a public hash result.

Implementation can inline the public hash protocol inside the trusted dict
method body instead of calling the public `hash` builtin directly. That inlined
sequence should have its own cache-bearing special-method lookup and call sites,
then finish with `CanonicalizeHash`. The semantic contract is still exactly
`hash(key)`: same canonical SMI result, same disabled-hash behavior, same
non-integer result error, and same exception propagation.

### Lookup

```python
def _general_dict_getitem(self, key):
    hash_value = _general_dict_hash_key(key)
    probe = _dict_probe_start(self, hash_value)

    while True:
        entry = _dict_probe_step(self, probe)
        if entry == DICT_PROBE_EMPTY:
            raise KeyError

        if entry >= 0:
            if _dict_entry_hash(self, entry) == hash_value:
                candidate = _dict_entry_key(self, entry)
                if candidate is key:
                    return _dict_entry_value(self, entry)

                table_generation = _dict_probe_table_generation(self)
                equal = candidate == key
                if not _dict_entry_still_matches(
                    self, table_generation, entry, candidate
                ):
                    probe = _dict_probe_start(self, hash_value)
                    continue

                if equal:
                    return _dict_entry_value(self, entry)

        _dict_probe_advance(probe)
```

The revalidation after equality is required. Equality may mutate the same
dictionary, clear it, resize it, delete the candidate key, or insert colliding
keys. The method must not assume that the candidate entry or probe state is
still valid after `candidate == key` returns.

### Membership

```python
def _general_dict_contains(self, key):
    try:
        _general_dict_getitem(self, key)
        return True
    except KeyError:
        return False
```

This can be optimized later with a lookup helper that returns a found/missing
status without materializing the value path. Semantically it has the same
hashing, equality, exception, and mutation behavior as lookup.

### Assignment

```python
def _general_dict_setitem(self, key, value):
    hash_value = _general_dict_hash_key(key)
    _dict_resize_general_if_needed(self)
    probe = _dict_probe_start(self, hash_value)

    while True:
        entry = _dict_probe_step(self, probe)

        if entry == DICT_PROBE_EMPTY:
            _dict_probe_write_new(self, probe, hash_value, key, value)
            return None

        if entry == DICT_PROBE_TOMBSTONE:
            _dict_probe_record_tombstone(probe, entry)
            _dict_probe_advance(probe)
            continue

        if _dict_entry_hash(self, entry) == hash_value:
            candidate = _dict_entry_key(self, entry)
            if candidate is key:
                _dict_entry_write_existing(self, entry, value)
                return None

            table_generation = _dict_probe_table_generation(self)
            equal = candidate == key
            if not _dict_entry_still_matches(
                self, table_generation, entry, candidate
            ):
                _dict_resize_general_if_needed(self)
                probe = _dict_probe_start(self, hash_value)
                continue

            if equal:
                _dict_entry_write_existing(self, entry, value)
                return None

        _dict_probe_advance(probe)
```

The tombstone handling can be represented either inside the probe state or in a
separate local. The bytecode must preserve insertion order: overwriting an
existing key does not move it, while inserting a new key appends one live entry.

### Deletion

```python
def _general_dict_delitem(self, key):
    hash_value = _general_dict_hash_key(key)
    probe = _dict_probe_start(self, hash_value)

    while True:
        entry = _dict_probe_step(self, probe)
        if entry == DICT_PROBE_EMPTY:
            raise KeyError

        if entry >= 0:
            if _dict_entry_hash(self, entry) == hash_value:
                candidate = _dict_entry_key(self, entry)
                if candidate is key:
                    _dict_entry_delete(self, entry)
                    return None

                table_generation = _dict_probe_table_generation(self)
                equal = candidate == key
                if not _dict_entry_still_matches(
                    self, table_generation, entry, candidate
                ):
                    probe = _dict_probe_start(self, hash_value)
                    continue

                if equal:
                    _dict_entry_delete(self, entry)
                    return None

        _dict_probe_advance(probe)
```

### Public Method Wrappers

Most public methods should be thin bytecode wrappers around the core lookup,
assignment, and deletion paths so they share the same hash/equality cache
behavior.

```python
def _general_dict_get(self, key, default=None):
    try:
        return _general_dict_getitem(self, key)
    except KeyError:
        return default


def _general_dict_setdefault(self, key, default=None):
    try:
        return _general_dict_getitem(self, key)
    except KeyError:
        _general_dict_setitem(self, key, default)
        return default


def _general_dict_pop(self, key, default=_missing):
    try:
        value = _general_dict_getitem(self, key)
    except KeyError:
        if default is _missing:
            raise
        return default
    _general_dict_delitem(self, key)
    return value


def _general_dict_update_from_dict(self, other):
    for item in other.items():
        _general_dict_setitem(self, item[0], item[1])
    return None


def _general_dict_update(self, other=None):
    if other is None:
        return None
    _general_dict_update_from_dict(self, other)
    return None
```

`update` is intentionally incomplete as a sketch. Full CPython behavior also
accepts iterables of pairs and keyword arguments. Those can be added when the
iterator and keyword-call surfaces are ready enough to support them honestly.

Methods that do not perform key lookup can stay mostly opcode-backed, but
they must work for either dict shape:

```python
def _general_dict_clear(self):
    return _dict_clear_all(self)


def _general_dict_copy(self):
    result = _dict_new_with_same_shape(self)
    _dict_copy_entries(result, self)
    return result


def _general_dict_keys(self):
    return _dict_keys_view(self)


def _general_dict_values(self):
    return _dict_values_view(self)


def _general_dict_items(self):
    return _dict_items_view(self)


def _general_dict_popitem(self):
    if _dict_len(self) == 0:
        raise KeyError
    return _dict_delete_last_live_entry(self)
```

`dict.fromkeys` should create a new dict and assign through the same public
setitem path, so non-string keys promote the result naturally:

```python
def _dict_fromkeys(cls, keys, value=None):
    result = dict()
    for key in keys:
        result[key] = value
    return result
```

## Promotion Sketch

Insertion into a string-shaped dict has a clear branch:

```python
def _dict_setitem(self, key, value):
    if _dict_is_string_shape(self):
        if _is_exact_str(key):
            return _string_dict_setitem_trusted(self, key, value)
        _dict_promote_to_general(self)
    return _general_dict_setitem(self, key, value)
```

Lookup by non-string in a string-shaped dict is semantically allowed to miss
without promotion only if no Python-visible equality can run. Because the shape
invariant proves all live keys are exact strings, a non-string lookup cannot
need custom-key equality from an existing key. However, once the key itself
requires `__hash__`, the public behavior of `d[key]` may still need to call
`key.__hash__` before concluding the key is absent. The conservative initial
design should route non-string lookup through general bytecode, promoting if
needed, rather than inventing a special miss path.

## Reentrancy and Probe Revalidation

General dictionary operations must assume that `__hash__` and equality can:

- raise
- mutate the same dictionary
- mutate candidate key objects
- insert or delete colliding keys
- re-enter dictionary operations

Therefore:

- do not hold raw table pointers across Python-visible calls
- hold or otherwise keep the candidate key alive across equality
- record the current table generation before calling equality
- after equality returns, revalidate that the probe still refers to the same
  table and same candidate key
- restart the probe if that table/key revalidation fails
- keep opcode effects small enough that a restart leaves the dictionary
  in a valid state

CPython uses this local table/key revalidation pattern in `dictobject.c` rather
than relying only on a broad public dictionary version tag. Its generic compare
helper keeps the entry key alive, calls rich comparison, then checks that the
keys table and entry key are unchanged. If they changed, lookup returns a
sentinel and restarts from the current table. CloverVM can implement the same
idea with a compact table generation plus a probe-entry key check.

## Interaction With Existing Caches

The existing operator inline cache can continue to guard the outer subscription
operation by receiver and key shapes. It should choose:

- a trusted string-dict handler when receiver shape and key shape prove the
  fast path
- a bytecode-backed dict method for general shape or uncacheable cases

The hash and equality caches belong inside the bytecode-backed method. That
keeps their call sites explicit and inspectable for the interpreter and future
JIT.

## C++ Dict Interface Boundary

The current C++ `Dict` interface is a design hazard because methods named
`get_item`, `set_item`, `contains`, and `del_item` accept arbitrary `Value`
keys while the table implementation is actually exact-string-only. That makes
it easy for native VM code to bypass Python-visible method guards and call raw
string hashing/equality on non-string keys. Dict literal construction already
has this shape: evaluated Python keys are passed directly to `Dict::set_item`.

The C++ API should make the operation's semantic level explicit. Raw
string-shape operations are not a public C++ API category. They are private
implementation details of `Dict` and trusted handlers, guarded by receiver
shape and exact `str` key checks before use. Exposing them as a separate API
would only move the current string-only hazard behind a different name.

The useful public/internal split is:

- **Exact-string semantic helpers**: C++ helpers that accept
  `TValue<String>` keys but still dispatch by dict shape. These are useful for
  callers that can prove the lookup key is a string, but they are not
  universally non-fallible. If the receiver has already promoted to general
  shape, probing can encounter non-string stored keys and equality may run
  Python.
- **General semantic helpers**: C++ helpers that accept arbitrary
  `Value` keys and implement ordinary Python dict semantics, including
  `hash(key)`, equality, pending-exception propagation, promotion, and
  stale-entry revalidation.

This means that fallibility is not limited to "general key" APIs. A string key
lookup into a general-shaped dict can still require Python-visible equality
against a non-string candidate. The C++ signature should reflect that both
exact-string semantic operations and general semantic operations can fail.

A possible naming shape is:

```cpp
// Semantic operations. May run Python, may promote, may raise.
[[nodiscard]] Expected<Value> get_item(ThreadState *thread, Value key);
[[nodiscard]] Expected<void> set_item(ThreadState *thread, Value key,
                                      Value value);
[[nodiscard]] Expected<void> del_item(ThreadState *thread, Value key);
[[nodiscard]] Expected<bool> contains(ThreadState *thread, Value key);

// Same semantics, but the incoming lookup key is known exact str.
[[nodiscard]] Expected<Value> get_string_item(ThreadState *thread,
                                              TValue<String> key);
[[nodiscard]] Expected<void> set_string_item(ThreadState *thread,
                                             TValue<String> key, Value value);
[[nodiscard]] Expected<void> del_string_item(ThreadState *thread,
                                             TValue<String> key);
[[nodiscard]] Expected<bool> contains_string(ThreadState *thread,
                                             TValue<String> key);
```

The exact spelling is unsettled. The required property is that no public C++
method with a generic name silently means "string-only". Either the method is
semantic and fallible, or it is a private/raw helper that cannot be reached as a
general dict interface.

These are internal C++ APIs, so pending-exception propagation should be explicit
in the return type. Native-method and interpreter opcode wrappers can translate
`Expected<void>` success into `Value::None()` and translate propagated failures
into `Value::exception_marker()` at the boundary. The core dict implementation
should not use in-band `Value` exception markers as its primary C++ contract.

Semantic dict helpers need two reusable `ThreadState` protocol operations rather
than direct dunder calls:

```cpp
// Equivalent to Python hash(value), including special-method lookup,
// integer-result validation, canonical SMI normalization, and -1 remapping.
[[nodiscard]] Expected<TValue<SMI>> hash_value(Value value);

// Equivalent to truth-testing Python left == right. This must use ordinary
// equality operator semantics, including reflected dispatch and NotImplemented
// fallback, not a direct __eq__ method call.
[[nodiscard]] Expected<bool> test_equal(Value left, Value right);
```

`hash_value` should not call the public `hash` builtin through global lookup.
It should be generated as a small cached helper code object that inlines the body
of CloverVM's current `hash` implementation:

```text
Load value
CallSpecialMethod "__hash__", TypeError, "object is unhashable"
CanonicalizeHash
Return
```

`test_equal` should also be generated as a small cached helper code object. It
must use the ordinary equality operator opcode and then explicitly apply the
VM's current truthiness conversion:

```text
Load right
TestEqual left, operator_ic[0]
ToBool
Return
```

This is intentionally not a direct `__eq__` call and not a call to the Python
`bool` builtin. It uses the same equality operator dispatch and `ToBool` opcode
behavior that normal bytecode uses today.

The hot general-dict bytecode path should still contain its own cache-bearing
hash and equality call sites. These `ThreadState` helpers are the C++ semantic
bridge for native/internal callers and future C API wrappers; they are not a
reason to move hot general dict lookup into opaque C++ code.

When public `dict` unification resumes, existing native VM uses should be
audited into these buckets:

- Import caches, `sys.modules`, module globals, and bootstrap construction use
  exact interned string keys. They should move to exact-string helpers and
  should not accidentally become semantic Python dict operations.
- Python-visible dict methods, `dict.fromkeys`, `update`, `setdefault`, `pop`,
  and subscription operators must route through semantic helpers once they can
  observe general-key public dicts.
- Trusted string fast paths may call private raw string-shape helpers only after
  guards prove both receiver shape and key shape.

`CreateDict` needs special attention later. Bootstrap `GeneralDict` does not
change dict-display lowering: `{}` and dict displays continue to construct the
existing string-only `Dict`. Once public dicts support general keys, insertion
can call `hash(key)`, which can execute Python. Dict display semantics require
each key/value pair to be evaluated and inserted before the next pair's
expressions are evaluated. A bulk `CreateDict` opcode that evaluates all
expressions first and inserts afterward is not a good general insertion
boundary. It can remain only for empty dicts or deliberately proven exact-string
construction; ordinary general-key dict displays should lower to create-empty
plus per-entry semantic insertion once public general keys are supported.

## Open Questions

Bootstrap questions:

- Should `GeneralDict.__repr__` be implemented in the first functional slice, or
  deferred until core lookup/assignment is stable?
- What minimal iteration or `items` surface is needed for tests without dragging
  in full dict view semantics?

Later public-dict unification questions:

- Should non-string lookup in a string-shaped dict promote immediately, or
  should there be a bytecode miss path that calls `__hash__` but proves no
  equality with existing keys is needed?
- Should string-to-general promotion reuse the current `Dict` table in place,
  or allocate a new general table and reinsert entries?
- How should active dict iterators respond to promotion and mutation?
- Where should the built-in bytecode method bodies live so they are available
  during bootstrap while still being the only code allowed to emit trusted dict
  opcodes?
- What exact C++ and C API surfaces should be provided for semantic dict
  operations and exact-string fast-path operations?
- What table generation mechanism should stale-entry defense use
  once public general dict lookup needs CPython-style revalidation?
- Should exact-string semantic helpers be separate overloads of the general
  helpers, or separate names, given that they can still be fallible in
  general-shaped dicts?
- Should `CreateDict` be split into an exact-string bulk opcode and a semantic
  per-entry insertion lowering, or should the existing opcode be narrowed to
  empty dict creation only?

## Milestones

Each milestone should preserve a working tree where existing string-key dicts
continue to work. The first track is intentionally a temporary internal
parallel class. It is a staging tool for the hard general-key semantics, not a
CPython-compatible public `dict` implementation.

### 0. Internal GeneralDict Skeleton

- [x] Add `NativeLayoutId::GeneralDict`.
- [x] Add `GeneralDict` to `dict.h` and `dict.cpp`.
- [x] Register a separate internal Python class named `__clover_general_dict`,
  backed by `GeneralDict`.
- [x] Give `GeneralDict` data members that mirror `Dict`: same hash-table array,
  same entry-array shape, same stored SMI hash, and same live-entry count.
- [x] Add construction and `len` tests for the internal class.

Stage invariants:

- No shared base class, shared table abstraction, template layer, or separate
  files are introduced for this stage.
- `{}` and dict-display lowering stay on the existing `Dict` path.
- Ordinary `dict` behavior does not change.

This milestone should make the internal class real without changing ordinary
`dict` behavior. During bootstrap, `__clover_general_dict` is temporarily
exported through the `builtins` module so Python-level tests can construct it.
That exposure should be removed or replaced once there is a better internal
construction/testing path.

### 1. ThreadState Protocol Helpers

- [x] Implement `ThreadState::hash_value(Value) -> Expected<TValue<SMI>>` as the
  semantic equivalent of `hash(value)`, including special-method lookup,
  integer-result validation, canonical SMI normalization, and `-1` remapping,
  by calling a cached helper code object that emits `CallSpecialMethod` followed
  by `CanonicalizeHash`.
- [x] Implement `ThreadState::test_equal(Value, Value) -> Expected<bool>` as the
  semantic equivalent of truth-testing `left == right`, by calling a cached
  helper code object that emits `TestEqual` followed by `ToBool`.
- [x] Add tests for missing and disabled hash, non-integer hash results,
  exceptions from `__hash__`, non-bool equality results, and equality exceptions.

Stage invariants:

- `test_equal` uses ordinary equality operator semantics, including reflected
  dispatch and `NotImplemented` fallback, not a direct `__eq__` call.
- `test_equal` matches the current behavior of Python `==` followed by the VM's
  current truthiness conversion, including raising for unsupported object
  truthiness.
- These helpers are allowed to re-enter Python.
- These helpers are the C++ semantic bridge for the bootstrap `GeneralDict`; they
  are not the final hot-path strategy for public `dict`.

`GeneralDict` lookup, assignment, deletion, and membership all depend on these
helpers. Do not implement general-key table operations first and backfill the
protocol calls later.

### 2. Basic GeneralDict Insertion

- [x] Implement `GeneralDict::__setitem__` for inserting new keys and
  overwriting existing keys.
- [x] Add tests for integer keys, bool/int key collisions, overwrites preserving
  insertion order, `__hash__` exceptions, and `__eq__` exceptions.

Stage invariants:

- Insertion uses `ThreadState::hash_value` before probing.
- Probing uses stored canonical SMI hashes.
- Identity match is an immediate hit.
- After hash match and identity miss, insertion calls `ThreadState::test_equal`.
- `key` and `value` are retained with `Owned<Value>` before calling
  `ThreadState::hash_value`.
- Candidate keys are retained with `Owned<Value>` before calling
  `ThreadState::test_equal`.
- Overwriting an existing key preserves insertion order.

Insertion has to come before lookup so tests can populate the table through the
same semantic path that later lookups will observe.

### 3. GeneralDict Lookup And Membership

- [x] Implement `GeneralDict::__getitem__` and `__contains__` using
  `ThreadState::hash_value` for the lookup key.
- [x] Add tests for present keys, missing keys, integer keys, bool/int
  collisions, hash exceptions, equality exceptions, and equality mutation that
  can be expressed with insertion alone.

Stage invariants:

- Lookup and membership probe using stored canonical SMI hashes.
- Identity match is an immediate hit.
- After hash match and identity miss, lookup and membership call
  `ThreadState::test_equal`.
- The lookup key is retained before calling `ThreadState::hash_value`.
- Candidate keys are retained with `Owned<Value>` before calling
  `ThreadState::test_equal`.

Lookup is the first slice that exercises equality during probing. The table can
now be populated through `__setitem__`, but CPython-style table generation
revalidation and mutation adversaries that require deletion or clearing are
parked for a later unification/stale-entry-defense pass.

### 4. GeneralDict Deletion And Mutation Adversaries

- [x] Implement `GeneralDict::__delitem__`.
- [x] Add tombstone handling and resize stress tests.
- [x] Add tests for deletion misses and deletion after equality mutation.

Stage invariants:

- Probing stays open-coded in `GeneralDict`; do not factor it into callback-based
  shared table helpers while equality may re-enter Python.
- Full CPython-style stale-entry adversaries where `__eq__` clears, resizes,
  deletes from, and reinserts into the same table stay parked for the later
  stale-entry-defense pass.

After this milestone, the internal class can represent integer-key dictionaries
for controlled VM tests and experiments. It still should not be treated as a
CPython-compatible `dict`.

### 5. Add GeneralDict Probe-Structure Generation

- [ ] Add a `GeneralDict` table generation or equivalent probe-structure
  generation.
- [ ] Increment the generation when grow/rehash, clear, compaction, promotion, or
  table-backing replacement can make existing probe state or entry indexes stale.
- [ ] Do not increment the generation for value overwrite, insertion without
  resize into an empty/tombstone slot, or deletion that only invalidates an entry
  and writes a tombstone.
- [ ] Update existing reentrant equality tests so lookup, assignment,
  membership, and deletion revalidate against the probe-structure generation and
  same candidate key.

Stage invariants:

- This is not a broad dictionary mutation version.
- Revalidation after equality checks both probe-structure generation and same
  entry/same candidate key.
- Public `dict` behavior still does not change.

This milestone should give the raw-helper factoring a stable table-generation
primitive before the table helpers are split away from the semantic drivers.

### 6. Factor GeneralDict Into Semantic Drivers And Raw Table Helpers

- [ ] Refactor `GeneralDict` C++ lookup, assignment, deletion, and membership
  into temporary semantic drivers layered over protocol-free raw table helpers.
- [ ] Keep the temporary semantic drivers behavior-equivalent to the current
  bootstrap methods, including pending-exception propagation and reentrant
  equality restart behavior.
- [ ] Add tests or preserve existing tests proving insertion, lookup,
  membership, deletion, tombstone reuse, and mutation-during-equality behavior
  still pass after the factoring.

Stage invariants:

- Raw table helpers do not take `ThreadState *` for protocol work and do not call
  `ThreadState::hash_value`, `ThreadState::test_equal`, `hash`, equality,
  descriptors, or arbitrary Python.
- Raw helpers return compact probe states, entry indexes, status integers,
  table identities, keys, values, or success/failure statuses. They do not
  return raw pointers or references that can survive a Python-visible call.
- Temporary C++ semantic drivers remain clearly temporary and should mirror the
  trusted bytecode sketches closely enough that they can be replaced
  mechanically later.
- This stage does not expose new public `dict` behavior and does not promote
  public dicts.

This milestone is the bridge from the bootstrap C++ implementation to the
future cache-bearing bytecode implementation. It should be a behavior-preserving
factoring, not a place to settle public dict representation questions.

### 7. Public Dict Unification Design

Design questions to resolve before implementation:

- Whether public `dict` will be one native layout with shape-backed modes, two
  native layouts registered to the same public class, or a merged table
  abstraction.
- How insertion of a non-string key into public `dict` promotes or replaces the
  existing string-only representation.
- Whether the final general public path is bytecode-backed with trusted probe
  opcodes, C++-backed through `ThreadState` helpers, or staged from one to the
  other.
- How dict displays should preserve evaluation/insertion order once general-key
  public dicts are supported.
- Active iterator behavior for promotion and mutation.

This is the point to revisit the older switchable-dictionary design. Do not
answer these questions in the bootstrap implementation by accident.

### 8. Public Dict And C API Integration

- [ ] Route public `dict` lookup, assignment, deletion, membership, and public
  methods through the chosen unified design.
- [ ] Add C API functions for semantic lookup, assignment, deletion, membership,
  and length operations.
- [ ] Document which C API functions may re-enter Python.
- [ ] Add native module tests that build string-key dicts, integer-key dicts,
  propagated `__hash__` and `__eq__` exceptions, and mutation during equality
  through the C API surface.

Stage invariant:

- Raw string-shape helpers remain unavailable through the C API.

### 9. Cleanup, Performance, And Stdlib Unblock

- [ ] Remove bootstrap-only `GeneralDict` exposure if the public `dict` path has
  absorbed its role.
- [ ] Remove obsolete string-only `TODO`s and helper names that imply raw hash
  storage.
- [ ] Audit dict fast paths so exact-string dictionaries still avoid Python
  bytecode and general dictionaries still expose cache-bearing hash/equality
  calls where required by the final design.
- [ ] Add focused performance checks for unchanged string-key workloads.
- [ ] Implement or unblock `errno.errorcode` using a real public integer-key
  `dict`.
- [ ] Update `doc/dictionaries.md`, `doc/clover-c-api.md`,
  `doc/development-priorities.md`, and the stdlib bringup checklist to reflect
  the completed scope.

This milestone should be cleanup and validation, not the place to introduce new
dictionary semantics.

## Test Plan

Bootstrap interpreter and native tests should cover:

- ordinary `{}` and dict displays still construct the existing string-only
  `dict`
- the internal `__clover_general_dict` class can be constructed directly
- `GeneralDict` stores and retrieves integer, bool, string, and object keys
- `True` and `1` collide according to Python equality and hash behavior inside
  `GeneralDict`
- custom `__hash__` is called for lookup, insertion, deletion, and membership
- custom `__eq__` is called only after matching hashes and non-identical keys
- exceptions from `__hash__` and `__eq__` propagate
- equality mutation cases are covered only to the extent supported by the
  bootstrap operations available so far; full stale-probe defense is a later
  unification requirement
- deleting arbitrary keys leaves the table valid and preserves remaining
  insertion order

Later unification tests should cover:

- public string-key dicts still use the trusted fast path and preserve insertion
  order
- inserting an integer key promotes or otherwise routes public `dict` to the
  chosen general representation
- deleting the non-string key does not incorrectly demote or corrupt the dict
- caches miss correctly after any public representation transition
- `errno.errorcode` is represented as a real public integer-key `dict`

Codegen tests apply only once the public general-dict path becomes bytecode
backed. They should be limited to structural guarantees, such as general dict
methods containing cache-bearing hash and equality call sites rather than native
helper calls that can execute Python.
