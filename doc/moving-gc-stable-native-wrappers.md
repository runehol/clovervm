# Moving GC With Stable Native Wrappers

This document sketches an alternative garbage-collection and native-extension
direction for CloverVM. It is intentionally separate from
[Generational Mark-Sweep GC Design Notes](generational-mark-sweep-gc.md):
this design allows ordinary VM objects to move, so it is incompatible with a
collector strategy that preserves raw managed-object addresses for native code.

The goal is a hybrid runtime:

- normal VM objects are allocated in moving generations and may be copied during
  stop-the-world collection;
- VM-internal intrinsic functions may use raw `Value` directly, but cannot let
  unrooted values survive safepoints;
- native code that uses CloverVM's own C extension API sees opaque
  `clover_value` handles that can survive collections through VM-managed
  indirection, but do not guarantee stable handle identity;
- native code that uses a CPython Limited API / Stable ABI style surface sees
  stable, opaque `PyObject *` values with CPython-style reference lifetime
  rules;
- the stable `PyObject *` values are VM-owned native wrappers, not raw pointers
  to movable VM object bodies;
- extension-owned object bodies live on a stable heap and participate in the
  same opaque-pointer interface;
- raw storage exports such as buffers and string/bytes data use specialized
  pinning or stable-storage rules rather than making arbitrary objects
  non-moving.

This is not full CPython C API or ABI compatibility. Extensions must not depend
on CPython object layout, downcast `PyObject *` to concrete internal structs, or
inspect object fields directly. The supported contract is an opaque object
pointer API where all behavior goes through VM-provided functions and supported
macros.

## Collector Model

The collector assumed by this proposal is generational, moving, and
stop-the-world. It is not concurrent or incremental in the initial design.

During a collection, mutator threads and native extension execution are stopped
at safepoints. The collector may copy movable VM objects, update managed roots,
repair managed heap references, update native wrapper target slots, and clean
dead zero-refcount wrappers before mutators resume.

This pause model is important for the stable-wrapper design. Native wrappers do
not need concurrent forwarding logic or read barriers merely to preserve
`PyObject *` stability. While native code is running, the wrapper's address is
stable. While the collector is running, native code is stopped and the VM can
rewrite wrapper target slots directly.

## Native API Layers

There are three native-facing APIs with different contracts. They should not be
collapsed into one handle model.

### Intrinsic API

The intrinsic API is VM-internal. Intrinsic functions expose raw `Value`
directly and are meant for trusted runtime helpers and builtins that participate
in CloverVM's internal calling convention.

This API has the weakest boundary guarantees:

- raw `Value` may contain direct pointers to movable VM objects;
- intrinsic code must not keep unrooted values across safepoints;
- intrinsic code must not store `Value` outside VM-owned rooted storage unless
  the storage participates in managed tracing/update;
- intrinsic calls should be treated as part of the VM implementation, not as an
  external extension ABI.

If an intrinsic can allocate, call back into Python, trigger collection, or
otherwise reach a safepoint, its live values must be visible through ordinary VM
roots, managed frames, or explicit VM-rooted handles before the safepoint.

### CloverVM C Extension API

The CloverVM C extension API uses `clover_value`. This is the preferred native
extension surface for Clover-specific modules.

Unlike the intrinsic API, this API may call helpers that allocate and trigger
collection. Therefore `clover_value` cannot remain a raw copy of movable
`Value` bits in a moving collector. It must become an opaque VM-managed token or
handle that the active `clover_context` can trace and update.

The CloverVM C API does not need to preserve handle identity. If the same VM
object is exposed twice as `clover_value`, the two `clover_value` tokens do not
need to compare equal unless a specific API function promises that. Extension
code should treat `clover_value` as an opaque value token, not as an identity
pointer.

This weaker contract avoids the cost of a global canonical wrapper table for
ordinary Clover-native extensions. `clover_value` can be an opaque pointer to
VM-owned handle storage. The handle storage is scanned and updated by the GC,
but it does not need to be canonical for object identity.

Temporary `clover_value` handles are valid only for the active API entry that
produced or received them. Native code that needs to store a value in C globals,
native module state, heap allocations, async callbacks, or other locations that
can outlive the current call must first convert it to an explicit
`clover_handle`.

### CPython Limited C API

The CPython Limited API / Stable ABI compatibility layer is the heaviest native
surface. It uses stable, opaque `PyObject *` values and CPython-style refcount
lifetime rules.

This layer needs stable wrapper identity for C-visible `PyObject *` values.
Unlike `clover_value`, a `PyObject *` may be compared, increfed, decrefed, and
held according to CPython's reference rules. That requires a canonical stable
wrapper or stable extension object while native references exist.

The CPython Limited API layer remains restricted: downcasts to CPython internal
object layouts and direct field access are unsupported.

## Compatibility Target

The intended native compatibility target is closer to CPython's Limited API /
Stable ABI than to the unrestricted CPython C API.

Supported direction:

```text
extension code
  holds PyObject *
  calls supported API functions/macros
  never relies on object layout
```

Unsupported direction:

```text
extension code
  casts PyObject * to PyLongObject *, PyTupleObject *, ...
  reads or writes CPython object fields
  assumes PyObject * points at a CPython-shaped object body
```

The important invariant is:

```text
A PyObject * value observed by Limited-API native code remains stable for the
lifetime promised by refcount and borrowed-reference rules, but it does not
imply that the underlying VM object has a stable address or CPython-compatible
layout.
```

The external pointer is opaque. Internally, it may be an indirection cell whose
target is updated by the collector.

## Object Categories

This design separates four object/storage categories.

### Movable VM Objects

These are ordinary Clover/Python objects used by the interpreter and managed
code. They are allocated in generational moving heaps. A stop-the-world
young-generation collection may copy them and update all managed references
before execution resumes.

Interpreter and VM-internal code may use direct managed object references while
the object is protected by ordinary managed-root rules. Those direct addresses
must not escape to Limited-API native code as `PyObject *`.

### Stable Native Wrappers

A stable native wrapper is the C-visible representation of a managed VM object.
It is allocated in VM-controlled stable memory and has a stable address. Native
Limited-API code sees a pointer to this wrapper as `PyObject *`.

Conceptually:

```text
PyObject * observed by native code
  -> stable native wrapper
       target -> movable VM object
```

The wrapper behaves like an implementation-private cell: the wrapper identity is
stable, while the target slot may be rewritten when the target object moves.
This is similar to CPython closure cells as an indirection pattern, but the
wrapper is not a Python-visible `cell` object and must not expose cell semantics.

### Extension-Owned Objects

Objects whose body layout is controlled by a native extension cannot move,
because the extension may store fields at fixed offsets inside its own object
allocation. These objects live on a stable heap.

They still participate in the same external opaque-pointer interface: native
code receives a stable `PyObject *`, and VM API calls understand that this
pointer denotes an extension-owned object. Such objects need tracing hooks for
references back into movable VM objects, and refcount/lifetime rules for their
extension-controlled memory.

### Exported Storage

Some APIs expose raw memory rather than just object identity. Examples include:

- buffer protocol exports;
- raw bytes or string data;
- typed array storage;
- memoryviews;
- internal native fast paths that temporarily need a direct storage address.

These cases should not use general-purpose "pin any object" semantics. They
should use explicit storage export rules:

```text
object
  -> backing storage
       export_count
       movable normally, but non-moving or stable while exported
```

An active export may pin the backing storage, allocate it directly in stable
storage, or prevent resizing until the export is released. Pinning the entire
Python object should be a rare implementation detail, not the default native
interop mechanism.

## CPython Stable Wrapper Table

For the CPython Limited API layer, the VM maintains a canonical table from
managed object identity to stable native wrapper:

```text
VM object identity -> NativeWrapper *
```

Looking up the same VM object for CPython Limited API exposure must return the
same wrapper while a live native reference to that wrapper exists. This
preserves `PyObject *` identity observations such as pointer equality for
simultaneously exposed values.

The table entry is an identity cache, not a permanent root. A wrapper with a
positive refcount is externally live and traces its target strongly. A wrapper
with refcount zero may remain in the table until a GC cleanup pass, but it does
not by itself keep the target object alive.

Wrapper states:

```text
refcount > 0:
  externally live
  trace target strongly
  update target if the object moves

refcount == 0:
  not externally live
  may remain cached temporarily
  does not by itself keep target alive
  removable during GC table cleanup
```

During collection, the VM should:

1. Treat positive-refcount native wrappers as roots.
2. Copy or mark their targets according to the managed heap policy.
3. Update wrapper target slots after object movement.
4. Remove zero-refcount wrappers from the identity table.
5. Free or recycle removed wrapper memory.

If a VM object remains alive after its zero-refcount wrapper is removed, a later
CPython Limited API exposure may create a different `PyObject *`. That is
acceptable because no native code may legally retain the old pointer after its
refcount or borrowed-reference lifetime has ended.

This table is not required for the CloverVM C API. `clover_value` handles do
not promise stable identity, so the VM can use cheaper per-context or
per-native-call handle storage.

## CloverVM C API Handles

The CloverVM C API needs handles because native C stack/register state is not
precisely scannable as managed `Value` storage. In a moving collector,
`clover_value` should be an opaque pointer to VM-owned handle storage rather
than contain raw `Value` bits.

There are two expected handle lifetimes:

- temporary handles owned by the active native call or `clover_context`;
- `clover_handle` objects explicitly retained by native code and released
  later.

Both storage classes contain a managed `Value` slot that the collector can scan
and update when movable objects are copied. The difference is ownership and
lifetime, not the external `clover_value` shape.

Conceptually:

```text
VM Value[] args
  -> CloverNativeCallFrame
       create temporary handles for arguments
       pass opaque clover_value pointers to native code
       API helpers read/write handle targets
       GC scans and updates handle storage
       convert returned clover_value through its handle
       release temporary handles on return
```

Temporary handles are valid only for the active API entry that produced or
received them. `clover_handle` objects are created by an explicit retain
operation and may be stored in C globals, native module state, C heap
allocations, or callbacks until released.

`clover_handle` is the name for a persistent native root. It should be a distinct
opaque C type from `clover_value`:

```c
typedef struct clover_handle clover_handle;
```

The expected API shape is:

```c
clover_handle *clover_value_retain(clover_context *ctx, clover_value value);
void clover_handle_release(clover_handle *handle);
clover_value clover_handle_get(clover_context *ctx, clover_handle *handle);
```

`clover_value_retain` resolves the temporary `clover_value` through the current
context, allocates persistent VM-owned handle storage, copies the managed value
into that storage, and registers the handle as a native root. `clover_handle_get`
converts the persistent root back into an ordinary `clover_value` usable with
the current context. `clover_handle_release` removes the native root and allows
the target to be collected if no other roots remain.

A `clover_handle` can be converted back to an ordinary call-scoped
`clover_value` by returning the same opaque handle pointer or by creating a
temporary alias for the current context. The key requirement is that the storage
backing the resulting `clover_value` is live independently of the old call stack
when the value originated from a `clover_handle`.

```text
clover_value
  -> VM-owned handle
       value slot -> movable VM object or immediate Value
       lifetime -> temporary context-owned or clover_handle persistent root
```

Every CloverVM C API function resolves values by validating the opaque handle
and then reading or updating its value slot:

```text
clover_* API(ctx, value)
  handle = validate_handle(ctx, value)
  read or update handle->slot
```

Validation should distinguish temporary handles from persistent handles. A
temporary handle used after its context has returned should fail validation. A
`clover_handle` remains valid until `clover_handle_release` is called.

When a CloverVM C API operation stores a value into VM-owned storage, it stores
the underlying managed value, not the `clover_value` handle. For example, a
future `clover_list_set_item(ctx, list, index, item)` would resolve `item`
through its handle and then perform a normal managed list store
with the collector's write barrier. The list would not retain the temporary
handle, and the native extension could not keep using `item` after the call
returns.

The same object exposed twice may receive two distinct `clover_value` tokens.
That is allowed unless a specific CloverVM C API function documents identity
semantics. Extension code must use API operations for equality, hashing, and
object behavior rather than comparing handle-token values.

Native code that needs to retain a value across calls must use `clover_handle`.
It should not rely on temporary handles surviving after the current API entry
returns.

## CPython Limited API Call Frames

Boundary bookkeeping should be centralized in a native-call adapter frame rather
than spread across individual call sites.

When managed code calls native Limited-API code, the adapter frame owns:

- the temporary `PyObject *` argument array or slots;
- temporary increfs that keep borrowed call arguments valid for the duration of
  the call;
- roots for any managed values needed while converting arguments or return
  values;
- cleanup records for decref on return or failure.

Conceptually:

```text
VM Value[] args
  -> NativeCallFrame
       expose each Value as canonical PyObject *
       incref wrappers needed for call lifetime
       call native function
       convert return value
       decref temporary wrappers in reverse order
```

Even when the native API says an argument is borrowed, the VM may internally
hold a temporary wrapper reference for the duration of the call. From the
extension's point of view the argument is borrowed; from the VM's point of view
the wrapper and target are protected across the native boundary.

This adapter also preserves identity for duplicate arguments. If two managed
arguments are the same object, exposing both through the frame must produce the
same stable wrapper pointer.

## CPython Wrapper Refcounts

Native wrapper refcounts describe the lifetime of the C-visible opaque pointer.
They do not imply that the movable target object itself is refcounted in the
same way.

The target's managed lifetime is determined by tracing:

```text
positive wrapper refcount
  -> wrapper is a native root
  -> wrapper traces target
  -> target remains live
```

`Py_INCREF` and `Py_DECREF` operate on the wrapper or stable extension object
seen by native code. If a `Py_DECREF` drops a wrapper to zero, the wrapper may be
eligible for identity-table cleanup at a later collection.

The design should avoid making every wrapper ever created a permanent strong
root. Otherwise repeated native exposure of short-lived values would leak object
graphs through the wrapper table.

`clover_value` handles do not use this refcount contract. Their lifetime is
owned either by the active CloverVM C API context for temporary handles or by an
explicit `clover_handle` retain/release API for persistent native roots.

## Cross-Heap References

This hybrid design has several important cross-boundary reference directions.

CloverVM C API handle to movable object:

```text
clover_value -> VM-owned handle -> movable VM object
```

Live CloverVM C API handles are scanned as native roots. If a target moves, the
handle's value slot is updated. The handle itself does not need to be canonical
for the target object.

Stable wrapper to movable object:

```text
wrapper.target -> movable VM object
```

Positive-refcount wrappers are scanned as roots. If the target moves, the target
slot is updated.

Extension-owned object to movable object:

```text
stable extension object field -> VM object
```

Extension-owned objects need tracing hooks, equivalent in spirit to
`tp_traverse`, so the collector can discover references into moving heaps.
Writes from stable extension objects into young/movable generations also need
whatever remembered-set or barrier policy the chosen collector requires.

Movable object to stable object:

```text
VM object field -> stable extension object or wrapper-visible object
```

Managed tracing must understand stable objects as heap references. Stable
objects may be non-moving, but their liveness is still part of the object graph
unless they are immortal or otherwise explicitly outside collection.

Cycles that cross movable VM objects and extension-owned stable objects require
a deliberate policy. Refcounts alone are not enough for cycles that include
both heaps.

## Pinning Policy

Pinning is not the mechanism that makes `PyObject *` stable. Stable native
wrappers provide that property.

Pinning is only needed when native code has a raw address into an object body or
backing storage. The preferred policies are:

- expose raw storage through explicit export objects;
- count active exports;
- prevent movement or resizing of exported storage while the export is live;
- allocate frequently exported storage in a stable storage class if needed;
- keep arbitrary object-body pinning internal, scoped, and rare.

The design should avoid a public API that lets extensions request and retain
raw addresses for arbitrary object bodies. That would reintroduce the
unrestricted CPython C API's layout and address-stability assumptions.

## Open Questions

- What exact Limited API / Stable ABI version or subset is targeted first?
- Which macros are supported, and which are rejected because they imply layout
  access?
- What is the concrete wrapper layout, and does it need to be CPython-header
  shaped for source compatibility with Limited-API code?
- What is the concrete `clover_value` handle layout: one allocation per handle,
  arena-allocated handles, freelists, debug cookies, and whether immediates get
  direct singleton handles or per-call/per-retain handles?
- What is the exact `clover_handle` API shape and error behavior for converting
  a temporary `clover_value` into a storable native root and releasing it later?
- How does the identity table key movable objects across copying collections:
  direct old address with forwarding repair, stable object ID, or side identity
  cell?
- When are zero-refcount wrappers swept: every collection, major collections
  only, or on wrapper-table pressure?
- How do extension-owned stable objects participate in cycle collection?
- Which storage classes support raw exports, and when do they pin versus move
  to stable storage?
- How are native-call adapter frames represented so GC can scan in-progress
  argument conversion and return conversion safely?
