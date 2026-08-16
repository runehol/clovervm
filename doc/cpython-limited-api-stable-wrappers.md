# CPython Limited API Stable Wrappers

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | Not started |
| Scope | Source compatibility with a CPython Limited API subset, stable `PyObject *` identity, wrapper lifetime, extension-owned objects, and native call boundaries |
| Owning layers | CPython compatibility API, native call adapters, stable storage, and GC root publication |
| Validated against | `d99f5e99` (2026-08-12) |
| Supersedes | CPython-wrapper sections formerly contained in `generational-copying-gc.md` |

This document describes a prospective source-compatibility layer for a selected
subset of the CPython Limited API over CloverVM's moving managed heap.
Extensions must be rebuilt against CloverVM's compatibility headers; CloverVM
does not promise that CPython Stable ABI or `abi3` extension binaries can be
loaded unchanged. The collector-facing requirements remain summarized in
[Generational Copying GC Design Notes](generational-copying-gc.md); this document
owns the wrapper representation and native compatibility mechanics.

This design does not target the unrestricted CPython C API. Native CloverVM
extensions should prefer the cheaper call-scoped `clover_handle` API described
in [Switchable Indirect Native Handles](indirect-native-handles.md).

## Compatibility Target

The intended target is source compatibility with a selected subset of CPython's
Limited API, not binary compatibility with CPython's Stable ABI. Extension code
is compiled against CloverVM-provided compatibility headers, which may implement
API operations differently from CPython and define a CloverVM-owned object
header layout.

Supported direction:

```text
extension code
  is rebuilt against CloverVM's compatibility headers
  holds PyObject *
  uses supported refcount and type API through those headers
  calls other supported API functions/macros
  never relies on object body layout
```

Unsupported direction:

```text
extension code
  is loaded as an unchanged CPython Stable ABI or abi3 binary
  casts PyObject * to PyLongObject *, PyTupleObject *, ...
  reads or writes CPython object fields
  assumes PyObject * has CPython's binary header or object-body layout
```

The important invariant is:

```text
A PyObject * value observed by Limited-API native code remains stable for the
lifetime promised by refcount and borrowed-reference rules, but it does not
imply that the underlying VM object has a stable address or CPython-compatible
binary layout.
```

The external pointer is opaque. Internally, it may be an indirection cell whose
target is updated by the collector.

## Stable Native Wrappers

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
The wrapper is not a Python-visible `cell` object and does not expose cell
semantics.

Every C-visible `PyObject *` allocation starts with the object header defined by
CloverVM's compatibility headers. This preserves source-level uses of
`PyObject`, `PyObject_HEAD`, and supported accessors without requiring CPython's
binary layout. For VM-object proxies, the header is the prefix of the stable
native wrapper. For extension-owned objects, it is the prefix of the
extension-owned allocation. The header contains:

- a native-visible refcount, with the exact width still to be selected;
- a flags field distinguishing VM-object proxies from extension-owned objects;
- a pointer to a Python type object. For VM-object proxies this remains null;
  for extension-owned objects it points at the real Python type object.

For VM-object proxies, the `Py_TYPE` accessor supplied by CloverVM's
compatibility headers does not fill or cache the header type pointer. It
dereferences the wrapped Clover value, inspects its current shape and class, and
materializes the corresponding CPython type wrapper. Caching that result in the
proxy header would require invalidation when the observed class changes.

Materialized CPython type wrappers need stable identity. The VM keeps a canonical
mapping from Clover class objects to their CPython type wrapper objects, so
repeated materialization of the same Clover class produces the same C-visible
type object while that wrapper identity is live.

## Extension-Owned Objects

Objects whose body layout is controlled by a native extension cannot move,
because the extension may store fields at fixed offsets inside its allocation.
These objects live in stable storage.

The collector cannot trace arbitrary fields inside an extension-owned body, and
the Limited API does not require that capability. References stored by the
extension are stable `PyObject *` values, not direct pointers to movable Clover
objects. For a VM-owned target, the `PyObject *` identifies a stable wrapper
whose updateable target slot is visible to the collector. Normal
`Py_INCREF`/`Py_DECREF` ownership keeps that wrapper live.

Consequently, an extension-owned field never creates a direct
stable-body-to-nursery edge. CloverVM does not apply its generational write
barrier to stores inside the opaque extension body; it traces the wrapper target
instead.

## Canonical Wrapper Table

The VM maintains a canonical table from managed object identity to stable native
wrapper:

```text
VM object identity -> NativeWrapper *
```

This is distinct from the Clover-class to CPython-type-wrapper identity map used
for type exposure.

Looking up the same VM object for CPython Limited API exposure returns the same
wrapper while a live native reference to that wrapper exists. This preserves
`PyObject *` pointer identity for simultaneously exposed values.

The table entry is an identity cache, not a permanent root:

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

During collection, the VM:

1. treats positive-refcount native wrappers as roots;
2. copies or marks their targets according to managed-heap policy;
3. updates wrapper target slots after movement;
4. removes eligible zero-refcount wrappers from the identity table;
5. frees or recycles removed wrapper memory.

If a VM object remains alive after its zero-refcount wrapper is removed, a later
Limited API exposure may create a different `PyObject *`. That is acceptable
because native code may not retain the old pointer after its reference or
borrowed-reference lifetime ends.

This canonical table is not required for CloverVM's `clover_handle` API, which
does not promise stable handle identity.

## Limited API Call Frames

Boundary bookkeeping is centralized in a native-call adapter frame rather than
spread across individual call sites. When managed code calls Limited-API native
code, the frame owns:

- temporary `PyObject *` argument storage;
- temporary increfs protecting borrowed call arguments;
- roots for managed values needed during argument and return conversion;
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

Even when the native API declares an argument borrowed, the VM may internally
hold a temporary wrapper reference for the duration of the call. Duplicate
managed arguments that denote the same object must be exposed as the same stable
wrapper pointer.

## Wrapper Refcounts

Native wrapper refcounts describe the lifetime of the C-visible opaque pointer.
They do not imply that the movable target itself uses CPython refcount lifetime.

```text
positive wrapper refcount
  -> wrapper is a native root
  -> wrapper traces target
  -> target remains live
```

`Py_INCREF` and `Py_DECREF` operate on the stable wrapper or extension-owned
object seen by native code. A wrapper that reaches zero may become eligible for
identity-table cleanup at a later collection. The table must not make every
wrapper ever created a permanent strong root.

## Cross-Heap References

Stable wrapper to movable object:

```text
wrapper.target -> movable VM object
```

Positive-refcount wrappers are roots. If the target moves, the collector updates
the target slot.

Extension-owned object to movable object:

```text
stable extension object field
  -> stable PyObject * wrapper
       -> movable VM object
```

The extension owns the stable wrapper through CPython reference counting. The
collector neither scans the opaque extension field nor barriers its store. It
scans and updates the wrapper's target slot.

Movable object to stable object:

```text
VM object field -> stable extension object or wrapper-visible object
```

Managed tracing understands stable objects as heap references. Stable objects
may be non-moving, but their liveness remains part of the object graph unless a
specific category is immortal or explicitly outside collection.

The supported cycle semantics are therefore constrained by the Limited API and
wrapper-refcount contract; the collector must not assume it can discover
otherwise opaque extension-owned fields.

## Relationship To Pinning

Pinning is not the mechanism that makes a Limited-API `PyObject *` stable. The
stable wrapper supplies C-visible pointer identity while its managed target may
move. Pinning remains relevant when an API exposes a raw address into an object
body or backing store rather than an opaque `PyObject *`.

## Open Questions

- What exact Limited API source subset is targeted first?
- Which source-level macros are supported, and which imply unsupported layout
  access?
- What exact CloverVM object-header prefix and private wrapper fields are used?
- How is proxy-versus-extension-owned state represented through the
  compatibility headers?
- What type object must `Py_TYPE` expose for a VM-object proxy, and how is it kept
  consistent with the target's class?
- What lifetime and cleanup rules govern the Clover-class to CPython-type-wrapper
  identity map?
- How does the canonical wrapper table key a movable object across collections?
- When are zero-refcount wrappers removed?
- What cycle, finalization, resurrection, and weak-reference semantics can be
  supported when extension-owned fields are opaque and only their stable
  wrapper references are visible?
- How are native-call adapter frames represented so GC can scan in-progress
  argument and return conversion safely?
