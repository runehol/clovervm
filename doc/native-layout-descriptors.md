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
- a cold custom release function

Objects with no owned cells should use `StaticSpan` with count zero. This keeps
the release path from needing a separate no-values branch.

Custom callbacks are escape hatches for layouts that cannot be described by this
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
    DynamicSpan,
    Custom,
};

struct ReleaseDescriptor {
    ReleaseKind kind;
    uint16_t value_offset_words;
    uint32_t static_release_count;
    uint16_t count_offset_words;
    void (*custom_release)(HeapObject *, ReclamationContext &);
};
```

`StaticSpan` and `DynamicSpan` are the fast forms for trivially destructible
objects whose owned references are represented as one contiguous `Value` span.
The generic release loop visits cells in order, clears each cell, and releases
the copied value through the reclamation path.

`Custom` means the type owns its whole release procedure. A custom release
function must release owned references in the correct order and run any required
native teardown, including a non-trivial C++ destructor.

For `DynamicSpan`, the count field is always:

- a SMI `Value`
- at a fixed offset from the object header
- the exact number of `Value` cells in the released span

The count field itself does not need to be in the released span. The release
loop can therefore remain generic:

```cpp
uint64_t count = load_smi_at_words(obj, desc.count_offset_words);
Value *values = value_ptr_at_words(obj, desc.value_offset_words);
for(uint64_t idx = 0; idx < count; ++idx) {
    Value value = values[idx];
    values[idx] = Value::not_present();
    release_value(value);
}
```

There is deliberately no per-layout scale or bias in this loop. If a backing
object stores entries that contain multiple `Value` cells, the backing object
stores the already-normalized number of `Value` cells to release.

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
  load, or an explicit cold custom release function

This can simplify future features (layout specialization, inline storage
variants, extension shims).

## Costs and Risks

### 1) Indirection overhead

Dispatching by layout ID introduces table lookups and potentially callback calls.
For hot paths, this is likely acceptable if:

- static entries are fully inlineable via small IDs + contiguous table
- the normalized `StaticSpan`/`DynamicSpan` paths handle most native objects,
  including zero-cell objects as `StaticSpan`
- custom release functions are rare/cold compared to descriptor objects

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

- ensure object is in valid terminal state before call
- prevent double-destruction in mixed ownership flows
- define exactly when VM decrefs child references vs when extension does

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
fit the normal `StaticSpan`/`DynamicSpan` form, or that require non-trivial
native destruction.

## 3. Keep compact fast paths for common layouts

For common layouts, encode direct descriptor data in a cache-hot table indexed by
`NativeLayoutId`. Avoid function pointers on these entries. Keep descriptor
dispatch biased toward a small number of common paths: static span, dynamic
span, then cold custom callbacks. A no-values object is just a static span with
count zero.

## 4. Store dynamic release counts locally

Objects using `DynamicSpan` should store the exact release-cell count locally as
an SMI `Value`.

Examples:

- `Tuple` stores its tuple element count in `size_value`; released values begin at
  `elements`.
- `Instance` should grow an `inline_slot_count` field; released values begin at
  the trailing inline slots.
- erased `ValueArrayBacking` should store `value_count`, the exact number of
  `Value` cells in its payload, not an element count that requires scaling.
- `OverflowSlots` should expose a count/capacity field suitable for dynamic
  release; if it is converted to a SMI `Value`, it fits the same descriptor
  shape.

This avoids pointer chasing through `Shape` or `ClassObject` to recover physical
layout facts. For `Instance`, the stored count should be treated as physical
allocation metadata. Class/shape metadata says what new instances should be
allocated with; the instance field says how large this object actually is.

## 5. Define teardown order

Teardown processes owned cells one by one. For each owned cell, the reclamation
path may read or copy the current value, then it must clear the cell in the
object, and only then release the copied value through the reclamation path.
Clearing before release prevents partially torn-down objects from retaining
ownership if child release cascades or trips debug checks.

For `Value` spans this is a direct `Value` loop. If a future descriptor releases
raw heap-pointer cells, that should be represented explicitly or documented as a
deliberate pointer-compatible release form.

## 6. Make extension path explicit and isolated

Treat extension layouts as a first-class descriptor kind. All special behavior
lives in one bridge layer that maps VM lifecycle to `tp_dealloc` safely.

## 7. Add validation

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
`HeapLayout` helpers. It should simply connect `type::native_layout_id` to the
metadata builders for that C++ type.

For example, a static span object should look like:

```cpp
class Dict : public Object {
public:
    static constexpr NativeLayoutId native_layout_id = NativeLayoutId::Dict;
    ...

    CL_DECLARE_STATIC_VALUE_SPAN(
        Dict, hash_table,
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
    static constexpr NativeLayoutId native_layout_id = NativeLayoutId::Tuple;
    ...

    CL_DECLARE_DYNAMIC_VALUE_SPAN(Tuple, size_value, elements);
    CL_DECLARE_DYNAMIC_OBJECT_SIZE(Tuple, Tuple::object_size_in_bytes);
};

CL_DECLARE_NATIVE_LAYOUT(Tuple);
```

For a custom release object:

```cpp
class CodeObject : public Object {
public:
    static constexpr NativeLayoutId native_layout_id =
        NativeLayoutId::CodeObject;
    ...

    CL_DECLARE_CUSTOM_VALUE_RELEASE(CodeObject, release_contents);
    CL_DECLARE_STATIC_OBJECT_SIZE(CodeObject);
};

CL_DECLARE_NATIVE_LAYOUT(CodeObject);
```

The class-local value-span macros choose the release classification:

```cpp
CL_DECLARE_EMPTY_VALUE_SPAN(type)                  // StaticSpan, count 0
CL_DECLARE_STATIC_VALUE_SPAN(type, first, count)   // StaticSpan
CL_DECLARE_DYNAMIC_VALUE_SPAN(type, count, first)  // DynamicSpan
CL_DECLARE_CUSTOM_VALUE_RELEASE(type, function)    // Custom
```

These macros expand inside the class body, so they may safely compute offsets
from private members. They expose release-specific helpers; they do not encode
or decode `HeapLayout`.

`CL_DECLARE_DYNAMIC_VALUE_SPAN(type, count_member, first_value_member)` expands
roughly to:

```cpp
static constexpr NativeValueSpanKind native_value_span_kind =
    NativeValueSpanKind::Dynamic;

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
```

`CL_DECLARE_STATIC_VALUE_SPAN(type, first_value_member, count_expr)` is the same
idea, but declares `NativeValueSpanKind::Static`,
`native_value_offset_in_words()`, and `native_static_value_count()`.

The class-local object-size macros choose the opaque object-size policy:

```cpp
CL_DECLARE_STATIC_OBJECT_SIZE(type)
CL_DECLARE_DYNAMIC_OBJECT_SIZE(type, function)
```

Static size expands to `NativeObjectSizeKind::Static` and
`static_object_size_in_bytes() { return sizeof(type); }`. Dynamic size expands
to `NativeObjectSizeKind::Dynamic` and a type-local
`native_object_size_in_bytes(const HeapObject *)` wrapper around the supplied
function.

The final namespace-scope macro is intentionally boring:

```cpp
template <>
struct NativeLayoutTraits<type::native_layout_id> {
    using object_type = type;

    static constexpr NativeLayoutId native_layout_id = type::native_layout_id;
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
                T::native_static_value_count());
        }
        else if constexpr(T::native_value_span_kind ==
                          NativeValueSpanKind::Dynamic)
        {
            return ReleaseDescriptor::dynamic_span(
                T::native_value_count_offset_in_words(),
                T::native_value_offset_in_words());
        }
        else
        {
            return ReleaseDescriptor::custom(T::native_custom_release);
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

`CodeObject` should be an explicit cold custom release case. Code objects
are rarely reclaimed, and their `std::vector<OwnedValue>` storage does not fit
the normal contiguous object-local span model. This is an acceptable tradeoff as
long as the descriptor says `Custom` rather than pretending the fixed inline
fields are the whole ownership story.

## Initial Classification

The exact offsets should be defined with class-local `CL_OFFSETOF` helpers, but
the intended shapes are:

| Native ID | Release | Object size |
|---|---|---|
| `String` | `StaticSpan`, count 0 | custom/dynamic from character count |
| `List` | `StaticSpan` | static |
| `Tuple` | `DynamicSpan`; count `size_value` SMI, values `elements` | custom/dynamic from tuple length |
| `Dict` | `StaticSpan` | static |
| `Function` | `StaticSpan` | static |
| `RangeIterator` | `StaticSpan` | static |
| `TupleIterator` | `StaticSpan` | static |
| `ListIterator` | `StaticSpan` | static |
| `CodeObject` | `Custom` | static |
| `ClassObject` | `StaticSpan` | static |
| `Exception` | `StaticSpan` | static |
| `StopIteration` | `StaticSpan` | static |
| `Instance` | `DynamicSpan`; proposed `inline_slot_count` SMI, values trailing inline slots | custom/dynamic from inline slot count |

Internal heap records need native IDs once `NativeLayoutId` moves to
`HeapObject`:

| Internal heap record | Release | Object size |
|---|---|---|
| `RawArrayBacking` | `StaticSpan`, count 0 | custom/dynamic from stored byte/storage count |
| `ValueArrayBacking` | `DynamicSpan`; count is normalized `Value` cell count | custom/dynamic from stored count |
| `HeapPtrArrayBacking` | dynamic heap-pointer span or documented pointer-compatible span | custom/dynamic from stored count |
| `OverflowSlots` | `DynamicSpan`; count from capacity/count field | custom/dynamic from capacity |
| `Scope` | `StaticSpan` | static |
| `Shape` | likely `Custom` because of transition vector ownership | custom/dynamic from property count |
| `ValidityCell` | `StaticSpan`, count 0 | static |

## Dynamic Count Fields

For `DynamicSpan`, count offsets and value offsets are separate. Do not force
the count field into the released span just to reuse a value offset. The release
loop loads the count as an SMI `Value`, then releases exactly that many cells
beginning at `value_offset_words`.

The intended offsets are:

| Layout | Count field | Count type | Count offset | Value offset |
|---|---|---|---:|---:|
| `Tuple` | `size_value` | `MemberTValue<SMI>` | 32 bytes / 4 words | `elements`: 40 bytes / 5 words |
| `Instance` | proposed `inline_slot_count` | `MemberTValue<SMI>` | 32 bytes / 4 words | proposed inline slots: 40 bytes / 5 words |
| `ValueArrayBacking` | proposed `value_count` | `MemberTValue<SMI>` | 16 bytes / 2 words | `elements`: 24 bytes / 3 words |
| `HeapPtrArrayBacking` | proposed pointer-cell count | `MemberTValue<SMI>` | 16 bytes / 2 words | `elements`: 24 bytes / 3 words |
| `OverflowSlots` | current `capacity`, or future SMI count | currently `uint32_t`; prefer `MemberTValue<SMI>` for `DynamicSpan` | current capacity: 16 bytes / 2 words | `slots`: 24 bytes / 3 words |

`String::count` is also a `MemberTValue<SMI>` at 32 bytes / 4 words, but it is
for `object_size_in_bytes()`, not release, if `String` is classified as
a zero-count `StaticSpan`.

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
   Use it for layouts that can be described by `StaticSpan` or `DynamicSpan`.
   Bridge still-unmigrated layouts through existing `HeapLayout` decoding as a
   compatibility path, not as the main reclamation interface.
3. **Migrate fixed native layouts in small groups.**
   Validate descriptor parity against current metadata as entries are added.
4. **Add local count fields for dynamic-span objects.**
   In particular, make `Instance` and array backing records remember their
   physical release counts directly.
5. **Add custom release functions only where descriptors are insufficient.**
   `CodeObject` is expected to use this path.
6. **Introduce C-extension descriptor kind + bridge.**
   Gate with focused extension lifecycle tests.
7. **Remove legacy `HeapLayout` release dependence once parity is proven.**

## Decision

This appears to be a good direction, with one caveat: keep the static-layout
and dynamic-span paths aggressively optimized and data-driven so we do not
regress hot refcount release behavior.

In short:

- **Yes** to layout-ID keyed release/teardown dispatch.
- **Yes** to a normalized release descriptor path for most native layouts.
- **Yes** to separate `object_size_in_bytes()` queries.
- **Yes** to local SMI count fields for dynamic spans.
- **Yes** to explicit cold custom release functions where the normal metadata form is
  insufficient, especially `CodeObject`.
- **Yes** to `NativeLayoutTraits<NativeLayoutId::...>` definitions near class
  definitions.
- **Yes** to dedicated C-extension (`tp_dealloc`) bridge kind.
- **No** to replacing fast metadata-descriptor release with generic callbacks or
  a broad unpredictable switch everywhere.

A hybrid descriptor table (fast static/dynamic-span entries + explicit
custom/extension handlers) provides the flexibility we want without giving up
predictable performance for common objects.
