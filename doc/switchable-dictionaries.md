# Switchable Dictionaries

This document sketches the design for one Python-visible `dict` type with
multiple shape-backed implementation modes.

The motivating case is `errno.errorcode`: CPython exposes it as a real `dict`
keyed by integer errno values. CloverVM's current `dict` implementation is
string-key-only, which keeps namespace-style maps simple but blocks compatible
stdlib modules.

## Goals

- Preserve one public Python `dict` class.
- Keep exact-string dictionaries fast and non-reentrant.
- Promote dictionaries to a general shape when non-string keys are inserted.
- Route general dictionary hashing and equality through bytecode-visible calls
  with normal inline caches.
- Avoid C++ helpers that invoke `__hash__`, `__eq__`, descriptors, or other
  Python-visible behavior behind the interpreter's back.

## Non-Goals

- Do not add a separate public string-dict class.
- Do not make a non-dict substitute for APIs such as `errno.errorcode`.
- Do not broaden the string-only implementation with ad hoc builtin-key cases.
- Do not call arbitrary Python from dict probing C++ helpers.

## Shape Invariants

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
  - `_dict_probe_table_identity(d)`
- hash normalization:
  - `NormalizeHash(value)`
- probing:
  - `_dict_probe_start(d, hash_value)`
  - `_dict_probe_step(d, probe)`
  - `_dict_probe_advance(probe)`
  - `_dict_probe_record_tombstone(probe, entry_status)`
  - `_dict_entry_still_matches(d, table_identity, entry, candidate)`
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

Probe results should be integer-shaped, not strings or public status objects.
One convenient contract is:

- `_dict_probe_step` returns `entry >= 0` for an occupied entry index
- `DICT_PROBE_EMPTY == -1`
- `DICT_PROBE_TOMBSTONE == -2`

This matches the current table representation style and keeps trusted bytecode
branches cheap. Named constants in trusted source can carry readability; the
runtime value should remain a small integer.

Dictionary entry hashes are stored as SMI values. General dict bytecode should
use the same hash protocol path as public `hash(key)`, then pass that result
through `NormalizeHash`. `NormalizeHash` is a general trusted opcode, not
dictionary-specific policy: it accepts `bool`, SMI, and BigInt integer results,
does not call Python, and returns a normalized SMI hash or raises if the value
is not an integer.

`NormalizeHash` should preserve exact small integer hashes before applying any
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
of `NormalizeHash` keeps the public `hash()` result, dict storage, and any
future C API compatibility path aligned.

Every exact built-in hash helper that can feed dict storage must use the same
normalization policy. In particular, the string-shaped dict fast path may call
a trusted string hash helper directly, but that helper must return a normalized
SMI hash, including `-1` to `-2` remapping. Raw string mixing helpers, if kept,
should stay private and should not be callable from dict insertion, lookup, or
public `hash()` without immediately applying `NormalizeHash`.

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
the entry index is reusable only if the same table identity and same candidate
key are still present at that entry.

## General Dict Bytecode Sketches

The sketches below are written in Python-like pseudocode. They are not intended
as public Python source; they describe trusted Clover bytecode bodies with
ordinary call sites and inline caches.

### Hashing

```python
def _general_dict_hash_key(key):
    return NormalizeHash(hash(key))
```

This assumes public `hash()` is implemented as the shared Python-visible hash
protocol. Missing or disabled `__hash__` should fail through that shared path;
non-integer and out-of-SMI integer hash results should be handled by
`NormalizeHash`. The dict should not have a parallel hash protocol.

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

                table_identity = _dict_probe_table_identity(self)
                equal = candidate == key
                if not _dict_entry_still_matches(
                    self, table_identity, entry, candidate
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

            table_identity = _dict_probe_table_identity(self)
            equal = candidate == key
            if not _dict_entry_still_matches(
                self, table_identity, entry, candidate
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

                table_identity = _dict_probe_table_identity(self)
                equal = candidate == key
                if not _dict_entry_still_matches(
                    self, table_identity, entry, candidate
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
- record the current table identity before calling equality
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
idea with a compact table generation or identity plus a probe-entry key check.

## Interaction With Existing Caches

The existing operator inline cache can continue to guard the outer subscription
operation by receiver and key shapes. It should choose:

- a trusted string-dict handler when receiver shape and key shape prove the
  fast path
- a bytecode-backed dict method for general shape or uncacheable cases

The hash and equality caches belong inside the bytecode-backed method. That
keeps their call sites explicit and inspectable for the interpreter and future
JIT.

## Open Questions

- Should non-string lookup in a string-shaped dict promote immediately, or
  should there be a bytecode miss path that calls `__hash__` but proves no
  equality with existing keys is needed?
- Should string-to-general promotion reuse the current `Dict` table in place,
  or allocate a new general table and reinsert entries?
- How should `hash()` be staged so general dict bytecode can use the shared
  hash protocol plus `NormalizeHash` without inventing a parallel dict-only
  hash path?
- How should active dict iterators respond to promotion and mutation?
- Where should the built-in bytecode method bodies live so they are available
  during bootstrap while still being the only code allowed to emit trusted dict
  opcodes?

## Test Plan

Interpreter tests should cover:

- string-key dicts still use the trusted fast path and preserve insertion order
- inserting an integer key promotes the dict and supports `errno.errorcode`
- `True` and `1` collide according to Python equality and hash behavior
- custom `__hash__` is called for lookup, insertion, deletion, and membership
- custom `__eq__` is called only after matching hashes and non-identical keys
- exceptions from `__hash__` and `__eq__` propagate
- equality that mutates the same dict does not use stale probe state
- deleting the non-string key does not demote the dict
- caches miss correctly after promotion from string shape to general shape

Codegen tests should be limited to structural guarantees, such as general dict
methods containing cache-bearing hash and equality call sites rather than
native helper calls that can execute Python.
