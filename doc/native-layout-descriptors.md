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

## Unified Layout Query Design

The separate release and object-size tables should be refactored into one
descriptor table keyed by `NativeLayoutId`. A per-object query returns the
complete scalar description needed by allocation accounting, copying, tracing,
slot repair, and deferred-refcount teardown:

```cpp
struct NativeLayoutInfo
{
    size_t allocated_size;
    size_t initialized_size;

    uint32_t value_offset_words;
    uint32_t strong_value_count;
};
```

Conceptually:

```cpp
const NativeLayoutDescriptor &descriptor =
    descriptor_for(obj->native_layout_id());
NativeLayoutInfo layout = descriptor.query(obj);
```

The descriptor lookup is static for a `NativeLayoutId`. The query resolves
dynamic sizes and counts from the object and `native_layout_aux_count`.
Consumers call the query when they need it; the result does not have to remain
live across collector phases.

### Byte Extents

`allocated_size` is the complete allocation extent, including spare capacity.
It is the number of bytes a copying collector reserves for the destination.

`initialized_size` is the contiguous prefix containing initialized object state
that is safe to read or copy. The invariant is:

```text
initialized_size <= allocated_size
```

A copying path uses both values:

```cpp
NativeLayoutInfo layout = descriptor.query(obj);
HeapObject *dst = allocate(layout.allocated_size);
memcpy(dst, obj, layout.initialized_size);
```

Spare capacity is preserved without reading uninitialized bytes. Shrinking a
copied allocation is explicit collector policy, not an implicit descriptor
behavior.

Knowing both extents does not make an object safe to copy. The space containing
the object determines whether the collector evacuates it. A moving space admits
only layouts compatible with bulk discard, as described below.

### Managed Value Span

Ordinary managed slots described by one layout form one contiguous strong,
owned span beginning at `value_offset_words`. `strong_value_count` is both the
number of references traced and rewritten by the collector and, while deferred
refcounting remains, the number released during teardown.

There is deliberately no persistent borrowed-strong heap-field policy. A heap
field that keeps its target semantically reachable owns that target during the
refcount transition. Removing deferred refcounting therefore removes the
release operation without changing the descriptor's strong-reference meaning.

A copying collector traces and repairs the strong span in one pass:

```cpp
Value *slots = reinterpret_cast<Value *>(obj) + layout.value_offset_words;
for(uint32_t idx = 0; idx < layout.strong_value_count; ++idx)
{
    slots[idx] = evacuate(slots[idx]);
}
```

Weak references are not part of this span. A weak-reference object still has
the ordinary strong `shape` slot, so its `strong_value_count` is one, while its
target is exposed only through the separate weak operation described below.

### Custom Layout Policy

Some layouts cannot be fully represented by byte extents and one contiguous
`Value` span. The static descriptor therefore retains cold custom operations:

```cpp
struct NativeLayoutDescriptor
{
    NativeLayoutInfo (*query)(const HeapObject *);

    void (*custom_trace_strong)(HeapObject *, StrongSlotVisitor);
    void (*process_weak)(HeapObject *, WeakTargetResolver);

    void (*custom_release)(HeapObject *);
    void (*destroy)(HeapObject *);
    HeapObject *(*relocate)(HeapObject *source, void *destination);
};
```

A custom strong operation supplements the ordinary span with strong owned slots
stored in C++ containers, pointer arrays, or other non-contiguous storage.
Those references are followed and rewritten during strong closure.

`process_weak` is called only after strong closure is complete. A weak target
that was otherwise reached is rewritten to its forwarding address; an
unreached target is cleared. For coherent structures such as inline caches, the
operation repairs the complete entry or clears the complete entry rather than
exposing partially valid cache state.

`custom_release` releases exceptional strong ownership while deferred
refcounting remains. `destroy` runs C++ destructors and releases native
resources after managed references have been cleared.

`relocate` is used only when an object resides in a moving space. A null
callback means the collector copies `initialized_size` bytes into the already
allocated destination. A non-null callback constructs the live destination in
that storage without allocating, safepointing, executing Python, or failing.
The callback is not an object policy: objects in stable spaces are not relocated
regardless of whether their descriptor provides it.

These are layout-level escape hatches, not per-slot policy in the common path.
The exact callback representation should follow existing low-overhead project
patterns and must not require virtual dispatch on every object.

A moving space is bulk discarded after evacuation. It must therefore reject any
layout with a non-null `destroy` callback: otherwise dead from-space objects
would have to be traversed solely to release their native resources. A custom
relocator is permitted only when dead source objects remain safely abandonable.
It cannot make ownership of external allocations safe, because an unreachable
source would still leak them.

Stable spaces may contain layouts with or without custom destruction and ignore
the relocation callback. Large objects may use stable space because of size,
while layouts such as `CodeObject` use it because destruction is required. The
object's address range or allocator metadata identifies its space; relocation
policy is not stored in the object or native-layout descriptor.

### Refactor Sequence

1. Introduce `NativeLayoutInfo` and one `NativeLayoutDescriptor` table.
2. Generate static-layout queries from the existing declaration macros.
3. Convert SMI-count and auxiliary-count layouts to return dynamic sizes and
   counts through the same query.
4. Move reclamation from `ReleaseDescriptor` to `strong_value_count` plus
   custom release and destruction.
5. Move opaque allocation-size queries from `ObjectSizeDescriptor` to
   `allocated_size`.
6. Populate and test `initialized_size` without changing allocation or copying
   behavior.
7. Add non-moving trace/update validation over the ordinary spans.
8. Add debug allocation checks that reject destructor-requiring layouts from
   moving spaces and verify custom relocation contracts.

The initial refactor preserves deferred refcounting as lifetime authority. It
consolidates metadata and exposes precise slots without requiring generation
state, barriers, or physical relocation.

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
  `RawArrayBacking`, `ValueArrayBacking`, `HeapPtrArrayBacking`, `HandleChunk`

`NativeLayoutId::TestOnly` is intentionally allowed to remain invalid while the
remaining test-only uses are parked.

## Representative Current Layout Classification

This table records the layouts most relevant to the current descriptor
mechanics; the registry remains the exhaustive source of registered IDs.

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
| `HandleChunk` | `StaticSpan` | static |

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
