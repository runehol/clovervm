# Known Python Semantic Deviations

This document tracks intentional or temporary deviations from CPython/Python
semantics. The goal is to make each gap explicit enough that we can accept it
for a slice without forgetting to revisit it later.

This is not a list of unsupported Python features. Add entries here when
clovervm implements a construct or runtime surface, but the behavior differs
from Python.

## Context Managers

### `with` statement `__exit__` lookup timing

Python evaluates and preserves the context manager exit function before calling
`__enter__`. In specification form, it is roughly:

```python
manager = EXPR
exit = type(manager).__exit__
value = type(manager).__enter__(manager)
```

The current clovervm `with` implementation calls `__exit__` later through
`CallSpecialMethod` on the manager. That means mutations between `__enter__` and
block exit may affect which `__exit__` implementation runs.

Reason: clovervm currently does not have a separate method-load primitive that
preserves the callable plus receiver binding, and `LoadAttr` does not
materialize bound method objects.

To close: add either bound method objects, or a method-load representation that
can preserve `(callable, self)` and be called later.

### Traceback argument to `__exit__`

Python passes `(exc_type, exc_value, traceback)` to `__exit__` on exceptional
exit. clovervm currently has exception objects but not full traceback objects.

The initial implementation should pass:

```python
(exc_value.__class__, exc_value, None)
```

Reason: this preserves the most important suppression behavior while tracebacks
are not represented.

To close: add traceback objects and thread them through exception propagation.

## Class Construction

### Class-body `__class__` shadowing is unsupported

CPython permits a class-body binding such as `__class__ = object` as a
namespace entry. CloverVM explicitly rejects a surviving `__class__` binding
when building the class, with `TypeError: __class__ shadowing in class bodies is
not supported`.

Reason: the current shape-backed attribute model does not support independently
shadowing its synthetic `__class__` descriptor. Ordinary instance `__class__`
reassignment retains its existing behavior.

### Class calls only partially support custom `__new__`

Python class construction calls `__new__` first, then conditionally calls
`__init__` on the returned object. clovervm's class-call path currently builds a
constructor thunk from either the ordinary allocate-then-`__init__` path or a
`__new__`-only path.

If a class supplies `__new__` and no `__init__`, clovervm calls `__new__` with
the class argument and preserves normal argument adaptation. If a class supplies
both custom `__new__` and `__init__`, clovervm still does not implement Python's
full constructor protocol.

Reason: the current constructor thunks cover the common allocate-then-init path
and the new-only path separately. They do not yet represent the more general
`__new__`/`__init__` interaction where `__init__` is called only when Python
would call it for the object returned by `__new__`.

To close: teach class calls to handle the combined custom `__new__` plus
`__init__` protocol, including non-instance returns and conditional `__init__`
dispatch.

### `dict` construction is implemented by `__new__` only

CPython's `dict.__new__` allocates an empty dictionary and `dict.__init__`
populates it. CloverVM's exact builtin `dict` instead installs a private
Python-backed `__new__` that accepts mapping, iterable-of-pairs, and keyword
inputs and returns the populated dictionary. It does not install
`dict.__init__`.

Consequently, direct calls to `dict.__new__` return populated dictionaries and
direct reinitialization through `dict.__init__` is unavailable. Ordinary
`dict(...)` construction preserves positional-input-before-keyword ordering.

Reason: CloverVM's constructor thunks support new-only construction directly,
while the combined `__new__`/`__init__` protocol remains incomplete.

To close: implement the full combined constructor protocol, then restore the
CPython allocation/population split.

### Bytes iteration

`bytes.__iter__` returns a dedicated `bytes_iterator` yielding unsigned integer
values from the stored byte payload, including for inherited bytes subclasses.
Subclass `__iter__` overrides still apply to ordinary iteration. Exhaustion is
permanent and releases the retained source on the first exhausted advance.
Iterator length hints and pickling/state methods remain unsupported, following
the bounded native iterator interface. General sequence-only `__getitem__`
iteration fallback remains unsupported.

### `bytes.join` uses stored byte payloads and the existing iterator subset

`bytes.join(iterable, /)` accepts stored bytes (including bytes subclasses) and
bytearray elements, returning exact bytes. It fully consumes nonexact-sequence
inputs before validating elements or observing their byte payloads. Later source
callbacks can therefore mutate earlier bytearrays before they are copied, and
iterator exceptions take precedence over invalid-element errors. Stored payloads
bypass conversion, length, and indexing hooks. A singleton exact bytes element
is returned unchanged; other singleton elements are copied into exact bytes.

General buffers and `bytearray.join` remain unsupported. Like the existing
iterable materializers, this method has no sequence-only fallback or length-hint
callbacks; CPython can instead raise from a length hint before consuming items.
Empty-result interning is unspecified. Checked result lengths use the VM's SMI
storage limit, and heap exhaustion retains the existing fatal VM heap policy.

### `str.join` uses stored strings and the existing iterator subset

`str.join(iterable, /)` accepts stored strings, including subclasses, and
returns exact str. Exact list/tuple inputs use their native contents; subclasses
and other supported iterables are fully consumed before element validation.
Iteration errors therefore take precedence over invalid-element errors. Stored
payloads bypass conversion, length, and indexing hooks. A singleton exact str
is returned unchanged; a singleton subclass is copied into exact str.

As with `bytes.join`, sequence-only fallback and length-hint callbacks remain
unsupported. Iterator-return validation retains the existing runtime limitation:
a custom `__iter__` returning a list can be consumed instead of being rejected
as CPython requires. This slice does not change generic iterator acquisition.
Empty-result interning is unspecified. Both methods share native
checked sizing, ownership and copying: lengths are bounded by SMI storage and
allocation-byte arithmetic, and payload views are reacquired after allocation.
Heap exhaustion retains the existing fatal VM heap policy. Bytes iteration is
not introduced by this method; acceptance follows existing iterator support.

### `bytearray` provides a bounded mutable byte container

`bytearray` supports empty/zero-filled construction, stored bytes/bytearray
copies, integer iterables, length/truth, indexing and slicing, item/slice
assignment and deletion, `append`, `extend`, `clear`, `pop`, `insert`, `remove`,
`reverse`, `copy`, `find`, `rfind`, `index`, `rindex`, `count`, `startswith`,
`endswith`, `split`, `rsplit`, `partition`, `rpartition`, `splitlines`,
`expandtabs`, repr/str, all six
comparisons with bytes/bytearray, and live integer iteration. `bytes(bytearray)` copies into
an independent immutable bytes object; bytes immutability and hashing are unchanged.

Like the existing dict boundary, construction is implemented by a Python-backed
`__new__` that returns a populated object. Direct `__new__` therefore differs
from CPython, `__init__`/reinitialization is unavailable, and bytearray subclassing
is rejected. Encoded-string constructors, arbitrary buffer objects, general
user-defined `__index__`, concatenation/repetition and other bytes-like
methods remain deferred. Iterable conversion uses the existing iterator subset:
there is no sequence-only fallback or length-hint callback. Stored integer/bool
payloads (including supported integer subclasses) are accepted. Counts and
growth are checked against the VM's SMI storage limit; heap exhaustion retains
the existing fatal VM heap policy rather than recoverable `MemoryError`.

`pop(index=-1, /)` and `insert(index, item, /)` validate stored integer indices
against signed 64-bit platform bounds before empty/bounds checks or byte
validation; overflowing indices raise `OverflowError`. Insertion clips valid
indices to the ends. `remove(value, /)` validates a byte and removes its first
occurrence without equality callbacks. Integer-subclass overrides are bypassed;
general `__index__` objects are rejected without callbacks. `reverse()` mutates
in place and `copy()` returns an independent exact bytearray, including for empty
inputs. Mutators retain the existing live-iterator cursor behavior and permanent
exhaustion. Native argument/receiver diagnostics may differ in wording.

Search methods accept stored bytes/bytes-subclass and bytearray payloads;
`find`, `rfind`, `index`, `rindex`, and `count` also accept stored integer/bool
byte needles in `0..255`. `startswith` and `endswith` accept tuples of these
binary payloads, trying entries in order until a match; an invalid entry raises
only when reached. Stored tuple, bytes, and integer subclass hooks are bypassed.
All seven methods are positional-only, validate start then end before needles,
and accept `None` and arbitrarily large stored integer bounds with clipping.
Starts beyond the end and reversed ranges remain no-match ranges, including
for empty needles. Counts are non-overlapping. Custom `__index__` and general
buffer inputs remain unsupported without callbacks; native diagnostic wording
may differ. Search retains owners and borrows spans only during synchronous
scans without allocation, callbacks, safepoints, or mutation.
Run `python3 tests/host/test_bytearray_search.py [path/to/clovervm]` with
CPython 3.14.5 for the reproducible native-method differential trace.

Splitting and partitioning follow CPython 3.14.10 for supported inputs.
`split(sep=None, maxsplit=-1)` and `rsplit` accept keyword arguments, use
ASCII whitespace for `None`, and convert `maxsplit` before validating the
separator. Signed platform-size integer/bool limits are supported; negative
limits mean unlimited splitting and overflow raises `OverflowError`.
`partition(sep, /)` and `rpartition` require a positional separator. Explicit
separators accept stored bytes/bytes-subclass and bytearray payloads; empty
separators raise `ValueError`, even with a zero split limit. Every result
component is a fresh exact bytearray, including empty pieces, no-match copies,
and matched partition separators. Source/separator aliases are supported.
Custom `__index__`, general buffers and bytearray subclasses retain the existing
unsupported boundary; native error wording may differ. The offset scan is
shared with bytes; result construction preserves each type's identity rules.
For explicit separators longer than 16 bytes, these four methods on both types
use constant-space Two-Way search in either direction. Separator factorization
is reused across split ranges, giving linear total scanning/preprocessing time
in source plus separator length. Shorter separators retain a bounded library
search path. Search state and payload views expire before result allocation.
The adapted factorization's source and license attribution are recorded in
`src/builtin_types/separator_search.h`.
Run `python3.14 tests/host/test_bytearray_split.py [path/to/clovervm]` using
CPython 3.14.10 for the supported-input differential trace.

The same `SeparatorSearch<CharT>` matcher serves explicit-separator `str.split`,
`rsplit`, `partition`, and `rpartition`, using full-width unsigned character
comparisons and a separately measured eight-character library-search cutoff.
String validation, whitespace splitting, and result identity policies are
unchanged. Explicit-separator scans retain their owners and finish before
result allocation; only offsets survive the scan. Other string search methods
are outside this change.
Run `python3.14 tests/host/test_string_separator_search.py [path/to/clovervm]`
using CPython 3.14.10 for the string search oracle.

Item access, assignment, and deletion enter native code, with trusted handlers
for SMI and slice keys. Slice assignment inspects the replacement on every
cached call: bytes and distinct bytearrays copy directly into the destination,
reusing spare capacity; self-assignment snapshots before modifying storage.
Only the iterable fallback enters the managed snapshot loop.

`extend` copies stored bytes/bytearray payloads directly, reusing spare capacity
and handling self-extension without a snapshot. Arbitrary iterable extension
and iterable slice assignment validate a private snapshot before committing;
mutations performed by input callbacks remain visible on failure.
Slices are validated before input consumption and normalized against the current
length after callbacks. Iterators follow current contents through backing
replacement, release their source on exhaustion, and remain exhausted after
later mutations of that source.

The internal `with_bytearray_writable_bytes` API permits only a synchronous,
exclusive native operation without allocation, callbacks, safepoints,
reclamation, resizing, alias mutation or retained pointers. It preserves owner
lifetime, not pinning. This is not a buffer protocol, memoryview, or persistent
export. FileIO `readinto` integration is separate; concurrent/moving collection,
Python signal callbacks and asynchronous I/O must reassess this contract.

### Binary stripping accepts stored binary charsets

`bytes` and `bytearray` provide positional-only `strip(bytes=None, /)`,
`lstrip`, and `rstrip`, checked against CPython 3.14.10. Omitted or `None`
charsets remove only ASCII whitespace (`0x09..0x0d` and `0x20`). Explicit
stored bytes/bytes-subclass or bytearray payloads are sets of removable bytes,
including NUL and high-bit bytes; an empty charset removes nothing. Invalid
charsets raise `TypeError` even for empty receivers. Subclass conversion,
length, indexing, and iteration hooks are bypassed. General buffer inputs and
bytearray subclass construction remain unsupported; native diagnostic wording
may differ from CPython.

Unchanged exact bytes return the receiver; bytes-subclass results are exact
bytes. Every bytearray result is an independent exact bytearray, including
unchanged and empty results. Source/charset aliases are supported. Explicit
binary charset membership uses a fixed 256-entry stack table; the complete
operation takes linear time in source plus charset length and constant extra
scan space. The shared `scan_strip<CharT>` boundary scanner also serves strings,
whose whitespace classification, charset lookup, call behavior, and result
construction remain unchanged. The synchronous scans cannot allocate, call
Python, mutate payloads, or poll safepoints. Binary result allocation retains
only offsets; owned source payloads are reacquired for copying afterward.

Run `python3.14 tests/host/test_binary_strip.py [path/to/clovervm]` with
CPython 3.14.10 for the supported-input differential trace. Interpreter
regressions also cover all 256 bytes, aliasing, result identity/independence,
stored-subclass hook bypass, string scanner behavior, and every-safepoint
reclamation. No object-layout, protocol, cache, or core execution changes are
involved.

### Bytes splitting accepts stored binary payloads and integer limits

`bytes.split` and `bytes.rsplit` implement separator/default-None splitting,
ASCII whitespace and signed platform-size `maxsplit` limits, checked against
CPython 3.14.5. `sep` and `maxsplit` accept keywords. `partition` and
`rpartition` accept a positional-only separator and return three-tuples.
A successful partition preserves the separator object (including bytearray);
a failed partition preserves the original receiver, including bytes subclasses.
Split pieces and newly constructed partition pieces are exact bytes.

Separators accept stored bytes/bytes-subclass payloads and bytearray, bypassing
subclass conversion, length, indexing and search hooks. General buffer inputs
remain unsupported. Limits accept stored int/bool/int-subclass payloads without
custom `__index__` callbacks. Out-of-platform-range integers raise OverflowError;
native argument error wording can differ from CPython. Bytearray subclassing
remains unavailable. No changes to interpreter, codegen, JIT, layout, cache or
buffer protocols are involved.

### String padding uses stored integers and bounded native storage

`str.ljust(width, fillchar=' ', /)`, `rjust` and `center` with the same
signature, and `str.zfill(width, /)` follow CPython 3.14.10 for code-point
padding, default spaces, single-character fill validation, center parity and
ASCII sign handling. Width conversion precedes fill validation, even when no
padding is required. Exact-string no-ops preserve identity; subclass inputs
produce exact strings, using stored payloads without subclass hooks.

Widths accept stored int/bool/int-subclass payloads within the signed platform
index range. Custom `__index__` remains unsupported and raises TypeError without
callbacks. Positive result lengths beyond SMI string-length storage or checked
allocation-size bounds raise OverflowError. Actual heap exhaustion retains the
existing fatal allocator policy rather than promising recoverable MemoryError.
Native argument diagnostics can differ from CPython. Padding counts the existing
string representation's code points, not display columns or grapheme clusters.
No general Unicode framework, layout, protocol or interpreter changes are added.
Run `python3.14 tests/host/test_string_padding.py [path/to/clovervm]` with
CPython 3.14.10 for the differential trace; VM-only storage/index limits have
separate regressions.

## Methods And Attribute Access

### `obj.method` does not produce a bound method object

In Python, reading a function through an instance produces a bound method object:

```python
f = obj.method
f()
```

clovervm's `LoadAttr` currently loads the underlying attribute value. Binding is
handled by `CallMethodAttrPositional` and `CallSpecialMethod`, which internally split a
method call into `callable` and optional `self`.

Reason: this is enough for direct method calls and avoids introducing bound
method objects before the object model needs them.

To close: implement bound method objects, or expose a first-class method-load
operation with equivalent semantics where needed.

### Descriptor protocol execution is partial

Python invokes descriptor methods such as `__get__`, `__set__`, `__delete__`,
and `__set_name__` at specific attribute-access and class-creation points.
clovervm already classifies descriptor-shaped values so lookup precedence and
cache invalidation can account for them, but descriptor execution is not fully
wired through the interpreter.

Observable consequences include:

- loading or calling an attribute that requires descriptor `__get__` dispatch
  raises an internal unsupported-descriptor error instead of invoking `__get__`;
- class creation rejects values that require `__set_name__` notification instead
  of calling `descriptor.__set_name__(owner, name)`.
- native-originated attribute reads and method calls can still reach an unchecked
  descriptor loader and abort the process when a Python subclass supplies a
  descriptor override. The shared boundary remains an
  [open design issue](descriptor-execution.md#open-attribute-access-initiated-by-native-code);
  it needs to be addressed across native callers rather than patched per class.

Reason: the lookup planner needs descriptor awareness for correct precedence and
cache guards, but invoking arbitrary Python code from those descriptor paths is a
separate interpreter-dispatch slice.

To close: route descriptor get/set/delete/name notifications through explicit
fallible interpreter paths with correct binding, exception propagation, and
cache behavior.

## Truthiness

### Length-result index coercion and sys.maxsize

Explicit conversions, Python `not`, and conditional jumps now dispatch
`__bool__`, then `__len__`, using cached special-method lookup and Python return
continuations. Conditional jumps preserve operand identity for short-circuit
expressions. See [Truthiness Dispatch](truthiness-dispatch.md).

Length results accept bool, SMI, and heap integers through INT64_MAX. Non-integer
results with `__index__` remain unsupported and raise TypeError. The native limit
is independent of the current SMI-sized `sys.maxsize`, whose correction is
separate work. Index coercion needs a separate return-validation stage.

### NotImplemented retains its older boolean behavior

Immediate `NotImplemented` deliberately remains truthy, preserving older Python
behavior. Returning it from `__bool__` raises TypeError. The immediate fast path
keeps this behavior intentionally; it is not a pending protocol implementation.

## Builtins And Iteration

### `range()` returns a range iterator

Python's `range()` returns an immutable reusable range object. `iter(range(...))`
then creates a separate range iterator.

clovervm's public `range()` builtin currently returns `RangeIterator` directly.
This works for the current loop and `next(range(...))` slice, but it means the
result is itself a mutable, consumable iterator rather than a reusable range
object.

Reason: the first range slice optimized direct `range(...)` iteration before
adding a distinct Python-visible range object.

To close: introduce a range object, make `range()` return it, and have
`range.__iter__` create a fresh `RangeIterator`.

### `enumerate` and `zip` use the existing iterator protocol subset

`enumerate(iterable, start=0)` and `zip(*iterables, strict=False)` are lazy
native builtin iterator objects with managed construction and stepping methods.
Both accept builtin containers and supported custom iterators, return themselves
from `iter`, and yield exact tuples. Sources are acquired eagerly at construction;
elements are fetched only by `next`. `enumerate` accepts arbitrary-size builtin
integers, bools and integer subclasses for `start`, normalizing the counter to an
exact integer without invoking subclass overrides. Custom `__index__` inputs
remain unsupported. Exact `enumerate(...)` calls use CPython's vectorcall
keyword matching by string payload; inherited and direct `__new__` calls use
dictionary lookup, including keyword hash/equality behavior and duplicate-argument
validation. The exact-type entry uses the normal constructor-thunk cache, while
the public `__new__` method remains independently callable.

`zip` consumes sources left to right, supports zero inputs and strict length
checking, and preserves CPython 3.14.5 mismatch consumption and diagnostics.
Neither wrapper adds an exhaustion latch: source exceptions and strict mismatches
leave it resumable, and exhaustion remains permanent when its sources remain
exhausted. Non-strict zip and enumerate propagate source `StopIteration` payloads;
strict zip discards them. Reentry uses frame-local partial rows and enumerate
reads its shared counter after the source callback returns.

Construction relies on the existing `iter` implementation. Validation of invalid
`__iter__` results and sequence-only `__getitem__` fallback belong to the separate
ordinary-iteration work. Subclasses preserve their runtime class and inherit the
existing class-call limitations on custom initialization. Pickling, CPython's
private result-tuple recycling/retention behavior, and specialized iteration/JIT
paths are omitted. Held result tuples remain distinct and retain their elements.

### Cycle uses a managed lazy cache

`itertools.cycle(iterable, /)` acquires its source at construction without
advancing it. Successful source results are saved by reference; mutable elements
remain shared. Ordinary source exceptions propagate without closing the source.
The first `StopIteration` discards its payload, releases the source, and switches
permanently to replay, including for resumable inputs. Empty inputs remain
exhausted. Cached elements remain retained until the cycle is released.
Reentry follows CPython 3.14.10's state transitions: a successful outer call still
caches its item after nested exhaustion. Explicit reinitialization is a no-op,
including invalid arguments. When both a nested and an outer source advancement
exhaust, CPython 3.14.10 resets replay to the first saved element for the outer
call. CPython 3.14.4 instead preserves the nested replay position. The cycle
regression and host differential target 3.14.10; the host harness requires that
exact reference version.

Infinite inputs are never eagerly materialized, but their saved-element cache
grows without bound. Inputs use existing iteration support. Allocation-failure
and exact refcount-sensitive finalization timing, subclass fidelity, signature
metadata, copy, pickle, and repr fidelity are excluded. Managed argument-error
wording differs; no native layout or iterator protocol changes are included.

### Combinatorial itertools use managed snapshots and index state

`itertools.combinations`, `combinations_with_replacement`, and `permutations`
snapshot supported input iterables into exact tuples at construction and yield
independent tuple results lazily in lexicographic input-index order. Equal input
elements occupy distinct positions. Pools remain retained after exhaustion;
source iterators are not retained separately from their yielded elements.
Subsequent explicit `__init__` calls are no-ops, including invalid arguments.

Combinations accept builtin int/bool/subclass `r` through the existing
`operator.index` subset; custom non-int `__index__` remains unsupported.
Permutations accepts only builtin int/bool/subclass payloads or `None`, matching
the reference's rejection of custom non-int indices. Both combinations APIs
convert and check signed 64-bit `r` before acquiring the input, then reject
negative values after snapshotting. Permutations snapshots before validating
`r`, with omitted/`None` using the pool length. All APIs consume the input for
`r=0` and yield one empty tuple.

The CPython 3.14.10 checked index-array bound (`r > (2**63 - 1) // 8`) raises
`MemoryError` after acquisition. Already-empty results skip unnecessary index or
cycle allocation. Exact allocation-failure timing, native throughput, tuple
recycling, refcount-sensitive finalization timing, copy/pickle/repr and subclass
or signature metadata fidelity are excluded. Ordinary managed argument-error
wording differs. Inputs use the existing iterator subset, without sequence-only
fallback or length-hint callbacks. No native or general protocol machinery is
added.

## Numeric Semantics

### `math.log` and `math.isnan` use builtin numeric payloads

`math.log` supports builtin ints/bools and int subclasses, including positive
integers too large for binary64. It reads the builtin integer payload through
`int.__int__`, ignoring subclass conversion and arithmetic overrides. The
overflow fallback uses existing correctly rounded integer true division to
normalize a mantissa before calling native libm; it allocates a power-of-two
denominator proportional to the input size. Numerical accuracy assumes binary64
round-to-nearest and platform libm accuracy; results are not guaranteed to be
correctly rounded or bit-identical to CPython.

`math.isnan` uses the existing builtin float/int/bool conversion helper before
comparing the converted float. Nonnumeric values raise `TypeError`, and integers
outside binary64 range raise `OverflowError`, including negative huge integers
encountered during `statistics.geometric_mean` validation. Numeric subclasses
use stored payloads; custom `__float__`/`__index__` protocols remain unsupported.
In particular, CPython calls an int subclass's `__float__` in `isnan` but ignores
that hook in `log`. CloverVM does not reproduce the `isnan` hook behavior.

Existing wrapper call conventions remain: `log` and `isnan` accept keywords,
and an explicit `log(x, None)` uses the natural logarithm. CPython rejects these
forms. Other math functions, including `log2`/`log10` conversion and broader
`isfinite`/`isclose` coercion behavior, are outside this repair. This supplies
math prerequisites for the builtin numeric subset; it does not implement
`statistics.geometric_mean`.

### Float NaNs use a canonical hash

All builtin float NaNs hash to `0x02e3779b97f4a7c1`, regardless of sign, payload,
or object address. This avoids common small-integer, boolean, zero, and infinity
hashes and keeps hashes stable across relocation without extra object
storage. Float subclasses inherit this behavior unless they override `__hash__`.
Distinct NaN objects remain distinct dictionary keys; lookup checks identity
before numeric equality. Tuples containing NaNs inherit the hash collisions.

Modern CPython instead uses an object-identity hash for NaNs. CloverVM accepts
collisions, including potentially quadratic insertion for many distinct NaN
keys, in exchange for the simpler relocation-safe policy.

### Base-conversion builtins accept stored integer payloads only

`bin(number, /)`, `oct(number, /)`, and `hex(number, /)` support tagged integers,
arbitrary-size heap integers, booleans, and int subclasses. They format the
stored payload without invoking subclass conversion or arithmetic overrides,
with lowercase prefixes and a leading minus sign for negative values.

Non-int objects with custom `__index__` remain unsupported: these builtins raise
`TypeError` without invoking the hook. CPython 3.14.5 accepts such objects via
its general index protocol. Closing this gap requires separate protocol work;
these builtins do not add a local special-method lookup substitute. Argument
count and keyword errors use the existing native-call binder, so their wording
can differ from CPython. Formatting mini-language support and `int.__format__`
are outside this slice.

### Some integer consumers still require SMI-sized tagged integers

In Python, `bool` is a subclass of `int`, and arbitrary-precision integers are
accepted by APIs that use the integer protocol as long as the value is in range
for the operation. clovervm now models `bool` as an `int` subclass and supports
heap `BigInt` values for general integer arithmetic, but some runtime consumers
still require SMI-sized tagged integer arguments.

For example, Python accepts `range(False)` as `range(0)`, while clovervm reports
`TypeError: range() arguments must be integers`. Non-SMI `BigInt` range
arguments are also outside the current range slice.

Reason: those consumers are still implemented directly on tagged SMI values.
`range()` also currently returns a `RangeIterator` whose fields are SMI-backed.

To close: route SMI-sized argument conversion through the same intlike policy
used by numeric operations where appropriate, and decide per API whether
non-SMI `BigInt` values should be accepted, rejected with Python-compatible
overflow behavior, or require a wider backing representation.

### BigInt support is not yet complete Python `int` semantics

Python integers have arbitrary precision across parsing, arithmetic, indexing,
hashing, conversions, and protocol dispatch. clovervm has heap `BigInt` values
and promotes many arithmetic overflow paths out of the tagged SMI range, but the
BigInt surface is still partial.

Known remaining gaps include APIs that still require SMI-sized values, non-SMI
BigInt indexing, and range support. Integer hashing and arbitrary-key public
dictionaries are implemented.

Reason: the first BigInt slice added the representation and core arithmetic
surface without converting every integer consumer in the runtime.

To close: complete the remaining integer-protocol consumers and remove SMI-only
argument paths where Python accepts wider integers.

## Exceptions

### Ordinary hierarchy with generic exception behavior

The ordinary builtin hierarchy is checked against CPython 3.14.5. It includes
`ArithmeticError` (`FloatingPointError`, `OverflowError`, `ZeroDivisionError`),
`LookupError` (`IndexError`, `KeyError`), `SyntaxError` → `IndentationError` →
`TabError`, `BufferError`, `EOFError`, `MemoryError`, `ReferenceError`, and
`RuntimeError` children `RecursionError` and `PythonFinalizationError`.
`Warning` derives from `Exception`, with direct children `BytesWarning`,
`DeprecationWarning`, `EncodingWarning`, `FutureWarning`, `ImportWarning`,
`PendingDeprecationWarning`, `ResourceWarning`, `RuntimeWarning`, `SyntaxWarning`,
`UnicodeWarning`, and `UserWarning`. `KeyboardInterrupt` remains a direct
`BaseException` child, outside `Exception`.

Default-allocated exception constructors capture the original positional `args`
before binding and calling `__init__`. Custom Python initializers therefore see
those arguments even when they omit a base initializer. Reinitializing a subclass
only changes the fields assigned by its initializer, leaving `args` unchanged
unless it explicitly calls the native base initializer.
`StopIteration.value` starts as `None` during allocation and is set by the native
`StopIteration.__init__`; internal exception factories still construct their
fully initialized values directly. `KeyError` still uses generic string
conversion rather than quoting a single argument with `repr`. The syntax-error
family still uses generic constructors and formatting, without CPython's structured location fields or
argument validation. Exception `args` remains read-only. Specialized constructor
attributes for existing classes such as `ImportError`, `NameError`, and
`AttributeError` are also incomplete.

Registration does not add runtime detection or production: recursion-limit
checks, warning emission, finalization detection, and emergency `MemoryError`
allocation are absent. Ordinary allocation failure is not converted into the
new `MemoryError` class.

Remaining families outside this slice are `UnicodeTranslateError`,
`SystemExit`, `GeneratorExit`, `StopAsyncIteration`, `BaseExceptionGroup`, and
`ExceptionGroup`. Chaining, full traceback support, and signal handling remain
separate work. Supplying these classes does not complete specialized exception
behavior.

### One-shot text codecs are limited to UTF-8 and Latin-1

`str.encode` and `bytes.decode` support UTF-8 and Latin-1, their standard aliases,
and normalized encoding names. The defaults are `encoding="utf-8"` and
`errors="strict"`; both options accept keywords. Other error policies explicitly
raise `NotImplementedError`, even on valid or empty input, rather than silently
ignoring unsupported handlers. Unknown encoding names raise `LookupError`, apart
from CPython's empty-bytes decode shortcut which skips encoding lookup. Argument
types and embedded NULs are still checked before that shortcut.

Strict conversion failures produce `UnicodeEncodeError`/`UnicodeDecodeError`
with canonical encoding names, absolute source positions, and encoding-specific
reasons. Adjacent encoding failures share an error span, including across scratch
boundaries. Errors retain the original immutable input object (including bytes
subclasses), rather than copying a decode payload as CPython does. Results are
exact builtin bytes/str values. Conversion measures before allocating the final
result, then uses bounded native scratch buffers. String storage bridges through
real `char32_t` objects using `memcpy`; this requires the current 32-bit string
representation. No `wchar_t`/`char32_t` pointer aliasing is used.

A codec registry, public `codecs`/`encodings` modules, Python incremental wrappers,
other encodings/error handlers, constructor encoding overloads, and text I/O
remain deferred.

### Unicode exception fields use ordinary overflow attributes

`UnicodeError` derives from `ValueError`; `UnicodeEncodeError` and
`UnicodeDecodeError` derive from `UnicodeError`. All three reuse the existing
`ExceptionObject` layout. The specialized constructors accept five positional
arguments and store the exact supplied objects in both `args` and the ordinary
overflow attributes `encoding`, `object`, `start`, `end`, and `reason`.
Unlike CPython, construction performs no type validation, index conversion,
range checks, or bytearray snapshotting, and invokes no conversion callbacks.
Mutable inputs remain shared with the caller. Formatting checks that the payload
is a string (encode) or bytes (decode), and that positions are stored integers
within signed platform-size bounds, before reading character or byte storage.

The fields intentionally lack CPython's typed member descriptors: assignment may
store any value, deletion removes the attribute, and instance dictionaries expose
the fields. Formatting validates payload and positional fields before accessing
the input. Fields become present during initialization rather than defaulting to
CPython's uninitialized native field values after a direct `__new__` call.
Inherited `args` remains read-only under the existing exception contract;
`BaseException.__init__` can update it independently of the structured fields.
The shared [native-subclass descriptor execution limitation](descriptor-execution.md#open-attribute-access-initiated-by-native-code)
is not changed by these classes. `UnicodeTranslateError`, encoding/decoding
operations, and automatic production of Unicode exceptions remain deferred.

### OSError fields use ordinary latent attributes

The POSIX OSError hierarchy and IOError/EnvironmentError aliases share one native
layout, including Python subclasses. Constructors select errno-specific classes
only for exact OSError, preserve filename/filename2 and args conventions, and
support inherited native allocation with a custom Python initializer. Custom
Python __new__ plus __init__ still depends on the general constructor protocol.

errno, strerror, filename and filename2 start as latent stable slots with
class-level None defaults. characters_written starts latent without a default.
Setting/deleting these fields uses ordinary attribute and shape transitions;
class defaults may be shadowed or overridden by Python subclasses. This
intentionally differs from CPython's member descriptors: class introspection
returns defaults, instance dictionaries expose ordinary present slots, and
characters_written assignment accepts any Python value instead of coercing a
platform-sized integer. Native constructor progress counts accept builtin
ints/bools/int subclasses within signed platform-size bounds; general custom
numeric/index protocols remain unsupported. Out-of-range native constructor
progress counts raise ValueError. Errno selection uses normal dictionary lookup,
including builtin int-subclass hash/equality/truth callbacks. Formatting uses native instance
presence, never inherited default values. Windows winerror is not exposed.

The existing POSIX os syscall wrappers translate failures into the appropriate
OSError subclass, retaining the original filename objects (both for rename).
The native-module C API provides `clover_raise_os_error` for this translation.
Argument-validation and allocation-error policies remain separate. Internal
generic exception factories allocate the correct OSError storage and retain raw args without invoking public
constructor parsing or errno-based class selection.

### Native FileIO is a bounded POSIX slice with explicit cleanup

`io.FileIO` / `_io.FileIO` provides native unbuffered binary I/O using one shared
fixed-slot/native-field layout. Descriptor ownership transfers after successful
construction. Explicit close and context-manager exit release owned descriptors;
construction failures close descriptors opened during that attempt. Abandoned
streams are intentionally not closed by reclamation or VM teardown.

Native IOBase/RawIOBase form the FileIO base hierarchy; native BufferedIOBase
is a sibling of RawIOBase with no additional fields. Its readinto/readinto1
fallbacks support bytearray destinations only. The common base provides
close/flush, capabilities, context management and state checks; generic
raw read/readall fallbacks and FileIO.readall are available; line iteration remains
deferred. RawIOBase.read rejects a count exceeding a callback-resized buffer's
current length rather than copying beyond it. Base subclasses support custom
Python initializers with the inherited native allocator. Closed
attributes use native read-only slots rather than getset descriptors. Descriptor
isatty and truncate remain unsupported; inherited isatty returns False.
Managed BufferedReader and BufferedWriter provide binary buffering. Their raw
attributes are writable, closed uses a local base flag rather than following
external raw closure, and
name/mode are omitted pending computed properties. Line methods/iteration,
truncate, text I/O and builtin open remain unimplemented. FileIO.readinto accepts
bytearray destinations using a synchronous
native borrow; general writable-buffer inputs remain deferred. FileIO accepts
string paths and stored integer descriptors; bytes
paths, path/index protocols, general write buffers, unbounded FileIO.read, custom
FileIO initializers and reinitialization are deferred. FileIO subclasses inherit
the native constructor
and can override methods and store additional attributes. See
[Native FileIO](file-io.md) for operation, validation, attribute-introspection and
platform boundaries.

### String and binary line/tab methods

`str`, `bytes` and `bytearray` support `splitlines(keepends=False)` and
`expandtabs(tabsize=8)`, including named keywords, against CPython 3.14.10.
String line splitting recognizes LF, VT, FF, CR, U+001C–U+001E, U+0085,
U+2028 and U+2029; binary splitting recognizes CR and LF only. CRLF is one
boundary, and a final boundary adds no trailing empty result. `keepends` uses
ordinary truth conversion, including callbacks, before native payload access.
A bytearray mutation in that callback is visible to the split. Existing VM
truth-conversion limits still apply, including the deliberately retained
[older NotImplemented behavior](#notimplemented-retains-its-older-boolean-behavior)
and non-integer length-result coercion limits.

Tab expansion counts stored code points for strings and bytes for binary data;
only CR and LF reset columns. Nonpositive sizes remove tabs. Sizes accept
stored ints, bools and int-subclass payloads within signed 32-bit bounds;
custom non-int `__index__` is unsupported and raises `TypeError` without calling
it. Conversion and range checks apply even to empty inputs and inputs without
tabs. Results have the corresponding base type. Exact strings without tabs
retain their identity, as do exact empty bytes; nonempty binary tab-expansion
results are fresh, and
bytearray line results are independent mutable copies. Exact immutable inputs
can be reused for a whole-input split line, including a retained final boundary.
Incidental empty/single-character interning identities are not promised.
Bytearray subclassing retains its existing unsupported status.

Shared character templates handle boundary scanning and two-pass tab expansion;
type-owned wrappers retain receivers, allocate results and reacquire payloads
after allocation. Output arithmetic is checked against native storage and tagged
length limits before allocation and raises `OverflowError` on overflow. Actual
allocation exhaustion retains the existing VM allocator policy; recoverable
`MemoryError` parity is outside this slice. No buffer exports, new conversion
protocols, object layouts or dispatch changes are introduced.


## Base85 subclass normalization

`base64.b85encode` and `base64.b85decode` consume stored bytes payloads,
including bytes subclasses, rather than invoking subclass length, indexing,
addition or conversion hooks. ASCII decoding similarly bypasses str-subclass
encoding hooks. CPython 3.14.10's managed Base85 implementation can observe
length/addition hooks and calls `str.encode` for text inputs. These exclusions
follow the existing bounded Base16/32 normalization policy; general buffers,
memoryview, custom conversion protocols and bytearray subclasses remain
unsupported. Builtin bytearray data is snapshotted before encoding, so a pad
truth callback may resize the original without changing the encoded result.
The callback runs only for an incomplete four-byte input group, after the
input has been consumed, matching CPython. No native buffer export is held.


### Ascii85 uses the same stored-payload boundary

`base64.a85encode(b, *, foldspaces=False, wrapcol=0, pad=False, adobe=False)`
and `a85decode(b, *, foldspaces=False, adobe=False, ignorechars=b' \t\n\r\v')`
share Base85's stored bytes/subclass, builtin bytearray and ASCII-text decoder
normalization policy. Subclass framing, length, indexing, addition and string
encoding hooks are bypassed; general buffers and bytearray subclasses remain
unsupported. Custom wrap-width `__index__` is outside the existing `operator.index`
subset. Existing `operator.index` (imported only when wrapping) validates
widths; range steps and slice bounds are limited by output length, so arbitrarily
large builtin integer widths work.
Supported builtin numeric widths retain reference clamping and errors.
Managed argument-binding/type-error wording can differ from CPython.

The encoder shares bounded radix arithmetic with `b85encode`; decoders remain
separate. Encoding freezes mutable input before option callbacks. `foldspaces`
is truth-tested for each nonzero word, including words other than four spaces;
`pad` is tested only for a partial group, after folding. Adobe and wrapping
options retain their separate conditional evaluations. Decoding normalizes text
before the Adobe callback, freezes binary input after that callback and framing,
and scans an immutable copy. Later fold/membership callbacks cannot alter this
copy, but `ignorechars` itself stays live and uses ordinary membership dispatch.
Its validation is lazy: digits and enabled shorthand take precedence. Default
ignored characters exclude form feed, matching CPython 3.14.10.

Fixed literals replace CPython's repeated padding bytes; no binary repetition,
struct module, Cartesian lookup table, native API, buffer export, or GC change
is required. Managed per-byte processing and result lists remain linear in the
input/output size; no native-codec throughput claim is made. Z85 and file/legacy
line-wrapping APIs remain outside this slice.
