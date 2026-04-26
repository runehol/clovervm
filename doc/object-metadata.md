# Object Metadata Layout

## Goal

We want a fixed-size object header that stays small in the common case, while
still supporting:

- exact location of the GC-visible `Value` region
- exact count of `Value` cells
- exact overall object size for allocation and teardown
- very large objects when compact metadata is not enough

All object pointers already need 16-byte alignment because of the tagged
pointer scheme in `Value`, so both the fixed object header and the expanded
metadata header should be designed around 16-byte allocation units.

## Fixed Object Header

The base heap-object header remains 16 bytes:

```cpp
struct HeapObject
{
    int32_t refcount;
    uint32_t layout;
    // plus padding/alignment in the concrete representation
};
```

Python-visible `Object` subclasses add native layout id, Shape, overflow
storage, and the fixed `__class__` slot after this base heap header. Native
layout identity no longer lives in a static `Klass` pointer; it is represented
by `NativeLayoutId` on `Object`.

The `layout` word describes the scanned heap layout in either:

- compact form, encoded directly in `layout`
- expanded form, indicated by a tag bit and described by a 16-byte prefix

## Common Layout Model

Both compact and expanded metadata describe the same three logical quantities:

- `object_size_in_16byte_units`
  The size of the heap object plus payload, in 16-byte units.

- `value_offset_in_words`
  The offset from the start of the heap object header to the first scanned
  `Value`, measured in 64-bit words.

- `value_count`
  The exact number of consecutive `Value` cells to scan.

This supports heap records with the following shape:

- optional raw prefix before the first scanned `Value`
- exact contiguous `Value` region
- optional raw suffix after the last scanned `Value`

The offset is measured in 64-bit words because the start of the scanned
`Value` region needs 8-byte precision. This remains useful for internal
non-`Object` records that have native fields before their scanned values.
Python-visible `Object` subclasses share inherited static layout declarations
so their value regions compose from the `Object` base. The object size is
measured in 16-byte units because all heap objects are already allocated at
16-byte granularity.

For expanded objects, the 16-byte `ExpandedHeader` prefix is allocator
overhead and is not included in `object_size_in_16byte_units`.

## Compact Metadata

Compact metadata is stored directly in the 32-bit `layout` word.

It contains:

- `expanded`
- compact `object_size_in_16byte_units`
- compact `value_offset_in_words`
- compact `value_count`

The compact bit split is:

- 1 bit for `expanded`
- 3 bits for `value_offset_in_words`
- 14 bits for `value_count`
- 14 bits for `object_size_in_16byte_units`

This is a `1:3:14:14` layout.

In compact form this gives:

- `value_offset_in_words` in the range `0..7`
- `value_count` in the range `0..16383`
- `object_size_in_16byte_units` in the range `0..16383`

So compact objects can be up to `16383 * 16` bytes, just under 256 KiB.

A suitable conceptual packing is:

```text
31            28            14             0
+-------------+-------------+--------------+
| expanded:1  | offset:3    | count:14     |
+-------------+-------------+--------------+
| object_size_in_16byte_units:14           |
+------------------------------------------+
```

Equivalently:

- bit 31: `expanded`
- bits 28..30: `value_offset_in_words`
- bits 14..27: `value_count`
- bits 0..13: `object_size_in_16byte_units`

The expected usage is:

- `value_offset_in_words` is usually `0`
- nonzero offsets are expected to be rare and small
- compact metadata should cover ordinary objects, instances, shapes, and
  medium-sized container objects

If any of the three logical quantities does not fit the compact encoding, the
object uses expanded metadata instead.

## Expanded Metadata

Expanded metadata uses:

- bit 31 of `layout` as the `expanded` tag
- bits 0..30 of `layout` as `value_offset_in_words`
- a 16-byte prefix immediately before the heap-object header for
  `object_size_in_16byte_units` and `value_count`

The expanded prefix header is:

```cpp
struct ExpandedHeader
{
    uint64_t object_size_in_16byte_units;
    uint64_t value_count;
};
```

The memory layout for an expanded object is therefore:

```text
[ ExpandedHeader ][ HeapObject / Object ][ payload... ]
```

The heap-object pointer still points to the fixed heap-object header, not to
the beginning of the allocation.

The expanded form uses:

- `object_size_in_16byte_units` as the size of the heap object plus payload
- `value_count` as the exact count of scanned `Value`s
- bits 0..30 of `layout` as the exact `value_offset_in_words`

This gives:

- `value_offset_in_words` in the range `0..2^31 - 1`
- `value_count` in the range `0..2^64 - 1`

That preserves a 16-byte expanded header while still allowing extremely large
scanned objects.

The expanded `layout` word is interpreted as:

```text
31 30                                      0
+--+----------------------------------------+
|E | value_offset_in_words:31               |
+--+----------------------------------------+
```

Equivalently:

- bit 31: `expanded`
- bits 0..30: `value_offset_in_words`

For expanded objects, the base header contains:

- the refcount in the fixed `refcount` field
- an `expanded` tag in the `layout` word

The expanded header is found by stepping back one `ExpandedHeader` from the
heap-object pointer.

## Why The Expanded Header Is Before The Object

Placing expanded metadata before the heap-object header avoids needing:

- a pointer to out-of-line metadata in `layout`
- allocator-side reverse lookup to find the end of the object
- a trailer search to find metadata at object teardown time

It also keeps the C++ object layout simple:

- fixed heap-object header at a stable address
- payload immediately after the heap object
- optional expanded metadata in a fixed position just before the heap object

## Decoding Rules

The object metadata API should expose one shared logical view regardless of
encoding:

- total object size in 16-byte units
- offset of the scanned `Value` region in 64-bit words
- exact count of scanned `Value`s

Decoding logic should be:

1. Read the `expanded` bit from the `layout` word.
2. If not expanded, decode the compact fields from `layout`.
3. If expanded, read `ExpandedHeader` from immediately before the `Object`.

The rest of the runtime should not need to care which encoding was used.

## Design Intent

This design keeps the common case compact while preserving an escape hatch for:

- very large strings or raw buffers
- very large `PyList`-like scanned objects
- unusual layouts with a larger raw prefix

The important invariant is that the metadata always describes:

- where the scanned `Value` run begins
- how many `Value`s it contains
- how large the heap object plus payload is

For expanded objects, the `ExpandedHeader` prefix is extra allocation overhead
and is not counted as part of the object size stored in metadata.

## Class Declaration Helpers

Object types should expose their layout facts as static class members so the
allocators can pick them up automatically.

The intended split is:

- fixed-size objects expose a precomputed `compact_layout`
- dynamic-layout objects expose their fixed structural invariants and provide
  the remaining values at allocation time

In all cases, classes should also expose:

- `has_dynamic_layout`
- `static_value_offset_in_words`

For fixed-size objects they should additionally expose:

- `static_value_count`
- `static_size_in_16byte_units`
- `compact_layout`

The current declaration helper family is:

```cpp
CL_DECLARE_STATIC_LAYOUT_WITH_VALUES(MyClass, first_value_member, value_count)
CL_DECLARE_STATIC_LAYOUT_NO_VALUES(MyClass)
CL_DECLARE_STATIC_LAYOUT_EXTENDS_WITH_VALUES(MyClass, BaseClass, own_value_count)

CL_DECLARE_DYNAMIC_LAYOUT_WITH_VALUES(MyClass, first_value_member)
CL_DECLARE_DYNAMIC_LAYOUT_NO_VALUES(MyClass)
CL_DECLARE_DYNAMIC_LAYOUT_EXTENDS_WITH_VALUES(
    MyClass, BaseClass, fixed_own_value_count)
```

These should define the following class members.

### `CL_DECLARE_STATIC_LAYOUT_WITH_VALUES`

This form is for fixed-size objects with a scanned `Value` run.

It should define:

- `static constexpr bool has_dynamic_layout = false`
- `static constexpr uint32_t static_value_offset_in_words`
- `static constexpr uint64_t static_value_count`
- `static constexpr uint64_t static_size_in_16byte_units`
- `static constexpr uint32_t compact_layout`

The macro should derive:

- `static_value_offset_in_words` from
  `offsetof(MyClass, first_value_member) / sizeof(Value)`
- `static_size_in_16byte_units` from `sizeof(MyClass)`, rounded up to 16-byte
  units

The caller provides:

- `first_value_member`
- `value_count`

### `CL_DECLARE_STATIC_LAYOUT_EXTENDS_WITH_VALUES`

This form is for fixed-size objects that inherit a scanned `Value` run from a
static-layout base class and append additional fixed `Value` fields.

It should define the same members as `CL_DECLARE_STATIC_LAYOUT_WITH_VALUES`.
The macro:

- asserts that the named base has static layout
- asserts that the named base is a C++ base class
- reuses the base class `static_value_offset_in_words`
- adds `own_value_count` to the base class `static_value_count`

This is the normal form for Python-visible `Object` subclasses such as
`ClassObject`: the scanned region starts in `Object`, and subclasses extend the
count.

### `CL_DECLARE_STATIC_LAYOUT_NO_VALUES`

This form is for fixed-size objects with no scanned `Value`s.

It should define:

- `static constexpr bool has_dynamic_layout = false`
- `static constexpr uint32_t static_value_offset_in_words = 0`
- `static constexpr uint64_t static_value_count = 0`
- `static constexpr uint64_t static_size_in_16byte_units`
- `static constexpr uint32_t compact_layout`

The macro should derive `static_size_in_16byte_units` from `sizeof(MyClass)`,
rounded up to 16-byte units.

### `CL_DECLARE_DYNAMIC_LAYOUT_WITH_VALUES`

This form is for variable-sized objects with a fixed starting offset for the
scanned `Value` region.

It should define:

- `static constexpr bool has_dynamic_layout = true`
- `static constexpr uint32_t static_value_offset_in_words`

The macro should derive `static_value_offset_in_words` from
`offsetof(MyClass, first_value_member) / sizeof(Value)`.

Runtime allocation supplies:

- a `DynamicLayoutSpec`

The dynamic layout spec contains:

- `object_size_in_16byte_units`
- `value_count`

### `CL_DECLARE_DYNAMIC_LAYOUT_NO_VALUES`

This form is for variable-sized objects with no scanned `Value`s.

It should define:

- `static constexpr bool has_dynamic_layout = true`
- `static constexpr uint32_t static_value_offset_in_words = 0`

Runtime allocation supplies:

- a `DynamicLayoutSpec` with `value_count = 0`

### `CL_DECLARE_DYNAMIC_LAYOUT_EXTENDS_WITH_VALUES`

This form is for variable-sized objects that inherit a static scanned prefix
from a base class and add a dynamic tail. It:

- asserts that the named base has static layout
- asserts that the named base is a C++ base class
- reuses the base `static_value_offset_in_words`
- exposes `static_fixed_value_count()` for the fixed scanned prefix

Runtime allocation supplies the final dynamic value count through the layout
spec.

## Allocator Hookup

The allocator interface should use the class metadata directly.

For fixed-size objects:

- allocate `sizeof(T)`
- initialize `layout` from `T::compact_layout`

For dynamic-layout objects:

- read `T::static_value_offset_in_words`
- combine that with the runtime `DynamicLayoutSpec`
- choose compact or expanded encoding
- initialize `layout`
- write `ExpandedHeader` if needed

This keeps the per-type structural facts attached to the type, while leaving
the compact-versus-expanded encoding decision inside the allocator.
