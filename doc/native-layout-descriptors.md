# Native Layout Descriptors

| Field | Value |
|---|---|
| Document type | Architecture contract |
| Status | Accepted |
| Implementation | Partial; unified descriptor registry implemented, initialized-extent/formula query remains future work |
| Scope | Descriptor-driven heap record sizing, strong scanning, weak processing, reference release, and teardown |
| Owning layers | Heap metadata, runtime object layouts, allocator, and reclamation |
| Validated against | Descriptor-unification working tree based on `b45b29e1` (2026-10-31); descriptor sections checked, unrelated layout prose unchanged |
| Supersedes | N/A |

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

## Unified Descriptor Registry

One NativeLayoutId-indexed table stores the existing two descriptor parts:

```cpp
struct NativeLayoutDescriptor
{
    ReleaseDescriptor release;
    ObjectSizeDescriptor object_size;
};

const NativeLayoutDescriptor &
native_layout_descriptor_for(NativeLayoutId);
```

Reclamation, strong/weak enumeration, and size queries use this single lookup.
The separate release/object-size tables and lookup functions are removed, not
kept as compatibility wrappers. The existing type-directed builders, formulas,
and callbacks remain unchanged. Combined completeness validation requires both
parts to be valid and every custom-release layout to have a custom scanner.
Invalid and TestOnly entries remain invalid; TestOnly is excluded from the
production completeness requirement.

The composite is 80 bytes on the current 64-bit targets: 56 bytes of release
metadata plus 24 bytes of object-size metadata, with no additional padding.
The logical table payload is unchanged from the sum of the previous tables.
This is static metadata, not a per-object header change. It is a preparatory
registry refactor, not implementation of initialized extents or the final
compact formula representation below; no performance improvement is implied.

Descriptor-unification validation (2026-10-31): all 113 focused descriptor,
reclamation, reference-slot, weak-processing, and GC-adapter tests passed.
`ninja -C build-debug all check` passed with 2,301 tests passing, and
`ninja -C build-release all check` passed with 2,290 tests passing. Each full
suite skipped three tests because runtime JIT tiering is disabled and reported
one disabled test. These checks validate the registry migration, not production
copying collection.

## Release Descriptors

Release descriptors answer one question:

```text
How does reclamation release this object's owned references?
```

The unified entry's release part uses these forms:

```cpp
enum class ReleaseKind : uint8_t {
    CustomRelease,
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

`CustomRelease` is a cold escape hatch for layouts that cannot honestly be
represented as a contiguous `Value` span. Its `custom_release` callback releases
managed ownership. Reclamation installs the reclaimed thread as active before
invoking it, so it may use ordinary `decref()`.

The descriptor's independent, optional `destroy` callback tears down native
resources after ordinary or custom reference release. It must not release any
further managed references. Owning C++ containers must therefore be emptied by
the release phase, before their destructors run. Direct `Member` fields retain
their existing explicit-release behavior; they do not release on destruction.
Release and destruction each run once, then reclamation retires the allocation.
Ordinary spans can have a destruction callback too; custom destruction is not a
reference-enumeration policy. Independent destruction grew `ReleaseDescriptor`
from 32 to 40 bytes, strong scanning made it 48 bytes, and slice 3c's weak
callback makes it 56 bytes. These are static table entries, not per-object
metadata.

`CustomRelease` is also the default-invalid descriptor encoding:
`custom_release == nullptr` means the table entry is unfilled. Descriptor table
validation requires every real native layout except `TestOnly` to have valid
release and object-size descriptors.

`resolve_ordinary_reference_span(object, descriptor)` resolves the three
ordinary forms to an `OrdinaryReferenceSpan` containing a 64-bit
`value_offset_words` and a 64-bit `value_count`. It reads count metadata without
reading, clearing, or releasing the reference cells. Reclamation consumes the
result using its existing clear-and-release loop.

The query describes one contiguous span inside the object's allocation. An
empty ordinary span is a supported result with a zero count. Custom handling
returns `std::nullopt`, which does not mean the object has no references.
Custom scanning and destruction are independent of reference release, as in the
unified descriptor design below.

`visit_strong_reference_slots(object, visitor)` visits this object's ordinary
span, if supported, then its optional `custom_trace_strong` callback. The
borrowed `StrongSlotVisitor` is an `absl::FunctionRef<void(HeapReferenceSlot)>`;
it is not retained. An ordinary span and a supplemental custom callback may
coexist. Every registered custom-release layout must supply a custom scanner;
table validation checks this structural requirement, not semantic completeness
of per-layout reference coverage.

The three custom scanners expose:

- `CodeObject`: shape, overflow storage, direct managed members, keyword-remap
  backing handles, native constant-table entries, and owned keyword names in
  expanded-call cache plans. VM-array wrappers expose their backing handles;
  their elements are visited later when those backing objects are scanned.
- `Shape`: class, every owned descriptor name including latent descriptors,
  and each transition's owned name and successor shape.
- `JitCodeObject`: only its tagged constant-pool prefix, not untagged pool data
  or executable addresses.

Enumeration is not recursive. Distinct cells containing the same pointer are
visited separately. Raw weak-cache references and `Shape::previous_shape` are
excluded and handled by separate weak processing. Native pointers such as
CodeObject's `compilation_unit` are not heap-reference cells; roots owned by native runtime
structures require a separate audit. These scans do not enter production
reclamation or imply relocation safety. The per-layout field inventory and
remaining movement prerequisites are recorded in the
[Native Layout Reference Audit](native-layout-reference-audit.md).

Neither enumeration nor relocation writes adjust refcounts or ZCT membership.
The visitor must not allocate managed objects, safepoint, execute Python, or
change container structure during a visit. Object and slot storage stay valid
until enumeration completes. Tests simulate substitutions and restore the
ownership graph before refcount teardown.

`visit_weak_reference_units(object, visitor)` invokes an optional descriptor
enumeration hook, exposing compact borrowed repair units. Shape exposes its
back-link; CodeObject exposes individual IC entries, not an owner-wide late
rescan. A borrowed `WeakResolver` reports Resolved, Pending, or Dead. Early
repair leaves unresolved fields unchanged and defers only their exact unit;
after strong closure, late resolution reports unforwarded source targets dead.
It must not mark or dereference dead inputs, allocate managed objects, or
reenter the VM. Unit addresses must remain valid through late repair. These
hooks do not enable production collection or reclamation.

Shape repairs or clears its non-owning back-link. CodeObject processes every
cache family, respecting optional nulls, tagged immediates, invalid/inline shape
keys, and active plan fields. Any dead active component resets the whole entry;
native handler pointers and scalar plans are not sent to the resolver. Successful
pointer repair is RC-neutral. Clearing an expanded-call entry releases its owned
keyword-name plan through ordinary ownership rules: with RC enabled an active
thread must be installed, and zero-count children may enter the ZCT, but no
reclamation runs inside the callback. Those names already participated in
strong closure. Tests use fake liveness maps with physically live objects;
Python-visible weakref objects and production collector integration are not
part of this API implementation.

`visit_ordinary_reference_slots(object, span, visitor)` enumerates each cell of
a resolved span through a borrowed `HeapReferenceSlot`. This includes immediate
values and null pointers: `load_pointer()` returns null for those cells.
Tagged references and raw heap pointers use the same pointer bits in the current
representation. The view uses byte copies rather than accessing raw fields as
`Value &` or invoking wrapper assignment operators.

`rewrite_pointer()` is a collector-only operation on an existing reference. Its
non-null replacement must preserve pointer storage class and logical identity.
It performs no refcount operations, barriers, allocation, or safepoints. The
supplied span must describe initialized storage within the object, and that
storage must remain valid throughout enumeration. Custom scanners may later
use the same view for references outside the ordinary span.

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

For `Instance`, the auxiliary count is the initialized inline-slot prefix.
Unused inline capacity remains uninitialized. The instance's current `Shape`
describes inline capacity and attribute storage locations; its slot frontier
also describes the initialized inline prefix after a completed mutation.

For ValueArrayBacking and HeapPtrArrayBacking whose ownership has been
transferred during growth, the old SMI owning-cell count is set to zero so RC
reclamation does not release moved cells. This is not an auxiliary-count field.
After the tracing cutover, retired backings are unreachable garbage; this
zeroing is removed rather than becoming a copying-GC requirement.

## Object Size Descriptors

Object-size descriptors answer a separate question:

```text
How many bytes does this object's current layout require, including spare capacity?
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
sizeof(T)                     // static-size layouts
T::allocation_size_for(...)   // dynamic/custom-size layouts
```

This keeps allocation type-directed while still allowing validation, accounting,
debugging, and slab policy code to query opaque object extents.

These queries describe the allocation requirement of the current layout. They
do not report allocator rounding, an older oversized reservation retained after
a future capacity shrink, or the initialized prefix safe to copy. Collector
policy chooses any different evacuation destination size separately.

## Unified Layout Query Design

The existing composite registry is the first step. The later query representation
should primarily contain compact formulas, not one indirect query function per
layout. An inline query
evaluates those formulas and returns the complete scalar description needed by
allocation accounting, copying, tracing, slot repair, and deferred-refcount
teardown:

```cpp
struct NativeLayoutInfo
{
    size_t allocated_size;
    size_t initialized_size;

    uint64_t value_offset_words;
    uint32_t strong_value_count;
};
```

The current descriptors establish the expected distribution:

- most layouts have a static object size and static strong span;
- `Tuple`, `ValueArrayBacking`, and `HeapPtrArrayBacking` read a dynamic count
  from an SMI field;
- `Instance` uses its shape for allocated capacity and
  `native_layout_aux_count` for its initialized inline prefix and release count;
- `OverflowSlots` uses `native_layout_aux_count` for release and its own
  capacity field for object size;
- most existing custom size helpers are `base + count * stride` formulas;
- `CodeObject`, `Shape`, and `JitCodeObject` have custom reference release and
  separate native destruction.

The common representation should preserve those LUT and arithmetic paths:

```cpp
enum class CountFieldKind : uint8_t
{
    LayoutAux,
    SmiField,
    IntegerField,
};

struct CountFieldSource
{
    uint16_t field_offset_words;
    CountFieldKind field_kind;
};

struct CountFormula
{
    uint32_t addend;
    uint16_t field_offset_words;
    uint8_t multiplier;
    CountFieldKind field_kind;
};

struct ExtentDescriptor
{
    size_t base_size;
    size_t element_size;
    CountFieldSource allocated_field;
    CountFieldSource initialized_field;
};

struct StrongSpanDescriptor
{
    uint64_t value_offset_words;
    CountFormula count;
};
```

Every count uses the same affine form:

```text
count = multiplier * field_value + addend
```

A static count `c` uses `multiplier = 0` and `addend = c`; no field is read.
A dynamic SMI, integer, or auxiliary count normally uses `multiplier = 1` and
the existing additional count as `addend`. There is no separate constant source
or constant-field encoding. The compact formula is eight bytes. Evaluation
checks that the resolved count fits the descriptor system's `uint32_t` object
span limit before returning `NativeLayoutInfo`.

The inline common query performs one `NativeLayoutId`-indexed table lookup,
loads only the selected count fields, and applies straight-line arithmetic:

```cpp
allocated_size = extent.base_size + extent.element_size *
                 read(extent.allocated_field, obj);
initialized_size = extent.base_size + extent.element_size *
                   read(extent.initialized_field, obj);
strong_value_count = evaluate(strong_span.count, obj);
```

Static layouts require no object-dependent count load. Auxiliary and SMI
layouts add one field load and arithmetic. Only a descriptor with a non-null
`custom_query` takes an indirect call.

### Byte Extents

`allocated_size` is the complete allocation extent, including spare capacity.
It is the number of bytes a copying collector reserves for the destination.

`initialized_size` is the contiguous prefix containing initialized object state
that is safe to read or copy. The invariant is:

```text
initialized_size <= allocated_size
```

A copying path uses both values returned by the inline query:

```cpp
NativeLayoutInfo layout = native_layout_info(obj);
HeapObject *dst = allocate(layout.allocated_size);
memcpy(dst, obj, layout.initialized_size);
```

Spare capacity is preserved without reading uninitialized bytes. Shrinking a
copied allocation is explicit collector policy, not an implicit descriptor
behavior.

Knowing both extents does not make an object safe to copy. The space containing
the object determines whether the collector evacuates it. A moving space admits
only layouts compatible with bulk discard, as described below.

Allocated and initialized extents share one byte formula:

```text
extent = base_size + element_size * field_value
```

They may read different fields. A capacity-backed object normally uses capacity
for `allocated_field` and logical size for `initialized_field`. An object that
initializes its complete capacity uses the same field for both. A static extent
uses `base_size = sizeof(T)` and `element_size = 0`, so neither field is read. A
layout whose allocated and initialized extents do not share the same base and
element sizes uses the rare custom query.

### Extent Source Invariant

Extent metadata may come from the object, the descriptor LUT, or the object's
shape. Shape-derived layout queries must work before ordinary strong-reference
repair: source metadata must remain readable during evacuation, including when
the shape has already been forwarded. The collector's forwarding representation
and metadata access must preserve this contract. It must not require recursively
tracing a shape's outgoing references merely to determine an instance's extent.

`Instance` retains lazy inline-slot initialization:

- allocation calls `Instance::allocation_size_for(ClassObject *)`, which obtains
  inline capacity from the class's instance root `Shape`;
- `Instance::object_size_in_bytes()` uses the instance's current shape's inline
  capacity;
- the auxiliary count grows as inline slots are lazily initialized,
  so it records initialized usage rather than physical allocation capacity.

The allocated-size helper reports full capacity from construction onward, while
reference release uses only the initialized inline prefix. The future unified
query will preserve that distinction for copying:

```text
allocated_size   = sizeof(Instance) + sizeof(Value) * shape.inline_capacity
initialized_size = sizeof(Instance) + sizeof(Value) * aux_count
strong_count     = inherited strong cells + aux_count
```

After a completed mutation, `aux_count` agrees with
`min(shape.next_slot_index, shape.inline_capacity)`. Deleted slots remain
initialized as `not_present`; deletion does not reduce the frontier. A shape
change and initialization of a newly claimed slot must not expose an invalid
initialized span at a collection safepoint.

### Future Instance Capacity Shrinking

A class may initially reserve generous inline capacity, then use allocation
profiling to choose a smaller capacity. At the shrink point, the new capacity
must cover the maximum inline-slot frontier already reached by its instances.
Every existing inline storage location therefore remains inline. Subsequent
attribute growth beyond the reduced capacity uses overflow storage.

The reduced capacity must govern subsequent shape transitions, including
transitions used by instances allocated before the shrink. Cached transitions
must not allow those instances to claim discarded inline capacity. The exact
profiling threshold and shape-update mechanism remain future implementation
work.

Existing allocations can retain their excess bytes until evacuation. A copying
collector then allocates the reduced extent and copies only the initialized
prefix; no existing values need to move between inline and overflow storage.
Allocator accounting or source-space walking must distinguish the original
reservation from the reduced destination extent when necessary. Shrinking
must be an explicit evacuation policy, rather than silently conflating those
two sizes in the ordinary layout query.

Lazy initialization remains the initial policy. Once capacity shrinking exists,
measure whether clearing the full, usually smaller capacity is cheap enough to
simplify initialized-prefix tracking.

### Layout Audit

The layout audit should flag any native ID with:

- managed metadata dependencies without a defined forwarding-safe query path;
- allocation capacity that cannot be recovered through its declared source;
- uninitialized capacity included in a copied or traced span;
- logical size conflated with physical capacity;
- a count mutated during backing ownership transfer;
- a count that cannot fit in `native_layout_aux_count`;
- undocumented alignment, rounding, or minimum-capacity rules.

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
for(size_t idx = 0; idx < layout.strong_value_count; ++idx)
{
    slots[idx] = evacuate(slots[idx]);
}
```

Weak references are not part of this span. A weak-reference object still has
the ordinary strong `shape` slot, so its `strong_value_count` is one, while its
target is exposed only through the separate weak operation described below.

### Custom Layout Policy

Some layouts cannot be fully represented by byte extents and one contiguous
`Value` span. The later formula-based descriptor is intended to retain cold
custom operations. This sketch is its target shape, not the current composite
registry's field list:

```cpp
struct NativeLayoutDescriptor
{
    ExtentDescriptor extent;
    StrongSpanDescriptor strong_span;

    NativeLayoutInfo (*custom_query)(const HeapObject *);

    void (*custom_trace_strong)(HeapObject *, StrongSlotVisitor);
    void (*visit_weak_units)(HeapObject *, WeakUnitVisitor);

    void (*custom_release)(HeapObject *);
    void (*destroy)(HeapObject *);
    HeapObject *(*relocate)(HeapObject *source, void *destination);
};
```

A custom strong operation supplements the ordinary span with strong owned slots
stored in C++ containers, pointer arrays, or other non-contiguous storage.
Those references are followed and rewritten during strong closure.

`visit_weak_units` enumerates address/callback handles. Their callbacks can
repair already-resolved fields early and report Pending without skipping other
active fields. After strong closure the remaining units are repaired with the
late resolver: surviving targets are rewritten and unreached targets are dead.
For coherent structures such as inline caches, one dead dependency clears the
complete entry and payload. No mutator observes a partially repaired entry.
See [Weak Repair Units](gc-weak-repair-units.md) for lifetime and reset rules.

`custom_release` releases exceptional strong ownership while deferred
refcounting remains. `destroy` runs C++ destructors and releases native
resources after managed references have been cleared.

Reference enumeration and destruction are independent capabilities. An ordinary
strong span, additional custom strong scanning, and custom destruction may all
be present on the same layout. The current custom-deallocator layouts
`CodeObject`, `Shape`, and `JitCodeObject` must each expose their strong
references for scanning and rewriting, plus separate weak processing where
applicable. Their eventual non-moving placement does not exempt references to
movable objects from repair. For `JitCodeObject`, scanning includes the tagged
constant-pool slots outside the object's own allocation.

`relocate` is used only when an object resides in a moving space. A null
callback means the collector copies `initialized_size` bytes into the already
allocated destination. A non-null callback constructs the live destination in
that storage without allocating, safepointing, executing Python, or failing.
The callback is not an object policy: objects in stable spaces are not relocated
regardless of whether their descriptor provides it.

These are layout-level escape hatches, not per-slot policy in the common path.
The exact callback representation should follow existing low-overhead project
patterns and must not require virtual dispatch on every object.

All callback fields are nullable. A null `custom_query` selects the LUT/formula
path. A null custom strong callback means the ordinary strong span is complete.
A null weak callback means the object is not added to weak work. Null release,
destroy, and relocate callbacks likewise select the ordinary behavior or no
operation appropriate to the containing space.

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

The [GC implementation plan](generational-copying-gc-implementation-plan.md)
breaks this target into incremental runtime stages, starting with ordinary
span resolution consumed by reclamation before consolidating the full table.

1. Introduce `NativeLayoutInfo`, affine count and extent formulas, and one
   `NativeLayoutDescriptor` table.
2. Generate static LUT entries from the existing declaration macros.
3. Convert SMI-count and auxiliary-count layouts to formula entries without
   indirect query calls.
4. Move reclamation from `ReleaseDescriptor` to `strong_value_count` plus
   custom release and destruction.
5. Move opaque object-size queries from `ObjectSizeDescriptor` to
   `allocated_size`.
6. Audit allocated, initialized, and strong count sources for every native ID;
   retain lazy instance initialization and its shape-derived allocated size,
   and establish forwarding-safe access to layout metadata.
7. Populate and test `initialized_size` without changing allocation or copying
   behavior.
8. Add non-moving trace/update validation over the ordinary spans.
9. Add debug allocation checks that reject destructor-requiring layouts from
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
CL_DECLARE_CUSTOM_RELEASE(type, function)
CL_DECLARE_NATIVE_DESTROY(type, function)
CL_DECLARE_CUSTOM_TRACE_STRONG(type, function)
CL_DECLARE_VISIT_WEAK_UNITS(type, function)
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
| `Instance` | `DynamicAuxSpan` via `SlotObject` | custom from shape inline capacity |
| `CodeObject` | `CustomRelease` | static |
| `JitCodeObject` | `CustomRelease` | static |
| `ClassObject` | `StaticSpan` via `SlotObject` | static |
| `ValidityCell` | empty `StaticSpan` | static |
| `Scope` | `StaticSpan` | static |
| `Shape` | `CustomRelease` | custom from property count |
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
- Custom release callbacks run cold with an active thread installed.
- Optional native destruction runs after release for either ordinary or custom
  layouts. C++ destructors remain context-free: custom release drains owned VM
  references in containers before destruction, avoiding a second release.
- Descriptor release and bitmap/discovery are separate concerns: slab walking
  discovers object headers, while native layout descriptors release owned
  references after an object has already been selected for reclamation.

## Future Extension Boundary

Public C-extension compatibility may eventually need an explicit extension
descriptor kind or bridge around CPython-style `tp_dealloc`. That is not the
same as today's internal `CustomRelease` support. The future bridge must define
resurrection, finalizer, weakref, and thread-state behavior explicitly before it
can run arbitrary extension teardown.
