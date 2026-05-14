# Layout-ID-Driven Value Scanning and Deallocation Dispatch

## Context

The current heap reclaimer has an interim object-specific value-span helper:
`ObjectValueSpan` is derived from the concrete `HeapObject`'s `HeapLayout`, and
reclamation clears those slots before releasing copied child values. That is
good enough for the acyclic deferred-refcount baseline, including compact and
expanded dynamic layouts, but it is not the final descriptor facade.

The next slab discovery step is described in
[Committed-Object Bitmap Reclamation](committed-object-bitmap-reclamation.md).
In short, slab candidate discovery uses a committed-object header bitmap, while
native-layout descriptors handle owned-value scanning and teardown. Object
extent can remain a future descriptor concern, but it is not required for
bitmap-based slab walks.

Today, heap object metadata encodes three scanning/deallocation facts per object:

- total object size
- start offset of scanned `Value` region
- number of scanned `Value` members

This model is simple and fast for homogeneous object layouts, but it creates hard
layout constraints:

- scanned `Value` slots must be represented as one contiguous region
- object teardown semantics are coupled to metadata shape assumptions
- integrating CPython extension objects is awkward because extensions already
  define custom destruction through `tp_dealloc`

At the same time, most native object layouts are either:

- fixed-size (fully known statically), or
- dynamic-size with size derivable from object state (e.g. list/tuple payload
  length)

And we already carry a native layout identity in `Object` that can serve as a
stable dispatch key.

## Proposal Summary

Use `NativeLayoutId` as the primary dispatch key for owned-value scanning and
object-specific teardown. In this document, "descriptor" means a VM-internal
heap layout descriptor, not a Python attribute descriptor. It is runtime metadata
that tells reclamation which stored `Value` fields are owned child references
and which native teardown hook, if any, must run. Object extent may be added to
the descriptor table later for accounting or validation, but bitmap-based slab
walking does not require it.

### Core idea

Maintain a runtime table keyed by `NativeLayoutId`. Most native layout IDs
should map to a normalized metadata descriptor:

- scanned owned `Value` field spans or recipes
- optional native destroy hook

Custom callbacks are escape hatches for layouts that cannot be described by this
normal form. This keeps reclamation friendly to branch prediction: most heap
objects flow through one predictable metadata-descriptor path instead of a large,
unpredictable per-object switch.

The table still needs distinct entry kinds:

- **Static-layout entries**: contain compile-time constants for
  - scan recipe (`Value` field locations or spans)
  - optional destroy hook
- **Dynamic-layout entries**: contain callbacks that compute
  - scan recipe from object contents
  - optional destroy hook
- **C-extension entry kind**: delegates teardown semantics to CPython-compatible
  deallocation via `tp_dealloc` (or VM wrapper that enforces runtime invariants
  before/after call)

In this scheme, metadata is no longer required to be an encoded contiguous
`Value` run; scanning can be described by per-layout recipes.

## Why this helps

### 1) Relaxed object layout constraints

`Value` members can live in multiple regions and coexist with native fields
without forcing a single contiguous encoded span. This makes native class
layout evolution simpler and reduces pressure on header encoding.

### 2) Better extension compatibility

A distinct extension layout kind can preserve CPython’s object ownership model:
custom `tp_dealloc` remains authoritative for extension types, while the VM
still uses layout-id dispatch for safepoint scanning and teardown coordination.

### 3) Clearer ownership of behavior

Each layout ID defines its own memory behavior contract (owned-value scan,
teardown, and possibly future extent), which is easier to audit than globally
decoding one packed header format.

### 4) Better separation of mechanism and policy

- Mechanism: refcount/safepoint engine asks “how do I scan or tear down this
  object?”
- Policy: layout entry answers with exact static data or dynamic computation

This can simplify future features (layout specialization, inline storage
variants, extension shims).

## Costs and Risks

### 1) Indirection overhead

Dispatching by layout ID introduces table lookups and potentially callback calls.
For hot paths, this is likely acceptable if:

- static entries are fully inlineable via small IDs + contiguous table
- the normalized metadata-descriptor path handles most native objects
- dynamic handlers are rare/cold compared to metadata-descriptor objects

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

Existing code that assumes contiguous `Value` spans must migrate to recipe-based
scanning. During migration, mixed modes can complicate debugging.

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
records and keeps scanning logic uniform.

## 2. Define The Descriptor Facade

```cpp
struct LayoutDescriptor {
  enum class Kind : uint8_t {
    kStatic,
    kDynamic,
    kCExtension,
  };

  Kind kind;

  // For kStatic
  ScanRecipe static_scan;

  // For kDynamic
  ScanRecipe (*dynamic_scan)(const HeapObject*);

  // Optional generic destroy hook for native kinds.
  void (*destroy)(HeapObject*);

  // For kCExtension
  void (*tp_dealloc_bridge)(HeapObject*);
};
```

`ScanRecipe` can start minimal:

- small fixed array of `(offset, count)` spans (for contiguous runs)
- fallback callback for truly irregular cases

This keeps fixed objects fast while permitting non-contiguous layouts.

For most native layouts, `NativeLayoutId` should look up a metadata descriptor
rather than dispatching directly to one custom handler per heap object kind.
Custom dynamic handlers should be reserved for layouts that genuinely cannot fit
the normal metadata form.

## 3. Keep compact fast paths for common static layouts

For top N high-frequency layouts, encode direct static recipes and size in a
cache-hot table indexed by `NativeLayoutId`. Avoid function pointers on these
entries to preserve branch predictability. Keep descriptor dispatch biased toward
a small number of common paths: metadata descriptors first, custom dynamic
handlers second, and extension bridges later.

## 4. Define teardown order

Teardown processes owned `Value` fields one by one. For each owned field, the
reclamation path may read or copy the current `Value`, then it must clear the
field in the object, and only then release the copied value through the
reclamation path. Clearing before release prevents partially torn-down objects
from retaining ownership if child release cascades or trips debug checks.

## 5. Make extension path explicit and isolated

Treat extension layouts as a first-class descriptor kind. All special behavior
lives in one bridge layer that maps VM lifecycle to `tp_dealloc` safely.

## 6. Add validation

At startup (or in debug builds):

- verify every ID has a descriptor
- verify descriptor kind invariants (e.g., static fields present for `kStatic`)
- verify scan expectations for known native C++ types with
  `static_assert`s and table checks

## Migration Plan

1. **Introduce the descriptor-shaped API without deleting current metadata.**
   Reclamation should call this API instead of open-coding `HeapLayout`
   decoding. The existing `ObjectValueSpan` helper is the useful nucleus for
   owned-value scanning, but it should move behind the facade rather than become
   the facade itself.
2. **Implement the normalized metadata-descriptor path.**
   Use it for layouts that can be described by scanned `Value` spans.
   Bridge still-unmigrated layouts through existing `HeapLayout` decoding as a
   compatibility path, not as the main reclamation interface.
3. **Migrate fixed native layouts in small groups.**
   Validate descriptor parity against current metadata as entries are added.
4. **Add custom dynamic handlers only where metadata descriptors are insufficient.**
   Validate parity in tests.
5. **Introduce C-extension descriptor kind + bridge.**
   Gate with focused extension lifecycle tests.
6. **Remove legacy header-scanning dependence once parity is proven.**

## Decision

This appears to be a good direction, with one caveat: keep the static-layout
path aggressively optimized and data-driven so we do not regress hot refcount/
scan behavior.

In short:

- **Yes** to layout-ID keyed scanning/teardown dispatch.
- **Yes** to a normalized metadata-descriptor path for most native layouts.
- **Yes** to explicit dynamic-layout handlers where the normal metadata form is
  insufficient.
- **Yes** to dedicated C-extension (`tp_dealloc`) bridge kind.
- **No** to replacing fast metadata-descriptor scanning with generic callbacks or
  a broad unpredictable switch everywhere.

A hybrid descriptor table (fast static entries + explicit dynamic/extension
handlers) provides the flexibility you want without giving up predictable
performance for common objects.
