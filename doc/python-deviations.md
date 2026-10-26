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

### `bytearray` provides a bounded mutable byte container

`bytearray` supports empty/zero-filled construction, stored bytes/bytearray
copies, integer iterables, length/truth, indexing and slicing, item/slice
assignment and deletion, `append`, `extend`, `clear`, repr/str, all six comparisons
with bytes/bytearray, and live integer iteration. `bytes(bytearray)` copies into
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

## Numeric Semantics

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

Remaining families outside this slice are `UnicodeError` and its specialized
subclasses,
`SystemExit`, `GeneratorExit`, `StopAsyncIteration`, `BaseExceptionGroup`, and
`ExceptionGroup`. Chaining, full traceback support, and signal handling remain
separate work. Supplying these classes does not complete specialized exception
behavior.

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

Native IOBase/RawIOBase now form the FileIO base hierarchy. Their first slice
provides close/flush, capabilities, context management and state checks; generic
raw read/readall fallbacks and FileIO.readall are available; line iteration remains
deferred. RawIOBase.read rejects a count exceeding a callback-resized buffer's
current length rather than copying beyond it. Base subclasses support custom
Python initializers with the inherited native allocator. Closed
attributes use native read-only slots rather than getset descriptors. Descriptor
isatty and truncate remain unsupported; inherited isatty returns False.
Buffering, text I/O and builtin open remain
unimplemented. FileIO.readinto accepts bytearray destinations using a synchronous
native borrow; general writable-buffer inputs remain deferred. FileIO accepts
string paths and stored integer descriptors; bytes
paths, path/index protocols, general write buffers, unbounded FileIO.read, custom
FileIO initializers and reinitialization are deferred. FileIO subclasses inherit
the native constructor
and can override methods and store additional attributes. See
[Native FileIO](file-io.md) for operation, validation, attribute-introspection and
platform boundaries.
