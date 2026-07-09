# Dictionaries

See [Switchable Dictionaries](switchable-dictionaries.md) for the detailed
shape, protocol, reentrancy, C API, and staging design.

## Current Representation

Public `dict` uses one Python class, one C++ `Dict` class, and one native layout.
Exact builtin instances have two canonical shapes:

- the string-keyed shape, whose live keys are all exact strings
- the general shape, which permits arbitrary Python keys

An exact builtin dict starts string-keyed. A semantic operation involving a
non-string key promotes it in place to the general shape. Promotion changes only
the shape and probe generation; it does not copy entries, replace the object, or
change insertion order. Promotion is one-way.

Dict storage consists of:

- an insertion-ordered entry array containing key, value, and canonical SMI hash
- an open-addressed probe table containing entry indexes plus empty and
  tombstone sentinels
- a live-entry count
- a probe-structure generation used to revalidate indexes across reentrant
  equality calls

The string-keyed and general shapes use the same storage layout. Exact-string
operations on the canonical string-keyed shape use trusted native hashing and
equality and cannot run Python. General operations use Python `hash` and
equality semantics and may raise, mutate the dict, or re-enter the VM.

## Hashes

Stored hashes are canonical SMI values. Public `hash()` and builtin hash helpers
use the same normalization, including BigInt reduction through CloverVM's
Mersenne-form hash modulus and `-1` to `-2` remapping. Equal integer values must
therefore have the same stored hash whether represented as bool, SMI, or BigInt.

## Remaining Work

Public `__getitem__`, `get`, and `__contains__` general-key paths use generated
bytecode with explicit hash and equality cache sites. Mutation paths still use
C++ semantic drivers calling cached `ThreadState` hash and equality helpers;
their intended hot path moves those protocol calls into trusted bytecode too.
The former bootstrap `GeneralDict` class has been removed after its unique
coverage was migrated to public `dict`.

Scopes and namespace storage are separate structures. They may share broad
open-addressing ideas with `Dict`, but their invalidation and parent-lookup
requirements should not be folded into the public dictionary representation.
