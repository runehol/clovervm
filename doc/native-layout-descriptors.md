# Native Layout Descriptors

## Current Model

Every VM heap record derives from `HeapObject`. `HeapObject` carries:

- `refcount`
- `HeapLifecycleState`
- `NativeLayoutId native_layout_id_`
- `uint16_t native_layout_aux_count`

`NativeLayoutId` is the descriptor dispatch key for all reclaimable heap
records, including internal records that are not Python-visible `Object`s.
Owned-reference release and opaque object-size queries are descriptor-driven;
allocation remains concrete-type driven.

The current implementation has no packed `HeapLayout` word and no legacy
expanded-header path. Native layout descriptors are the source of truth for
heap-object teardown.

## Object And SlotObject

`Object` is the Python-visible heap base. It owns Python class identity through
`Shape *shape`, but it does not physically carry attribute slot storage.

`SlotObject` derives from `Object` and owns the physical attribute storage
mechanics:

- `OverflowSlots *overflow_storage`
- inline slots located immediately after the `SlotObject` header

`Object` keeps the public storage APIs:

- `read_storage_location`
- `write_existing_storage_location`
- `write_storage_location`
- `inline_slot_base`

Those APIs downcast to `SlotObject` and assert that the object's native layout
is slot-bearing. The invariant is:

```text
Inline/Overflow StorageLocation implies the storage owner is physically a
SlotObject.
```

Current slot-bearing native layouts are:

- `Instance`
- `ClassObject`
- `Function`
- `Exception`
- `StopIteration`

Other Python-visible native objects, such as strings, tuples, lists, dicts, and
iterators, still have a `Shape`, but their fixed builtin shapes do not produce
valid inline or overflow storage plans for ordinary attribute storage.

## Release Descriptors

Release descriptors answer one question:

```text
How does reclamation release this object's owned references?
```

The release table is keyed by `NativeLayoutId` and uses these forms:

```cpp
enum class ReleaseKind : uint8_t {
    CustomDealloc,
    StaticSpan,
    DynamicSmiSpan,
    DynamicAuxSpan,
};
```

`StaticSpan`, `DynamicSmiSpan`, and `DynamicAuxSpan` describe one contiguous
word-aligned `Value` span. The reclamation loop copies each cell, clears it to
`Value::not_present()`, then releases the copied value through the active
reclamation context. It does not call ordinary hot-path `decref()` or look up
the active thread through TLS.

`CustomDealloc` is a cold escape hatch for layouts that cannot honestly be
represented as a contiguous `Value` span. A custom deallocator owns the full
`tp_dealloc`-style operation: release VM references in the right order and run
any native payload teardown. Reclamation installs the reclaimed thread as active
before invoking custom deallocators, so they may use ordinary `decref()`.

`CustomDealloc` is also the default-invalid descriptor encoding:
`custom_dealloc == nullptr` means the table entry is unfilled. Descriptor table
validation requires every real native layout except `TestOnly` to have valid
release and object-size descriptors.

## Dynamic Counts

Dynamic spans use one of two count sources:

- `DynamicSmiSpan`: an SMI `Value` member at a fixed word offset.
- `DynamicAuxSpan`: `HeapObject::native_layout_aux_count_value()`.

The final release count is:

```cpp
dynamic_count + additional_release_count
```

The dynamic count is already normalized to the number of `Value` cells visited
by the release loop. There is no per-layout scale factor in reclamation. Backing
records that store arrays of structs must store the physical `Value` cell count,
not the number of logical elements.

For `Instance`, the auxiliary count is the physical inline-slot allocation
count. `Shape` remains the semantic authority for which attributes exist and
which storage locations they use.

For backing records whose ownership has been transferred during growth, the old
backing's auxiliary count is set to zero so reclamation does not release moved
cells.

## Object Size Descriptors

Object-size descriptors answer a separate question:

```text
How many bytes does this already-allocated heap object occupy?
```

The opaque query is:

```cpp
object_size_in_bytes(const HeapObject *obj);
```

It dispatches by `NativeLayoutId` and uses:

- `ObjectSizeKind::StaticSize` for `sizeof(T)`
- `ObjectSizeKind::Custom` for type-local dynamic helpers

Allocation does not use the opaque descriptor dispatch. Allocation sites know
the concrete C++ type and use:

```cpp
sizeof(T)          // static-size layouts
T::size_for(...)   // dynamic/custom-size layouts
```

This keeps allocation type-directed while still allowing validation, accounting,
debugging, and slab policy code to query opaque object extents.

## Declaration Macros

Native layout facts are declared near each C++ class definition with class-local
macros from `native_layout_declarations.h`.

Release declarations:

```cpp
CL_DECLARE_EMPTY_VALUE_SPAN(type)
CL_DECLARE_STATIC_VALUE_SPAN(type, first_value_member, count_expr)
CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(type, base_type, own_count_expr)
CL_DECLARE_DYNAMIC_SMI_VALUE_SPAN(type, count_member, first_value_member,
                                  additional_release_count_expr)
CL_DECLARE_DYNAMIC_SMI_VALUE_SPAN_EXTENDS(type, base_type, count_member,
                                          own_additional_release_count_expr)
CL_DECLARE_DYNAMIC_AUX_VALUE_SPAN(type, first_value_member,
                                  additional_release_count_expr)
CL_DECLARE_DYNAMIC_AUX_VALUE_SPAN_EXTENDS(type, base_type,
                                          own_additional_release_count_expr)
CL_DECLARE_CUSTOM_DEALLOC(type, function)
```

Object-size declarations:

```cpp
CL_DECLARE_STATIC_OBJECT_SIZE(type)
CL_DECLARE_CUSTOM_OBJECT_SIZE(type, function)
```

The `EXTENDS` release declarations inherit the base type's release span start
and add the derived type's owned-cell count. The base release kind must be
`StaticSpan`, which keeps inherited dynamic spans from becoming ambiguous.

`Object` currently declares a static span for `shape`. `SlotObject` extends it
with `overflow_storage`. Slot-bearing derived classes extend `SlotObject`;
non-slot Python-visible classes extend `Object`.

## Registration And Lookup

Registered layouts are listed in `native_layout_registry.h` through
`CL_NATIVE_LAYOUT_REGISTRY`. That header includes the concrete type definitions
and instantiates `NativeLayoutTraits<type::native_layout>` via
`CL_DECLARE_NATIVE_LAYOUT(type)`.

Ordinary descriptor users should include `native_layout_descriptor.h`, not the
registry directly. The descriptor header builds constexpr release and object-size
tables from the registered traits.

The registered native layouts currently include:

- Python-visible object layouts: `List`, `Tuple`, `RangeIterator`,
  `TupleIterator`, `ListIterator`, `ExceptionObject`, `StopIterationObject`,
  `Function`, `Dict`, `String`, `Instance`, `CodeObject`, `ClassObject`
- internal heap records: `ValidityCell`, `Scope`, `Shape`, `OverflowSlots`,
  `RawArrayBacking`, `ValueArrayBacking`, `HeapPtrArrayBacking`

`NativeLayoutId::TestOnly` is intentionally allowed to remain invalid while the
remaining test-only uses are parked.

## Current Layout Classification

| Native layout | Release | Object size |
|---|---|---|
| `List` | `StaticSpan` | static |
| `Tuple` | `DynamicSmiSpan` from `size_value` | custom from tuple length |
| `RangeIterator` | `StaticSpan` | static |
| `TupleIterator` | `StaticSpan` | static |
| `ListIterator` | `StaticSpan` | static |
| `ExceptionObject` | `StaticSpan` via `SlotObject` | static |
| `StopIterationObject` | `StaticSpan` via `ExceptionObject` | static |
| `Function` | `StaticSpan` via `SlotObject` | static |
| `Dict` | `StaticSpan` | static |
| `String` | `StaticSpan` | custom from character count |
| `Instance` | `DynamicAuxSpan` via `SlotObject` | custom from aux count |
| `CodeObject` | `CustomDealloc` | static |
| `ClassObject` | `StaticSpan` via `SlotObject` | static |
| `ValidityCell` | empty `StaticSpan` | static |
| `Scope` | `StaticSpan` | static |
| `Shape` | `CustomDealloc` | custom from property count |
| `OverflowSlots` | `DynamicAuxSpan` | custom from capacity |
| `RawArrayBacking` | empty `StaticSpan` | custom from storage bytes |
| `ValueArrayBacking` | `DynamicAuxSpan` | custom from value-cell count |
| `HeapPtrArrayBacking` | `DynamicAuxSpan` using pointer-compatible cells | custom from cell count |

## Teardown Rules

- Static and dynamic span descriptors clear cells before releasing copied
  values.
- Static and dynamic span release uses the current reclamation context's direct
  zero-count-table path.
- Custom deallocators run cold with an active thread installed.
- C++ destructors should stay context-free; custom deallocators should drain VM
  references before invoking destructors that might otherwise release VM values.
- Descriptor release and bitmap/discovery are separate concerns: slab walking
  discovers object headers, while native layout descriptors release owned
  references after an object has already been selected for reclamation.

## Future Extension Boundary

Public C-extension compatibility may eventually need an explicit extension
descriptor kind or bridge around CPython-style `tp_dealloc`. That is not the
same as today's internal `CustomDealloc` support. The future bridge must define
resurrection, finalizer, weakref, and thread-state behavior explicitly before it
can run arbitrary extension teardown.
