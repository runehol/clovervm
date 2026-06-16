# Generational Copying GC Design Notes

This document sketches an alternative garbage-collection direction for CloverVM:
a generational, moving collector that copies ordinary VM objects and handles
native API boundaries through explicit handles or stable wrappers.

It is intentionally separate from
[Generational Mark-Sweep GC Design Notes](generational-mark-sweep-gc.md):
this design allows ordinary VM objects to move, so it is incompatible with a
collector strategy that preserves raw managed-object addresses for native code.

The collector goal is a stop-the-world generational copying runtime:

- normal VM objects are allocated in moving generations and may be copied during
  stop-the-world collection;
- old or stable storage may use a non-moving policy where copying is not
  appropriate, such as extension-owned objects or exported storage;
- roots and heap references are updated after object movement.

Native API support follows from that collector choice:

- VM-internal intrinsic functions may use raw `Value` directly, but cannot let
  unrooted values survive safepoints;
- native code that uses CloverVM's own C extension API sees opaque
  `clover_handle` values that can survive collections through VM-managed
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

The native API sections are part of the GC design because a moving collector
must know every root and must be able to update every reference to moved
objects. They are not the main collector policy.

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

## Copying Collection Shape

The ordinary managed heap should be designed around evacuation: live objects in
the collected generation are copied to new storage, and every root or heap slot
that referenced the old address is rewritten to the new address before
execution resumes.

A collection has three broad phases:

1. Stop mutators at safepoints and publish all VM-controlled roots.
2. Evacuate reachable movable objects from the collected generation.
3. Rewrite roots, handle slots, and heap references to point at copied objects.

The collector must be able to enumerate and update:

- managed stack slots and accumulators;
- VM/runtime roots;
- ordinary object fields and backing arrays;
- remembered old-to-young references for minor collections;
- CloverVM C API handle storage;
- CPython Limited API wrapper target slots;
- extension-owned stable objects that expose tracing hooks.

Objects in stable storage are not copied by this ordinary evacuation path. They
are still part of the traced object graph if they can reference or be referenced
by movable VM objects.

## Heap Object Metadata

The collector should keep layout metadata separate from GC state.

The current heap header already carries `native_layout_id_` and
`native_layout_aux_count`. For a copying collector, the GC should treat those as
inputs to the native-layout descriptor system:

```text
descriptor = descriptor_for(obj->native_layout_id())
size = descriptor.object_size(obj)
trace/copy layout = descriptor.trace_layout(obj)
```

The GC-specific object state needed for ordinary copied objects is much smaller:

- normal versus forwarded state;
- forwarding target when the object has been evacuated.

The forwarding target can be written into the from-space object itself after
the object has been copied. A reference repair path can then recognize a
forwarded object and replace the old address with the forwarding target.

For copied generations, classic white/grey/black mark bits do not need to live
in every object header. The states are implicit:

```text
not forwarded in from-space = white
forwarded but not scanned = grey
forwarded and scan complete = black
```

The grey/black distinction can be represented by the evacuation scan queue and
its scan frontier rather than by header bits.

Generation should also be kept out of the object header if the owning
space/slab/region can answer it cheaply. A per-object generation marker is only
needed if the implementation allows mixed-generation storage where generation
cannot be derived from the allocation region.

Remembered-set state is separate from forwarding state. It can be represented
with side metadata such as cards, remembered-object sets, or region dirty state.
A per-object remembered bit is an optimization to avoid duplicate remembered-set
entries, not a fundamental copied-object header requirement.

## Generations

The first policy should be a stop-the-world generational collector:

- young generation for newly allocated movable objects;
- old generation for objects that survive enough young collections or are
  promoted by policy;
- stable storage for extension-owned objects and storage that must not move.

Young collections evacuate live young objects and update references from roots
and remembered old objects. They must not scan the entire old generation.
Old-to-young stores therefore need a remembered-set barrier, even though the
collector itself is stop-the-world.

Major collections may collect both young and old movable generations. The exact
old-generation policy is still open: old objects may continue to move during
major collection, or the old generation may use a non-moving mark-sweep policy
for objects that are expensive or risky to copy. That choice is independent from
the C API handle contract: native code still cannot observe raw addresses for
ordinary movable objects.

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

The CloverVM C extension API uses `clover_handle`. This is the preferred native
extension surface for Clover-specific modules. This name is intentional: in a
moving collector the API value is a handle to VM-owned storage, not the VM value
representation itself.

Unlike the intrinsic API, this API may call helpers that allocate and trigger
collection. Therefore `clover_handle` cannot remain a raw copy of movable
`Value` bits in a moving collector. It must become an opaque VM-managed token or
handle that the active `clover_context` can trace and update.

The CloverVM C API does not need to preserve handle identity. If the same VM
object is exposed twice as `clover_handle`, the two `clover_handle` values do not
need to compare equal unless a specific API function promises that. Extension
code should treat `clover_handle` as an opaque value handle, not as an identity
pointer.

This weaker contract avoids the cost of a global canonical wrapper table for
ordinary Clover-native extensions. `clover_handle` can be an opaque pointer to
VM-owned handle storage. The handle storage is scanned and updated by the GC,
but it does not need to be canonical for object identity.

Ordinary `clover_handle` values are transitory handles, valid only for the
active API entry that produced or received them. Native code that needs to store
a value in C globals, native module state, heap allocations, async callbacks, or
other locations that can outlive the current call must first convert it to an
explicit `clover_persistent_handle`.

### CPython Limited C API

The CPython Limited API / Stable ABI compatibility layer is the heaviest native
surface. It uses stable, opaque `PyObject *` values and CPython-style refcount
lifetime rules.

This layer needs stable wrapper identity for C-visible `PyObject *` values.
Unlike `clover_handle`, a `PyObject *` may be compared, increfed, decrefed, and
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

This table is not required for the CloverVM C API. `clover_handle` values do
not promise stable identity, so the VM can use cheaper per-context or
per-native-call handle storage.

## CloverVM C API Handles

The CloverVM C API needs handles because native C stack/register state is not
precisely scannable as managed `Value` storage. In a moving collector,
`clover_handle` should be an opaque pointer to VM-owned handle storage rather
than contain raw `Value` bits.

There are two expected handle lifetimes:

- transitory `clover_handle` objects owned by the active native call or
  `clover_context`;
- `clover_persistent_handle` objects explicitly retained by native code and
  released later.

Both storage classes contain a managed `Value` slot that the collector can scan
and update when movable objects are copied. The difference is ownership and
lifetime, not the external handle concept.

Conceptually:

```text
VM Value[] args
  -> CloverNativeCallFrame
       create transitory handles for arguments
       pass opaque clover_handle pointers to native code
       API helpers read/write handle targets
       GC scans and updates handle storage
       convert returned clover_handle through its handle
       release transitory handles on return
```

Transitory handles are valid only for the active API entry that produced or
received them. `clover_persistent_handle` objects are created by an explicit
retain operation and may be stored in C globals, native module state, C heap
allocations, or callbacks until released.

`clover_persistent_handle` is the name for a persistent native root. It should
be a distinct opaque C type from `clover_handle`:

```c
typedef struct clover_handle clover_handle;
typedef struct clover_persistent_handle clover_persistent_handle;
```

The expected API shape is:

```c
clover_persistent_handle *clover_handle_retain(clover_context *ctx,
                                               clover_handle *handle);
void clover_persistent_handle_release(clover_persistent_handle *handle);
clover_handle *clover_persistent_handle_get(
    clover_context *ctx, clover_persistent_handle *handle);
```

`clover_handle_retain` resolves the transitory `clover_handle` through the
current context, allocates persistent VM-owned handle storage, copies the
managed value into that storage, and registers the persistent handle as a native
root. `clover_persistent_handle_get` converts the persistent root back into an
ordinary transitory `clover_handle` usable with the current context.
`clover_persistent_handle_release` removes the native root and allows the target
to be collected if no other roots remain.

An implementation may choose to back `clover_persistent_handle` with the same
stable indirection objects used for CPython Limited API `PyObject *` wrappers.
That is an implementation detail only. The CloverVM C API should expose
`clover_persistent_handle` as its own opaque type, and native Clover extensions
must not depend on it being layout-compatible with, pointer-identical to, or
interchangeable with `PyObject *`.

A `clover_persistent_handle` can be converted back to an ordinary transitory
`clover_handle` by returning a handle view of the same underlying storage or by
creating a transitory alias for the current context. The key requirement is that
the storage backing the resulting `clover_handle` is live independently of the
old call stack when the value originated from a `clover_persistent_handle`.

```text
clover_handle
  -> VM-owned handle
       value slot -> movable VM object or immediate Value
       lifetime -> transitory context-owned or clover_persistent_handle root
```

Every CloverVM C API function resolves values by validating the opaque handle
and then reading or updating its value slot:

```text
clover_* API(ctx, handle)
  storage = validate_handle(ctx, handle)
  read or update storage->slot
```

Validation should distinguish transitory handles from persistent handles. A
transitory handle used after its context has returned should fail validation. A
`clover_persistent_handle` remains valid until
`clover_persistent_handle_release` is called.

When a CloverVM C API operation stores a value into VM-owned storage, it stores
the underlying managed value, not the `clover_handle` itself. For example, a
future `clover_list_set_item(ctx, list, index, item)` would resolve `item`
through its handle and then perform a normal managed list store
with the collector's write barrier. The list would not retain the transitory
handle, and the native extension could not keep using `item` after the call
returns.

The same object exposed twice may receive two distinct `clover_handle` values.
That is allowed unless a specific CloverVM C API function documents identity
semantics. Extension code must use API operations for equality, hashing, and
object behavior rather than comparing handle pointer values.

Native code that needs to retain a value across calls must use
`clover_persistent_handle`. It should not rely on transitory handles surviving
after the current API entry returns.

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

`clover_handle` values do not use this refcount contract. Their lifetime is
owned either by the active CloverVM C API context for transitory handles or by
an explicit `clover_persistent_handle` retain/release API for persistent native
roots.

## Cross-Heap References

This hybrid design has several important cross-boundary reference directions.

CloverVM C API handle to movable object:

```text
clover_handle -> VM-owned handle -> movable VM object
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
- What is the concrete `clover_handle` storage layout: one allocation per handle,
  arena-allocated handles, freelists, debug cookies, and whether immediates get
  direct singleton handles or per-call/per-retain handles?
- What is the exact `clover_persistent_handle` API shape and error behavior for
  converting a transitory `clover_handle` into a storable native root and
  releasing it later?
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
