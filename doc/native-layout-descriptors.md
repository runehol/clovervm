# Native Layout Descriptors

## Context

The current heap reclaimer has an interim object-specific value-span helper:
`ObjectValueSpan` is derived from the concrete `HeapObject`'s `HeapLayout`, and
reclamation clears those slots before releasing copied child values. That is
good enough for the acyclic deferred-refcount baseline, including compact and
expanded dynamic layouts, but it is not the final descriptor facade.

The next slab discovery step is described in
[Committed-Object Bitmap Reclamation](committed-object-bitmap-reclamation.md).
In short, slab candidate discovery uses a committed-object header bitmap, while
native-layout descriptors handle owned-reference release and native teardown.
Object size is a separate concern: allocation knows the concrete C++ type and
should compute sizes through type-local helpers, while reclamation/debugging may
need an opaque `HeapObject* -> object_size_in_bytes()` query.

Today, heap object metadata encodes three physical facts per object:

- total object size
- start offset of released `Value` region
- number of released `Value` members

This model is simple and fast for homogeneous object layouts, but it creates hard
layout constraints:

- released `Value` slots must be represented as one contiguous region
- object teardown semantics are coupled to metadata shape assumptions
- integrating CPython extension objects is awkward because extensions already
  define custom destruction through `tp_dealloc`

At the same time, most native object layouts are either:

- fixed-size (fully known statically), or
- dynamic-size with size derivable from object state (e.g. list/tuple payload
  length)

And we already carry a native layout identity in `Object` that can serve as a
stable dispatch key. The intended end state is to move that identity to
`HeapObject` so internal heap records use the same mechanism.

## Proposal Summary

Use `NativeLayoutId` as the primary dispatch key for releasing owned contents on
already-allocated heap objects. In this document, "descriptor" means a
VM-internal heap layout descriptor, not a Python attribute descriptor.

Keep two questions separate:

- **Release:** how does reclamation release the object's owned contents?
- **Object size:** how many bytes does this already-allocated object occupy?

The release path should be regular and predictable. The `object_size_in_bytes()`
path may be less optimized for opaque objects, because the hot allocation path
should not dispatch by native ID at all.

### Core idea

Maintain tables keyed by `NativeLayoutId`. Most native layout IDs should map to
a normalized release descriptor:

- a static contiguous `Value` span
- a dynamic contiguous `Value` span whose count is stored as an SMI `Value` at
  a fixed offset from the object header
- a dynamic contiguous `Value` span whose count is stored in the heap object's
  small auxiliary count field
- a cold custom dealloc function

Objects with no owned cells should use `StaticSpan` with count zero. This keeps
the release path from needing a separate no-values branch.

Custom dealloc callbacks are escape hatches for layouts that cannot be described by this
normal form, such as `CodeObject` or objects with non-trivial C++ destructors.
This keeps reclamation friendly to branch prediction: most heap objects flow
through one predictable metadata-descriptor path instead of a large,
unpredictable per-object switch.

The object-size query is separate:

- static object sizes can be table constants
- dynamic object sizes can be type-local helpers behind a cold descriptor
- allocation should use concrete `T::object_size_in_bytes(args...)`-style
  helpers and should not switch on `NativeLayoutId`

In this scheme, metadata is no longer the packed `HeapLayout` word. Native ID
identifies the concrete layout family, and descriptors describe only the query
being asked.

## Release Kinds

The hot release table should stay small:

```cpp
enum class ReleaseKind : uint8_t {
    StaticSpan,
    DynamicSmiSpan,
    DynamicAuxSpan,
    Custom,
};

struct ReleaseDescriptor {
    ReleaseKind kind;
    uint16_t value_offset_words;
    uint32_t static_release_count;
    uint16_t count_offset_words;
    uint32_t additional_release_count;
    void (*custom_dealloc)(HeapObject *);
};
```

`StaticSpan`, `DynamicSmiSpan`, and `DynamicAuxSpan` are the fast forms
for trivially destructible objects whose owned references are represented as one
contiguous `Value` span. The generic release loop visits cells in order, clears
each cell, and releases the copied value through the reclamation context's
direct zero-count-table path. It does not call `decref()` or look up the active
thread through TLS.

`Custom` means the type owns its whole `tp_dealloc`-style procedure. A custom
deallocator must release owned references in the correct order and run any
required native teardown, including non-trivial C++ payload destruction. It runs
while heap reclamation has installed the owning `ThreadState` as the active
thread, so it may use ordinary `decref()`/`Py_DecRef()` and route cascaded
zero-count objects into that thread's zero-count table.

C++ destructors should stay context-free. If a custom deallocator invokes a C++
destructor, it must first drain VM references that would otherwise decref from
that destructor implicitly.

For dynamic spans, the dynamic count source is either:

- an SMI `Value` at a fixed offset from the object header, for
  `DynamicSmiSpan`
- the heap object's small auxiliary count field, for `DynamicAuxSpan`

In both cases, the dynamic count is the exact number of variable `Value` cells
in the released span.

The total released count is:

```cpp
uint64_t count = dynamic_count_for(obj, desc) +
                 desc.additional_release_count;
```

The release loop remains generic:

```cpp
Value *values = value_ptr_at_words(obj, desc.value_offset_words);
for(uint64_t idx = 0; idx < count; ++idx) {
    Value value = values[idx];
    values[idx] = Value::not_present();
    release_value(value);
}
```

There is deliberately no per-layout scale in this loop. If a backing object
stores entries that contain multiple `Value` cells, the backing object stores
the already-normalized number of `Value` cells to release. The only per-layout
addition is `additional_release_count`, which accounts for fixed owned cells in
the same contiguous span.

## Object Size Query

Use `object_size_in_bytes()` terminology for the opaque size query. Avoid
exposing 16-byte units in the new API; that was an encoding detail of the
current `HeapLayout` representation and is not the right abstraction boundary
for future allocators.

Two paths should exist:

```cpp
// Hot allocation path. Concrete type and constructor arguments are known.
T::object_size_in_bytes(args...);

// Colder opaque path. The object already exists.
object_size_in_bytes(const HeapObject *obj);
```

The opaque path may dispatch by `NativeLayoutId` and call small type-local
helpers for dynamic objects. That should not contaminate the release loop or the
allocation fast path.

## Why this helps

### 1) Relaxed object layout constraints

Dynamic layouts can use ordinary object-local count fields instead of fitting
into a packed header. Native fields can coexist with released values without
forcing object size, value offset, and value count into one encoded word.

### 2) Better extension compatibility

A distinct extension layout kind can preserve CPython’s object ownership model:
custom `tp_dealloc` remains authoritative for extension types, while the VM
still uses layout-id dispatch for release and teardown coordination.

### 3) Clearer ownership of behavior

Each layout ID defines its own memory behavior contract. Release and
object sizing are separate contracts, which is easier to audit than globally
decoding one packed header format.

### 4) Better separation of mechanism and policy

- Mechanism: refcount/safepoint engine asks “how do I release this object's
  owned contents?”
- Policy: release entry answers with exact static data, a dynamic local count
  load, or an explicit cold custom dealloc function

This can simplify future features (layout specialization, inline storage
variants, extension shims).

## Costs and Risks

### 1) Indirection overhead

Dispatching by layout ID introduces table lookups and potentially callback calls.
For hot paths, this is likely acceptable if:

- static entries are fully inlineable via small IDs + contiguous table
- the normalized static and dynamic span paths handle most native objects,
  including zero-cell objects as `StaticSpan`
- custom dealloc functions are rare/cold compared to descriptor objects

Still, this is a measurable tradeoff vs reading packed fields directly.

### 2) More moving parts in runtime invariants

A packed header gives a single local source of truth. A table-driven system
adds global registration/init order concerns:

- missing registration
- duplicate IDs
- stale handler pointers
- accidental divergence between C++ object definition and table entry

Mitigation needs compile-time assertions and startup validation.

### 3) Transitional complexity

Existing code that assumes contiguous `Value` spans must migrate to descriptor-
based release. During migration, mixed modes can complicate debugging.

### 4) Extension safety boundary

Delegating to `tp_dealloc` requires strict guardrails:

- install the reclaimed thread as active before the call
- ensure object is in valid terminal state before call
- prevent double-destruction in mixed ownership flows
- define that custom deallocators own both child decrefs and native teardown

Without clear contracts, bugs here can be catastrophic.

## Recommended Architecture

## 1. Move native layout identity to `HeapObject`

Place `NativeLayoutId` in `HeapObject` (not only `Object`) so every heap entity
uses one dispatch mechanism. This avoids bifurcated paths for non-`Object`
records and keeps release logic uniform.

## 2. Define release separately from object size

Do not combine release and object sizing into one descriptor. They are asked by
different call sites and have different hotness properties.

For most native layouts, `NativeLayoutId` should look up a `ReleaseDescriptor`
rather than dispatching directly to one custom handler per heap object kind.
Custom release functions should be reserved for layouts that genuinely cannot
fit the normal static/dynamic span forms, or that require non-trivial native
destruction.

## 3. Keep compact fast paths for common layouts

For common layouts, encode direct descriptor data in a cache-hot table indexed by
`NativeLayoutId`. Avoid function pointers on these entries. Keep descriptor
dispatch biased toward a small number of common paths: static span, dynamic
span, then cold custom callbacks. A no-values object is just a static span with
count zero.

## 4. Keep semantic slots separate from physical slot storage

Every Python-visible `Object` has slots semantically because every `Object` has
a `Shape`, and `Shape` describes the object's storage locations. That does not
mean every C++ object representation must physically carry inline-slot count or
overflow-slot storage.

This split is not a prerequisite for native-layout descriptors. The descriptor
facade only needs a physical count source, a value offset, and an
`additional_release_count`. Those facts can be declared for the current
`Instance` layout just as well as for a future `SlotObject` layout.

Use a physical `SlotObject` layout for objects whose shapes can actually resolve
inline or overflow storage locations. Plain builtin objects can remain ordinary
`Object` subclasses with immutable/fixed shapes that describe zero addable
slots. For those objects, attribute add/delete is impossible and no valid
storage plan should produce an inline or overflow `StorageLocation`.

The intended split is:

- `Object` owns universal Python object metadata, especially `Shape`.
- `SlotObject` owns slot storage mechanics: overflow-slot storage and the inline
  slot payload.
- `Instance` is a dynamic-size `SlotObject`.
- `ClassObject` can also be a `SlotObject`, but because its slot count and
  cooked tail fields are fixed, it can use a static release descriptor.
- builtin non-slot objects such as strings, lists, tuples, dicts, functions,
  iterators, and exceptions remain plain `Object` subclasses with immutable
  zero-slot shapes.

`Object` may still expose semantic storage APIs such as
`read_storage_location`, `write_storage_location`, and `inline_slot_base`.
Those APIs should assert/delegate to `SlotObject` when a real inline or overflow
storage location exists. The invariant is:

```text
Inline/Overflow StorageLocation implies the storage owner is physically a
SlotObject.
```

This keeps the object model coherent while avoiding the count/overflow tax on
all builtin objects, but it should be treated as an object-model refactor rather
than as part of the descriptor migration's critical path.

Once `NativeLayoutId` moves to `HeapObject`, the header should also reserve a
small auxiliary count field. A 16-bit field is enough for the intended inline
slot count use case and avoids spending a full `Value` on every dynamic
`SlotObject`. That auxiliary count is physical layout metadata: it tells release
and opaque sizing how many inline slots were allocated. `Shape` remains the
semantic authority for which slots exist and where attributes live.

## 5. Define dynamic release count sources

Objects using a dynamic span need a cheap count source, but that source does not
always need to be an object-local SMI field. For objects that already need a
large semantic count, an SMI member is fine. For hot dynamic-size objects such
as `Instance`, the count should come from `HeapObject`'s auxiliary count field
instead of chasing through `Shape` or adding a full `Value` member.

Examples:

- `Tuple` stores its tuple element count in `size_value`; released values begin
  at `size_value`.
- `Instance` stores its physical inline-slot allocation count in the heap
  object's auxiliary count field; released values begin at the class-declared
  value offset and include the slot payload.
- erased `ValueArrayBacking` should store `value_count`, the exact number of
  `Value` cells in its payload, not an element count that requires scaling.
- `OverflowSlots` should expose a count/capacity field suitable for dynamic
  release; if it is converted to a SMI `Value`, it fits the same descriptor
  shape.

For array backings, local SMI counts are preferred because the backing has no
shape and may need counts larger than the header auxiliary field. For instances,
the auxiliary count keeps release and `object_size_in_bytes()` local to the
object while preserving `Shape` as semantic slot metadata.

Dynamic spans also need an `additional_release_count` field. SMI count members
should keep their semantic meaning, such as tuple length or backing storage
value count. The release descriptor adds `additional_release_count` for fixed
owned values in the same contiguous span, including inherited slot-header fields
or fixed tail fields:

```cpp
uint64_t dynamic_count =
    desc.kind == ReleaseKind::DynamicSmiSpan
        ? load_smi_at_words(obj, desc.count_offset_words)
        : obj->native_layout_aux_count();
uint64_t release_count = dynamic_count + desc.additional_release_count;
```

For `Instance`, the dynamic count is the heap-object auxiliary count and the
additional values are the fixed owned fields before the inline slot payload,
whatever the current C++ inheritance shape is. For `Tuple`, the dynamic count is
the tuple length and the additional value is the SMI count field itself. For
`ValueArrayBacking`, the dynamic count is the payload `Value` cell count and the
additional value is the SMI count field.

## 6. Define teardown order

Teardown processes owned cells one by one. For each owned cell, the reclamation
path may read or copy the current value, then it must clear the cell in the
object, and only then release the copied value through the reclamation path.
Clearing before release prevents partially torn-down objects from retaining
ownership if child release cascades or trips debug checks.

For `Value` spans this is a direct `Value` loop. If a future descriptor releases
raw heap-pointer cells, that should be represented explicitly or documented as a
deliberate pointer-compatible release form.

## 7. Make extension path explicit and isolated

Treat extension layouts as a first-class descriptor kind. All special behavior
lives in one bridge layer that maps VM lifecycle to `tp_dealloc` safely.

## 8. Add validation

At startup (or in debug builds):

- verify every ID has a descriptor
- verify descriptor kind invariants
- verify release expectations for known native C++ types with
  `static_assert`s and table checks

## Native Layout Traits

Keep descriptor definitions close to the C++ class definitions, but key them by
`NativeLayoutId`, which is the dispatch key used by the release table.

The design uses three declarations:

- a class-local value-span policy
- a class-local object-size policy
- one namespace-scope `CL_DECLARE_NATIVE_LAYOUT(type)` macro that instantiates
  the trait

The namespace-scope macro should have little logic in it. It should not name
private members, compute offsets, decide classifications, or depend on the old
`HeapLayout` helpers. It should simply connect `type::native_layout` to the
metadata builders for that C++ type.

For example, a static span object should look like:

```cpp
class Object : public HeapObject {
public:
    ...

    CL_DECLARE_STATIC_VALUE_SPAN(Object, shape, 2);
    CL_DECLARE_STATIC_OBJECT_SIZE(Object);
};

class Dict : public Object {
public:
    static constexpr NativeLayoutId native_layout = NativeLayoutId::Dict;
    ...

    CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(
        Dict, Object,
        decltype(hash_table)::embedded_value_count +
            decltype(entries)::embedded_value_count);
    CL_DECLARE_STATIC_OBJECT_SIZE(Dict);
};

CL_DECLARE_NATIVE_LAYOUT(Dict);
```

For a dynamic span object:

```cpp
class Tuple : public Object {
public:
    static constexpr NativeLayoutId native_layout = NativeLayoutId::Tuple;
    ...

    CL_DECLARE_DYNAMIC_SMI_VALUE_SPAN_EXTENDS(
        Tuple, Object, size_value, 1);
    CL_DECLARE_CUSTOM_OBJECT_SIZE(Tuple, Tuple::object_size_in_bytes);
};

CL_DECLARE_NATIVE_LAYOUT(Tuple);
```

The count member is the SMI count field, and the final argument is the
subclass's additional fixed owned-cell count. The inherited form takes the
contiguous release-span start from the base class and adds the base owned-cell
count into `additional_release_count`.

For `Instance`, the declaration would use the heap-object auxiliary count as
the dynamic count and the class-local first owned value as the release-span
start. It does not require `SlotObject` to exist first.

For a custom dealloc object:

```cpp
class CodeObject : public Object {
public:
    static constexpr NativeLayoutId native_layout =
        NativeLayoutId::CodeObject;
    ...

    CL_DECLARE_CUSTOM_DEALLOC(CodeObject, dealloc);
    CL_DECLARE_STATIC_OBJECT_SIZE(CodeObject);
};

CL_DECLARE_NATIVE_LAYOUT(CodeObject);
```

Migrated native layouts are listed in `native_layout_registry.h`. That header is
the intentional dependency boundary for including concrete layout classes and
invoking `CL_DECLARE_NATIVE_LAYOUT(type)`; ordinary users should include
`native_layout_descriptor.h` for descriptor lookup.

The class-local value-span macros choose the release classification:

```cpp
CL_DECLARE_EMPTY_VALUE_SPAN(type)                  // StaticSpan, count 0
CL_DECLARE_STATIC_VALUE_SPAN(type, first, count)   // StaticSpan
CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(type, base, own_count)
CL_DECLARE_DYNAMIC_SMI_VALUE_SPAN(type, count, first, additional)
CL_DECLARE_DYNAMIC_SMI_VALUE_SPAN_EXTENDS(type, base, count, own_additional)
CL_DECLARE_DYNAMIC_AUX_VALUE_SPAN(type, first, additional)
CL_DECLARE_DYNAMIC_AUX_VALUE_SPAN_EXTENDS(type, base, own_additional)
CL_DECLARE_CUSTOM_DEALLOC(type, function)          // Custom
```

These macros expand inside the class body, so they may safely compute offsets
from private members. They expose release-specific helpers; they do not encode
or decode `HeapLayout`.

`CL_DECLARE_DYNAMIC_SMI_VALUE_SPAN(type, count_member, first_value_member,
additional_release_count_expr)` expands roughly to:

```cpp
static constexpr NativeValueSpanKind native_value_span_kind =
    NativeValueSpanKind::DynamicSmi;

static constexpr uint32_t native_value_count_offset_in_words()
{
    static_assert(CL_OFFSETOF(type, count_member) % sizeof(Value) == 0,
                  "Dynamic value count must be stored on a Value boundary");
    return CL_OFFSETOF(type, count_member) / sizeof(Value);
}

static constexpr uint32_t native_value_offset_in_words()
{
    static_assert(CL_OFFSETOF(type, first_value_member) % sizeof(Value) == 0,
                  "Value span must start on a Value boundary");
    return CL_OFFSETOF(type, first_value_member) / sizeof(Value);
}

static constexpr uint64_t native_additional_release_count()
{
    return additional_release_count_expr;
}
```

`CL_DECLARE_DYNAMIC_AUX_VALUE_SPAN(type, first_value_member,
additional_release_count_expr)` is similar, but declares
`NativeValueSpanKind::DynamicAux` and does not need a count-member offset. The
dynamic count comes from `HeapObject`'s auxiliary count field.

`CL_DECLARE_STATIC_VALUE_SPAN(type, first_value_member, count_expr)` declares
`NativeValueSpanKind::Static`, `native_value_offset_in_words()`, and
`native_static_release_count()`.

The `EXTENDS` variants mirror the current legacy layout inheritance shape, but
use only native descriptor helpers. They do not call the old
`CL_DECLARE_*LAYOUT*` helpers.

`CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(type, base_type, own_count_expr)` expands
roughly to:

```cpp
static_assert(std::is_base_of_v<base_type, type>,
              "Native layout base must be a C++ base class");
static_assert(base_type::native_value_span_kind ==
              NativeValueSpanKind::Static,
              "Static inherited value spans require a static base span");

static constexpr NativeValueSpanKind native_value_span_kind =
    NativeValueSpanKind::Static;

static constexpr uint32_t native_value_offset_in_words()
{
    return base_type::native_value_offset_in_words();
}

static constexpr uint64_t native_static_release_count()
{
    return base_type::native_static_release_count() + own_count_expr;
}
```

`CL_DECLARE_DYNAMIC_SMI_VALUE_SPAN_EXTENDS(type, base_type, count_member,
own_additional_release_count_expr)` and
`CL_DECLARE_DYNAMIC_AUX_VALUE_SPAN_EXTENDS(type, base_type,
own_additional_release_count_expr)` also inherit
`base_type::native_value_offset_in_words()`. Their
`native_additional_release_count()` is:

```cpp
base_type::native_static_release_count() + own_additional_release_count_expr
```

This is how `Object`'s owned cells, currently `shape` and `overflow_storage`,
are included in every concrete `Object` subclass. A subclass declares only the
owned cells it adds; the inherited macro keeps the release span contiguous from
the base start.

The class-local object-size macros choose the opaque object-size policy:

```cpp
CL_DECLARE_STATIC_OBJECT_SIZE(type)
CL_DECLARE_CUSTOM_OBJECT_SIZE(type, function)
```

Static size expands to `NativeObjectSizeKind::Static` and
`static_object_size_in_bytes() { return sizeof(type); }`. Custom size expands
to `NativeObjectSizeKind::Custom` and a type-local
`native_object_size_in_bytes(const HeapObject *)` wrapper around the supplied
function.

The final namespace-scope macro is intentionally boring:

```cpp
template <>
struct NativeLayoutTraits<type::native_layout> {
    using object_type = type;

    static constexpr NativeLayoutId native_layout = type::native_layout;
    static constexpr const char *cpp_name = #type;
    static constexpr ReleaseDescriptor release =
        NativeLayoutReleaseDescriptorBuilder<type>::build();
    static constexpr ObjectSizeDescriptor object_size =
        NativeLayoutObjectSizeDescriptorBuilder<type>::build();
};
```

The reusable builders contain the classification logic:

```cpp
template <typename T>
struct NativeLayoutReleaseDescriptorBuilder
{
    static constexpr ReleaseDescriptor build()
    {
        if constexpr(T::native_value_span_kind ==
                     NativeValueSpanKind::Static)
        {
            return ReleaseDescriptor::static_span(
                T::native_value_offset_in_words(),
                T::native_static_release_count());
        }
        else if constexpr(T::native_value_span_kind ==
                          NativeValueSpanKind::DynamicSmi)
        {
            return ReleaseDescriptor::dynamic_smi_span(
                T::native_value_count_offset_in_words(),
                T::native_value_offset_in_words(),
                T::native_additional_release_count());
        }
        else if constexpr(T::native_value_span_kind ==
                          NativeValueSpanKind::DynamicAux)
        {
            return ReleaseDescriptor::dynamic_aux_span(
                T::native_value_offset_in_words(),
                T::native_additional_release_count());
        }
        else
        {
            return ReleaseDescriptor::custom(T::native_dealloc);
        }
    }
};
```

The object-size builder is parallel:

```cpp
template <typename T>
struct NativeLayoutObjectSizeDescriptorBuilder
{
    static constexpr ObjectSizeDescriptor build()
    {
        if constexpr(T::native_object_size_kind ==
                     NativeObjectSizeKind::Static)
        {
            return ObjectSizeDescriptor::static_size(
                T::static_object_size_in_bytes());
        }
        else
        {
            return ObjectSizeDescriptor::custom(
                T::native_object_size_in_bytes);
        }
    }
};
```

This keeps decisions next to the object definition while keeping the final
native-layout registration macro nearly mechanical.

Avoid using template container internals as native layouts. `ValueArray<T>` and
`RawArray<T>` are embedded members, not heap objects, and are released as part of
their owning object's static span. Their backing heap records should be erased
into concrete native-layout types, such as `ValueArrayBacking` and
`RawArrayBacking`, so one native ID describes one actual heap layout.

`CodeObject` should be an explicit cold custom dealloc case. Code objects
are rarely reclaimed, and their `std::vector<OwnedValue>` storage does not fit
the normal contiguous object-local span model. This is an acceptable tradeoff as
long as the descriptor says `Custom` rather than pretending the fixed inline
fields are the whole ownership story.

## Initial Classification

The exact offsets should be defined with class-local `CL_OFFSETOF` helpers, but
the intended shapes are:

| Native ID | Release | Object size |
|---|---|---|
| `String` | `StaticSpan`; `Object` fields + own count field | custom/dynamic from character count |
| `List` | `StaticSpan` | static |
| `Tuple` | `DynamicSmiSpan`; count `size_value` SMI, additional count includes fixed fields | custom/dynamic from tuple length |
| `Dict` | `StaticSpan` | static |
| `Function` | `StaticSpan` | static |
| `RangeIterator` | `StaticSpan` | static |
| `TupleIterator` | `StaticSpan` | static |
| `ListIterator` | `StaticSpan` | static |
| `CodeObject` | `Custom` | static |
| `ClassObject` | `StaticSpan`; `SlotObject` fields + fixed class fields | static |
| `Exception` | `StaticSpan` | static |
| `StopIteration` | `StaticSpan` | static |
| `Instance` | `DynamicAuxSpan`; heap-object aux count plus additional fixed fields | custom/dynamic from heap-object aux count |

Internal heap records need native IDs once `NativeLayoutId` moves to
`HeapObject`:

| Internal heap record | Release | Object size |
|---|---|---|
| `RawArrayBacking` | `StaticSpan`, count 0 | custom/dynamic from stored byte/storage count |
| `ValueArrayBacking` | `DynamicSmiSpan`; count is normalized `Value` cell count, additional count includes count field | custom/dynamic from stored count |
| `HeapPtrArrayBacking` | dynamic heap-pointer span or documented pointer-compatible span | custom/dynamic from stored count |
| `OverflowSlots` | `DynamicSmiSpan`; count from capacity/count field | custom/dynamic from capacity |
| `Scope` | `StaticSpan` | static |
| `Shape` | likely `Custom` because of transition vector ownership | custom/dynamic from property count |
| `ValidityCell` | `StaticSpan`, count 0 | static |

## Dynamic Count Fields

For dynamic spans, count offsets and value offsets are separate when the count
is an SMI field. `DynamicAuxSpan` has no count-member offset because the count
comes from `HeapObject`. In both cases, the release count is the dynamic count
plus `additional_release_count`. This lets count fields keep their semantic
meaning while still releasing one contiguous span that may include fixed header
fields, count fields, or fixed tail fields.

The intended offsets are:

| Layout | Count field | Count type | Count offset | Value offset | Additional values |
|---|---|---|---:|---:|---:|
| `Tuple` | `size_value` | `MemberTValue<SMI>` | 32 bytes / 4 words | `size_value`: 32 bytes / 4 words | fixed fields including count |
| `Instance` | heap-object auxiliary count | `uint16_t` header field | n/a | class-declared first owned value | fixed owned fields before inline slots |
| `ValueArrayBacking` | proposed `value_count` | `MemberTValue<SMI>` | 16 bytes / 2 words | `value_count`: 16 bytes / 2 words | count field |
| `HeapPtrArrayBacking` | proposed pointer-cell count | `MemberTValue<SMI>` | 16 bytes / 2 words | count field: 16 bytes / 2 words | count field |
| `OverflowSlots` | current `capacity`, or future SMI count | currently `uint32_t`; prefer `MemberTValue<SMI>` for `DynamicSmiSpan` | current capacity: 16 bytes / 2 words | count/capacity field | fixed fields included in contiguous span |

`String::count` is also a `MemberTValue<SMI>` and is part of a static release
span. It also participates in `object_size_in_bytes()`.

For backing objects, the stored count should be the already-normalized number
of cells the release loop sees. For example, a backing used by `ValueArray<Entry>`
should store the number of `Value` cells in all allocated entries, not the
number of `Entry` objects. This keeps the hot release loop free of scale factors.

## Migration Plan

1. **Introduce the descriptor-shaped API without deleting current metadata.**
   Reclamation should call this API instead of open-coding `HeapLayout`
   decoding. The existing `ObjectValueSpan` helper is the useful nucleus for
   owned-value release, but it should move behind the facade rather than become
   the facade itself.
2. **Implement the normalized release descriptor path.**
   Use it for layouts that can be described by static or dynamic spans.
   Bridge still-unmigrated layouts through existing `HeapLayout` decoding as a
   compatibility path, not as the main reclamation interface.
3. **Migrate fixed native layouts in small groups.**
   Use legacy metadata parity only where the old value region already covers
   all releasable cells. Native release descriptors are the source of truth.
4. **Add physical count sources for dynamic-span objects.**
   In particular, use the heap-object auxiliary count for dynamic-size
   `Instance` layouts, and make array backing records remember their physical
   dynamic counts directly.
5. **Add custom dealloc functions only where descriptors are insufficient.**
   `CodeObject` is expected to use this path.
6. **Optionally refactor slot-bearing objects into `SlotObject`.**
   This can happen after descriptors are in place. The class-local offset
   declarations should absorb the physical layout change without changing the
   release table model.
7. **Introduce C-extension descriptor kind + bridge.**
   Gate with focused extension lifecycle tests.
8. **Remove legacy `HeapLayout` release dependence once parity is proven.**

## Decision

This appears to be a good direction, with one caveat: keep the static-layout
and dynamic-span paths aggressively optimized and data-driven so we do not
regress hot refcount release behavior.

In short:

- **Yes** to layout-ID keyed release/teardown dispatch.
- **Yes** to a normalized release descriptor path for most native layouts.
- **Yes** to separate `object_size_in_bytes()` queries.
- **Yes** to local physical count sources for dynamic spans: SMI fields for
  backing records and the heap-object auxiliary count for hot slot objects.
- **Yes** to deferring the `SlotObject` refactor; descriptors should not depend
  on it.
- **Yes** to explicit cold custom dealloc functions where the normal metadata form is
  insufficient, especially `CodeObject`.
- **Yes** to `NativeLayoutTraits<NativeLayoutId::...>` definitions near class
  definitions.
- **Yes** to dedicated C-extension (`tp_dealloc`) bridge kind.
- **No** to replacing fast metadata-descriptor release with generic callbacks or
  a broad unpredictable switch everywhere.

A hybrid descriptor table (fast static/dynamic-span entries + explicit
custom/extension handlers) provides the flexibility we want without giving up
predictable performance for common objects.
