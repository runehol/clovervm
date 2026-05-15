# Heap Object Metadata

This document describes the current heap object metadata shape. The older
packed `HeapLayout` word and expanded-header scheme have been removed.

## HeapObject Header

All VM heap records derive from `HeapObject`, whether or not they are
Python-visible objects:

```cpp
class HeapObject {
public:
    int32_t refcount;
    HeapLifecycleState lifecycle_state;
    NativeLayoutId native_layout_id_;
    uint16_t native_layout_aux_count;
};
```

The header is currently 8 bytes. Its fields mean:

- `refcount`: heap-owned reference count. Stack/register values are borrowed and
  do not increment this count.
- `lifecycle_state`: `Normal`, `InZct`, `Reclaiming`, or `Dead`; this prevents
  duplicate zero-count table entries and double reclamation.
- `native_layout_id_`: descriptor dispatch key for release and opaque size
  queries.
- `native_layout_aux_count`: small physical count field used by dynamic native
  layouts, such as `Instance` inline slot count and array backing cell counts.

The auxiliary count is physical layout metadata. Semantic information, such as
which attributes exist on an object, remains in `Shape`.

## Python-Visible Object Header

Python-visible heap records derive from `Object`:

```cpp
class Object : public HeapObject {
public:
    Shape *shape;
};
```

`Object` carries Python class identity and own-property shape metadata. It does
not physically carry inline attribute slots or overflow slot storage.

Slot-backed Python-visible objects derive from `SlotObject`:

```cpp
class SlotObject : public Object {
public:
    OverflowSlots *overflow_storage;
    // inline Value slots follow the SlotObject header for slot-bearing layouts
};
```

`Object` still exposes storage APIs such as `read_storage_location()` and
`inline_slot_base()`, but those APIs assert that the native layout is
slot-bearing and delegate to `SlotObject`.

Current slot-bearing layouts are `Instance`, `ClassObject`, `Function`,
`Exception`, and `StopIteration`.

## Release Metadata

Owned-reference release is not encoded in the heap object header. It is described
by native-layout release descriptors keyed by `NativeLayoutId`:

- `StaticSpan`
- `DynamicSmiSpan`
- `DynamicAuxSpan`
- `CustomDealloc`

Static and dynamic spans describe one contiguous `Value` cell range to clear and
release. Custom deallocators handle layouts whose ownership cannot be described
as a simple span, such as `CodeObject` and `Shape`.

See [Native Layout Descriptors](native-layout-descriptors.md) for the descriptor
contracts and declaration macros.

## Object Size Metadata

Allocation size is computed by concrete type:

```cpp
sizeof(T)          // static-size layouts
T::size_for(...)   // dynamic/custom-size layouts
```

For already-allocated opaque objects, the runtime can call:

```cpp
object_size_in_bytes(const HeapObject *obj)
```

That query dispatches by native layout ID through the object-size descriptor
table. It is intended for validation, accounting, debugging, and future policy
work, not for hot allocation.

## Slab Visibility

Constructed object presence is tracked by each slab's valid-object bitmap.
Successful construction marks the object's bit. Failed construction leaves the
reserved bump memory unmarked. Reclamation clears the bit after object teardown
and later asks the `GlobalHeap` to release empty, unpinned slabs.

The object header does not store a slab pointer. Slab ownership is found through
the global slab lookup table.
