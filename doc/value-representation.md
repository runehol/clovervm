# Value Representation

This document describes clovervm's runtime `Value` representation. `Value` is
the common currency used by bytecode registers, the accumulator, object slots,
containers, native helpers, inline caches, and future JIT code.

The implementation lives primarily in `src/value.h`. This document describes
the architectural contract; local helper types and ownership wrappers are
implementation details unless they affect that contract.

## Goals

- Represent common Python values in one machine word.
- Keep small integer tests and arithmetic cheap.
- Keep heap-pointer tests cheap.
- Distinguish refcounted and interned heap pointers without loading the object
  header.
- Reserve inline sentinels for VM protocols such as missing values and pending
  exception propagation.
- Preserve a representation that native code can pass in registers and future
  JIT code can guard, materialize, and scan.

## Word Layout

`Value` is currently a 64-bit cell with a 5-bit low tag:

```text
payload-or-pointer-bits ... low tag
XXXXXXXXXXXXXXXXXXXXXXXXXXXXTTTTT
```

The low tag bits are:

```text
bit 4: refcounted heap pointer
bit 3: interned heap pointer
bit 2: boolean special
bits 1-0: other inline special values
```

Heap allocations used as `Value` pointers must therefore be aligned to
`value_ptr_granularity`, currently 32 bytes. The allocator records whether a heap
belongs to the refcounted or interned storage class by OR-ing the corresponding
pointer tag into the returned pointer value.

## Tag Table

The important current encodings are:

| Encoding | Meaning |
| --- | --- |
| low bits `00000` | signed small integer, or SMI |
| `0x04` | `False` |
| `0x24` | `True` |
| `0x01` | `None` |
| low 32 bits `0x02` | `not_present`, with optional signed index in high 32 bits |
| `0x03` | `exception_marker` |
| low pointer bits `0x08` | interned heap pointer |
| low pointer bits `0x10` | refcounted heap pointer |

Values with both heap pointer tag bits set are invalid. Pointer-tagged values
are expected to refer to `HeapObject` records.

## Small Integers

SMIs use tag `0`. The signed integer payload is stored in the upper 59 bits:

```text
encoded = int64_value << value_tag_bits
decoded = encoded >> value_tag_bits
```

This makes `is_smi()` a single low-tag mask check. It also lets arithmetic
fast paths test whether either operand is not an SMI by OR-ing the raw words and
checking the low tag bits.

The left-shifted representation is deliberate. For simple arithmetic where both
operands are SMIs, operations such as addition and subtraction can be performed
directly on the encoded machine words with ordinary native integer
instructions. Because both operands have zero low tag bits, the result still has
zero low tag bits if it remains in range. The processor overflow flag then tells
the fast path whether the SMI operation overflowed and must be retried through
the boxed integer path.

The representable SMI range is the signed 59-bit range. Operations that may
overflow this range must leave the SMI fast path and use the appropriate slow
path or boxed integer representation.

## Inline Specials

`None`, booleans, `not_present`, and `exception_marker` are inline values, not
heap objects.

`True` and `False` use the boolean special bit. `True` additionally sets a
payload bit so inline truthiness can be tested cheaply. The first payload bit,
bit 5 or `0x20`, is the truthiness bit for truthy inline values; nonzero SMIs
also have payload bits set, while `False`, integer zero, and `None` do not.

Booleans are shaped like SMIs with the boolean tag bit added. `False` is the SMI
zero encoding plus `value_boolean_tag`; `True` is the SMI one encoding plus
`value_boolean_tag`. Clearing that one tag bit with
`value_boolean_to_integer_mask` promotes booleans to their integer encodings.
This lets equality fast paths compare SMIs and booleans with a single masked
XOR, so `True == 1` and `False == 0` do not need a slow path when neither side
is a refcounted pointer.

`None` follows the same inline truthiness convention as a falsy special value:
it is not a boolean and does not promote to an integer, but its payload bits are
clear so inline truthiness checks classify it as falsy.

`not_present` is an internal sentinel used for absent or deleted values in
places such as scopes, object slots, dictionary internals, and optional protocol
state. It can carry a signed 32-bit index in the high 32 bits. Code that only
needs to test the sentinel compares the low 32 bits against the `not_present`
tag, while code that owns the indexed form can recover the high 32-bit value.

`exception_marker` is not a Python exception object. It is a transport sentinel
meaning that VM pending exception state has been set and the caller must follow
the exception propagation protocol.

## Heap Pointers

Pointer values refer to `HeapObject` records. `HeapObject` provides the common
header used by heap records:

- `refcount`
- `layout`

Python-visible objects are a subset of heap records. Internal records such as
code objects, arrays, shape-related state, or runtime metadata may also be heap
records when they need common lifetime and scanning behavior.

The two pointer storage classes are:

- `RefcountedPtr`: ordinary heap values whose reference count participates in
  reclamation.
- `InternedPtr`: immutable or interned heap values that do not need ordinary
  refcount traffic.

The pointer tag lets `Value::storage_class()` decide whether a value is inline,
interned, or refcounted without dereferencing the pointer.

Detailed heap metadata is covered in [Object Metadata Layout](object-metadata.md)
and [Layout-ID-Driven Value Scanning and Deallocation Dispatch](layout-id-driven-scanning.md).

## Relationship To Object Layouts

The `Value` representation does not describe object fields by itself. Heap
records use layout metadata to tell the runtime which contiguous words are
scanned `Value` fields and how large the allocation is.

This separation matters:

- `Value` answers "what kind of runtime word is this?"
- heap layout metadata answers "where are the child `Value`s inside this heap
  object?"
- shapes answer "what Python-level property does this slot represent?"

Those three layers should stay distinct.

## Bytecode And Frames

The interpreter accumulator and frame registers store raw `Value`s. Loading a
value from a heap object into a register, moving between registers, or moving
between a register and the accumulator should not imply ownership transfer by
itself.

This is why the representation is paired with deferred refcounting:

- frame/register/accumulator values are live because active frames are roots;
- heap-to-heap stores are the places that retain and release references;
- safepoints must be able to discover all live frame values before reclamation.

See [Refcounting and Safepoints](refcounting-and-safepoints.md) for the full
lifetime model.

## Native Exception Transport

Fallible native VM operations return `Value`. On success, the value is the
natural result or `Value::None()` for operations with no Python result. On
failure, the native operation sets pending exception state and returns
`Value::exception_marker()`.

Callers must not treat `exception_marker` as ordinary Python data. Functions
that can propagate pending exception state should preserve the `[[nodiscard]]
Value` contract described in the exception design docs.

Python bytecode does not use `exception_marker` as the user-visible exception
mechanism. Managed Python code uses exception tables and interpreter unwind
state to find handlers, bind active exceptions, reraise, or continue unwinding.
`exception_marker` is the bridge used when native helpers, runtime operations,
or interpreter paths need to report that pending exception state has been set.

See [Exception Transport And Protocols](exception-transport-and-protocols.md).

## JIT Constraints

Future JIT code should preserve the same observable representation:

- compiled code can keep `Value`s in machine registers and spill slots;
- SMI guards should use the low-tag test;
- heap-pointer guards should use the pointer tag bits;
- deoptimization must materialize interpreter frame slots as `Value`s;
- safepoint maps must expose every live `Value` that may hold a heap pointer;
- runtime helper calls must follow the same pending-exception and lifetime
  protocols as interpreter calls.

The JIT may internally reason about unboxed values, but any value that crosses a
runtime boundary, deoptimizes, or becomes visible to the interpreter must be
materializable as a normal `Value`.

## Invariants

- A valid `Value` is either an inline value, an interned heap pointer, or a
  refcounted heap pointer.
- SMI values always have all low tag bits clear.
- Heap pointers never use the inline-special encodings.
- Interned and refcounted pointer tags are mutually exclusive.
- `exception_marker` is only a control-flow sentinel paired with pending
  exception state.
- `not_present` is an internal absence sentinel, not Python `None`.
- Heap layout metadata, not the `Value` tag, determines where child values live
  inside an object.
- At safepoints, every live heap pointer stored in a frame, object, container,
  inline cache, or future JIT spill must be discoverable.
