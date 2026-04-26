# Layout-ID-Driven Value Scanning and Deallocation Dispatch

## Context

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

Use `NativeLayoutId` as the primary dispatch key for object scanning and
object-specific teardown.

### Core idea

Maintain a runtime table keyed by `NativeLayoutId`:

- **Static-layout entries**: contain compile-time constants for
  - object size
  - scan recipe (`Value` field locations or spans)
  - optional destroy hook
- **Dynamic-layout entries**: contain callbacks that compute
  - object size from object contents
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

Each layout ID defines its own memory behavior contract (size, scan, teardown),
which is easier to audit than globally decoding one packed header format.

### 4) Better separation of mechanism and policy

- Mechanism: refcount/safepoint engine asks “how do I scan/free this object?”
- Policy: layout entry answers with exact static data or dynamic computation

This can simplify future features (layout specialization, inline storage
variants, extension shims).

## Costs and Risks

### 1) Indirection overhead

Dispatching by layout ID introduces table lookups and potentially callback calls.
For hot paths, this is likely acceptable if:

- static entries are fully inlineable via small IDs + contiguous table
- dynamic handlers are rare/cold compared to fixed-layout objects

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

## 2. Define `LayoutDescriptor`

```cpp
struct LayoutDescriptor {
  enum class Kind : uint8_t {
    kStatic,
    kDynamic,
    kCExtension,
  };

  Kind kind;

  // For kStatic
  uint32_t static_size;
  ScanRecipe static_scan;

  // For kDynamic
  uint32_t (*dynamic_size)(const HeapObject*);
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

## 3. Keep compact fast paths for common static layouts

For top N high-frequency layouts, encode direct static recipes and size in a
cache-hot table indexed by `NativeLayoutId`. Avoid function pointers on these
entries to preserve branch predictability.

## 4. Make extension path explicit and isolated

Treat extension layouts as a first-class descriptor kind. All special behavior
lives in one bridge layer that maps VM lifecycle to `tp_dealloc` safely.

## 5. Add validation

At startup (or in debug builds):

- verify every ID has a descriptor
- verify descriptor kind invariants (e.g., static fields present for `kStatic`)
- verify size/scan expectations for known native C++ types with
  `static_assert`s and table checks

## Migration Plan

1. **Introduce descriptors without deleting current metadata path.**
   Add layout table and wire read-only verification against current metadata.
2. **Switch scanning to descriptor path for fixed native layouts.**
   Keep fallback to old header decode behind debug asserts.
3. **Add dynamic handlers for variable-size builtins.**
   Validate parity in tests.
4. **Introduce C-extension descriptor kind + bridge.**
   Gate with focused extension lifecycle tests.
5. **Remove legacy header-scanning dependence once parity is proven.**

## Decision

This appears to be a good direction, with one caveat: keep the static-layout
path aggressively optimized and data-driven so we do not regress hot refcount/
scan behavior.

In short:

- **Yes** to layout-ID keyed scanning/teardown dispatch.
- **Yes** to explicit dynamic-layout handlers.
- **Yes** to dedicated C-extension (`tp_dealloc`) bridge kind.
- **No** to replacing fast static decoding with generic callbacks everywhere.

A hybrid descriptor table (fast static entries + explicit dynamic/extension
handlers) provides the flexibility you want without giving up predictable
performance for common objects.
