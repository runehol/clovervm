# Module Global Namespace Design Sketch

This note captures the intended split between lexical scopes, module objects,
and runtime global lookup.

For staged implementation work, see
[Module Global Namespace Implementation Plan](module-global-namespace-implementation.md).

The short version is:

- `Scope` should remain a compiler/name-binding concept for function-like
  lexical structure.
- Modules should not be modeled as ordinary lexical parent scopes.
- Module globals should live in a runtime namespace owned by the module object.
- `LOAD_GLOBAL` should be a special operation over a globals namespace and a
  builtins namespace, not a generic attribute lookup.

## Motivation

The current runtime uses `Scope` for both local scope structure and module
globals. That worked while both were just string-keyed name stores, but the
abstraction is now pulling in incompatible directions.

Function locals are statically classified and live in call-frame positions:

```text
LOAD_FAST  -> frame slot
LOAD_DEREF -> closure cell
```

Module globals are late-bound through a mutable namespace:

```text
LOAD_GLOBAL -> frame globals, then frame builtins
```

Those are different semantic operations. They should not be forced to share a
runtime representation just because both can eventually load from a slot.

## Terminology

Use separate names for the separate jobs:

- **Symbol scope**
  Compiler-side name classification for function, class, comprehension, and
  module blocks. A symbol scope maps names to stable slot identities used by
  compiled code and preserves insertion-order metadata for scope names; it does
  not own runtime value storage, runtime namespace entries, or parent-scope
  lookup indirections.

- **Frame locals**
  Runtime storage for function-local slots and closure-cell references.

- **Runtime namespace**
  Mapping-like storage for module globals, class body locals, `exec` globals,
  and other Python-visible namespace dictionaries.

- **Module object**
  Python object whose attribute storage is also the normal backing namespace for
  module globals.

This avoids using `Scope` to mean both "lexical parent" and "runtime namespace".

## Separate Storage Families

`Scope`, runtime namespaces, object Shapes, and Python `dict` are related but
not interchangeable.

- `Scope`
  - compiler-side name classification
  - `name -> stable slot identity`
  - ordered name/slot metadata for dictionary-like presentation when required
  - no runtime value cells
  - no parent-slot fallback payload

- `Namespace`
  - runtime binding storage for module globals, class body locals, and `exec`
    globals
  - owns live value state and delete/reinsert behavior
  - may be backed by shape/object storage, dict-like storage, or a temporary
    extracted scope-storage substrate during the refactor

- `Shape`
  - object-attribute structure and lookup metadata
  - shape-relative slots, descriptors, transitions, and validity cells
  - the semantic authority for object membership and `obj.__dict__` mapping
    views

- `Dict`
  - normal Python mapping object
  - arbitrary Python keys
  - Python-level hash and equality semantics
  - own insertion-order and value payload

The important rule is that shared implementation substrates are allowed, but
these concepts should not collapse into one concrete table type. In particular,
global lookup should not depend on `Scope` parent-slot indirection, and Python
`dict` should not be coupled to VM-internal string-keyed lookup tables.

## Compile-Time Classification

The compiler should classify each name access before bytecode emission:

```text
local      -> LOAD_FAST / STORE_FAST
cell/free  -> LOAD_DEREF / STORE_DEREF
global     -> LOAD_GLOBAL / STORE_GLOBAL / DEL_GLOBAL
class name -> class-body namespace load/store form
```

For a function, an implicit global is not an unresolved dynamic search through
parent scopes. It means:

```text
emit LOAD_GLOBAL name
```

The runtime then resolves that name against the function frame's globals and
builtins.

## Code Objects, Functions, And Frames

The current implementation stores module context on `CodeObject` as
`module_scope`. During migration, `CodeObject` should carry both the old
`module_scope` and a semantic defining-module reference. The old scope pointer
keeps existing slot-index bytecode working while the new module-object path is
built. The end state should remove `CodeObject::module_scope`, not replace it
with another parent scope chain.

Conceptually:

```text
CodeObject
  module scope (migration only)
  defining module

Function
  code
  defining module
  builtins namespace
  closure cells

Frame
  fast local slots
  deref slots / cells
  defining module
  builtins namespace
```

The defining module is useful for more than global lookup. It is also the
natural home for traceback/debugger/source context, `__module__`-style metadata,
and future module-level reflection.

A function object carries the module context from the environment where it was
defined. In normal module code, global lookup uses the defining module's
namespace.

The function does not search the caller's module. A global read in a function is
late-bound in the function's defining module namespace.

This distinction matters for builtin shadowing:

```python
def f():
    return len([1])

len = lambda x: 99
```

The later module assignment changes `f()` because `LOAD_GLOBAL len` still checks
the same mutable defining module namespace before builtins.

## Builtins

Builtins are not a visible parent module or `__parent__` relationship on module
objects. They are execution-frame state.

The builtins namespace is resolved from the actual `__builtins__` binding in the
globals namespace:

```text
globals["__builtins__"] as module        -> module.__dict__
globals["__builtins__"] as dict-like     -> that object
missing globals["__builtins__"]          -> VM default builtins namespace
```

If `__builtins__` is present but is not a module or dict-like object, CPython
does not silently ignore it. A later builtin lookup tries to use that object as
the builtins namespace and can fail, for example with `'int' object is not
subscriptable`.

Missing `__builtins__` only falls back while resolving the hidden frame builtins
namespace. It does not synthesize a visible module binding.

For example:

```python
del __builtins__
len
```

still finds `len` through the frame's default builtins namespace. But an
ordinary lookup of the name `__builtins__` is still just `LOAD_GLOBAL
"__builtins__"`: it checks the defining module namespace and then the frame
builtins namespace. If the builtins namespace itself has no `__builtins__`
entry, the visible name lookup fails.

Likewise:

```python
__builtins__.__builtins__
```

first resolves the visible global `__builtins__`, then performs ordinary
attribute lookup on that object. The builtin module does not need to have a
visible `__builtins__` attribute.

Because frame builtins resolution depends on the value of the module
`__builtins__` binding, that binding should be treated as a distinguished
predefined module slot. Every `ModuleObject` should reserve fixed predefined
slots for:

```text
slot 0: __name__
slot 1: __builtins__
```

This lets C++ runtime code find both bindings without doing a general namespace
lookup first.

Shape membership alone is not enough: assigning a new value to an existing
`__builtins__` slot changes future builtins resolution even though the slot
location is unchanged.

The module object will eventually need a dedicated builtins-binding validity
cell, or equivalent flag, for the `__builtins__` slot:

```text
insert __builtins__ -> invalidate builtins-binding validity
assign __builtins__ -> invalidate builtins-binding validity
delete __builtins__ -> invalidate builtins-binding validity
```

This is similar in spirit to special handling for an instance `__class__` slot:
the name is Python-visible, but mutation also changes hidden runtime
assumptions.

There are two important timings:

- module code resolves builtins for the module execution frame from the current
  module `__builtins__` binding
- function objects capture the resolved builtins namespace when the function is
  created

That means changing module `__builtins__` can affect later module execution and
functions created after the change, but it does not necessarily change builtin
lookup for functions that already captured the previous builtins namespace.

Then `LOAD_GLOBAL name` uses:

```text
frame.defining_module.namespace[name]
frame.builtins_namespace[name]
```

The exact storage may be shape-backed module storage, a Python dict-like
namespace, or a future namespace wrapper. The semantic operation is mapping
lookup, not module attribute lookup.

The important field names should remain semantic. `CodeObject` needs a defining
module reference, not a generic namespace search path:

```cpp
MemberHeapPtr<ModuleObject> defining_module;
```

Resolved builtins belong on the function/frame side of execution context, not on
`CodeObject`. There are exactly two namespaces in normal global lookup, and they
have different meanings. The first is named for the module that defined the code,
not for the `LOAD_GLOBAL` operation that happens to use that module's namespace.
The second is not necessarily a module.

## Module Objects

Module objects should be runtime objects with ordinary object identity and
shape-backed attribute storage.

That lets these operations share one backing namespace:

- `module.x`
- `globals()["x"]` in that module
- top-level module stores
- global lookup's first namespace probe
- the module's eventual `__dict__` mapping view

The storage can be shared, but the lookup operation must remain distinct:

```text
module.x:
  object attribute lookup on the module object
  module class / descriptor / __getattribute__ behavior
  module-level __getattr__ fallback when supported

LOAD_GLOBAL x:
  globals namespace lookup
  builtins namespace lookup
  no module class lookup
  no descriptor protocol
  no module-level __getattr__
```

This is the important boundary. Attribute access should benefit from the normal
object model. Global lookup should remain a namespace operation with builtin
fallback.

## Class Bodies

Class scopes are their own case.

They are closer to functions in the compiler because a class body is a distinct
code block with its own symbol analysis. But only function locals and outer
function locals are lexical in the sense that name access becomes a fixed
frame slot or closure cell. Module names are late-bound in module namespaces,
and class body names are also dynamic.

For the module refactor, class bodies do not need to change immediately. The
current implementation can keep compiling class locals to frame slots and
harvesting those slots into the class object at `BuildClass`. The design should
still leave room for the later Python-compatible class namespace path.

The intended future class-body execution model is:

```text
active class namespace
defining module namespace
builtins namespace
```

where the active class namespace is usually a temporary dict-like namespace
created before the class body executes. It is not the final class object. After
the body finishes, class construction consumes that namespace and transfers its
contents into the `ClassObject`.

Class body lookup is roughly:

```text
LOAD_NAME name:
  active class namespace
  defining module namespace
  builtins namespace

STORE_NAME name:
  active class namespace only

DEL_NAME name:
  active class namespace only
```

Class-body locals are not ordinary closure parents for nested methods:

```python
class C:
    x = 1

    def f(self):
        return x
```

The `x` inside `f` is not `C.x` and is not captured from the class body. It is a
global lookup unless other binding rules classify it differently.

The `__class__` cell needed for zero-argument `super()` and direct `__class__`
references is a special case, not evidence that class scopes should behave like
ordinary function closure scopes.

This means future `LOAD_NAME` can reuse the same global lookup helper as
`LOAD_GLOBAL` after it checks the active class namespace:

```text
if active_class_namespace contains name:
  return active_class_namespace[name]

return load_global_from_defining_module_then_builtins(name)
```

## LoadGlobal Inline Cache

`LOAD_GLOBAL` should have a dedicated inline cache. It can reuse shaped storage
and validity cells, but it should not route through the generic attribute cache.

Useful cache states are:

```text
uninitialized
globals hit
builtins hit through globals miss
missing in both namespaces
```

A globals hit depends on:

- the globals namespace still having the name in the same slot
- the slot value being live

A builtins hit depends on:

- the globals namespace still not having the name
- the builtins namespace still having the name in the same slot

A missing result depends on:

- the globals namespace still not having the name
- the builtins namespace still not having the name

Mutations should invalidate the appropriate assumptions:

- adding a module/global name invalidates cached misses for that name
- deleting a module/global name invalidates cached globals hits and may reveal a
  builtin
- updating an existing global slot should not require a shape transition when
  the slot remains live
- mutating builtins invalidates builtin-hit and builtin-miss assumptions
- replacing or changing `__builtins__` must invalidate any cached frame/module
  builtins resolution that depended on the previous namespace; this requires the
  dedicated builtins-binding validity cell because assigning `__builtins__` can
  preserve the same module shape and slot location

## Large Module Shape Lookup

Shapes currently use an ordered descriptor walk. That is a good first
representation for ordinary instances and classes:

- most objects have few own properties
- the descriptor payload stays compact
- hot attribute sites are expected to reach inline-cache fast paths

Modules can grow much larger. A module that imports many names, especially with
`from other_module import *`, can have enough globals that cold name lookup
through a linear descriptor walk becomes visible.

The shape representation should therefore leave room for an optional side index:

```text
Shape
  ordered descriptors
  optional descriptor index
```

The ordered descriptor array remains authoritative for semantics and iteration
order. The optional index is only an acceleration structure:

```text
interned string/name -> descriptor index
```

It should not store values and should not define insertion order. It only maps
from a name to the descriptor position under a particular shape.

The index can be built lazily once a shape crosses a threshold:

```text
small shape:
  linear descriptor scan

large shape:
  shape side index lookup, then descriptor validation/access
```

This can be shape-owned rather than module-owned so large classes or other large
objects can benefit too. It must preserve shape immutability: either build it
before publishing the shape or treat it as a mutable side cache like transition
metadata, with no effect on semantic shape identity.

Constraints:

- descriptor order stays in the ordered descriptor array
- the index stores descriptor indices, not values
- delete/reinsert behavior still creates successor shapes with correct order
- string-key lookup remains VM-internal and does not invoke Python `__hash__` or
  `__eq__`
- `LOAD_GLOBAL` inline caches still guard the namespace shape and any validity
  cells; the side index only speeds up cache misses and slow-path resolution

## JIT Slot Loads

The global lookup cache should primarily prove lookup location, not callee
identity:

```text
name lookup -> storage location, under shape and validity guards
```

A JIT that wants to inline a function loaded from a global should layer a value
identity guard on top:

```text
guard defining module shape / lookup validity
load value from resolved storage location
guard value == expected function
inline expected function
```

This keeps lookup stability separate from value stability. Rebinding a global to
a different function can leave the same shape and slot location in place; the
lookup cache remains valid, while the JIT's function-identity guard fails and
falls back or re-specializes.

At specialization time the shape gives the storage kind and index. The JIT can
therefore specialize the slot load itself:

```text
inline/fixed slot:
  guard shape / validity
  load directly from stable module slot
  guard value identity if inlining depends on it

overflow slot:
  guard shape / validity
  load module overflow storage
  load overflow[index]
  guard value identity if inlining depends on it
```

Raw `Value *` cell caching is only generally safe for storage whose address is
stable. Ordinary overflow storage may move, so overflow locations should be
cached as a storage-location descriptor rather than as a raw pointer.

Module objects should probably start with a larger inline slot budget than
ordinary instances. Module globals are lookup-heavy and JIT-sensitive, and many
important names should fit without spilling:

- locally defined functions and classes
- imported hot functions
- names introduced by `from module import *`
- module constants
- `__name__`
- `__builtins__`
- common shadowed builtins

Keeping those names in fixed or inline slots lets interpreter caches and future
JIT code avoid overflow indirection in the common case, while preserving the
same shape-based semantics for names that do spill.

The inline budget should not assume that important names are always created
early. Star imports and generated module initialization can introduce a large
batch of important names after the module has already accumulated boilerplate
bindings. The layout policy should therefore provide enough module-specific
headroom, and later profiling may justify promotion or other storage tuning for
hot globals that land in overflow storage.

The first implementation should use a `ClassObject`-style fixed inline storage
layout with 256 module inline slots. Validity-cell fields should not be placed
where they can alias the inline slots; add that state later once the cache layout
is explicit.

## Refactor Direction

The preferred direction is to introduce an explicit `ModuleObject` concept first,
then split the current `Scope` responsibilities before adding module-shaped
global optimization. Without a module object, the refactor has no semantic home
for defining-module identity, module-owned globals, builtins resolution state,
future import records, or module attribute access.

Suggested layering:

```text
SymbolScope
  compiler-side lexical structure
  parent chain for function/class/module block analysis
  maps names to stable slot identities
  no Python-visible runtime storage contract

FrameLocals
  call-frame slots
  closure cells
  used by LOAD_FAST and LOAD_DEREF

Namespace
  mapping-like runtime storage
  used by module globals, class body locals, and exec globals
  may be backed by shape storage or by dict storage

ModuleObject
  Python-visible object
  owns a Namespace as its attribute/global storage
```

Module top-level code can still have a root symbol scope for classification and
diagnostics, but that root should not be the runtime parent of function locals.
At runtime the module frame should instead carry explicit namespace references:

```text
module frame:
  locals module namespace = module namespace
  defining module namespace = module namespace
  builtins namespace = resolved from module __builtins__

function frame:
  fast locals
  closure cells
  defining module namespace = function.defining_module.namespace
  builtins namespace = function.builtins_namespace

class frame:
  active locals namespace = temporary class namespace
  defining module namespace = defining module namespace
  builtins namespace = resolved builtins namespace
```

The class-frame entry above is the target shape, not a requirement for the first
module refactor. Until `LOAD_NAME` / `STORE_NAME` / `DEL_NAME` exist, class
bodies may continue using the current frame-slot representation and
`BuildClass` slot-harvesting path.

Recommended staging:

```text
stage 0:
  introduce ModuleObject as a runtime object with identity
  give each module object an owned namespace/global storage reference
  create a module object for startup and interactive execution
  thread ModuleObject references through CodeObject, Function, and Frame context
  keep current lookup behavior as much as possible while the context is moved

stage 1:
  replace module Scope parent lookup with defining_module + builtins_namespace
  move module globals onto module-owned namespace/storage
  keep class bodies on the existing local-slot harvest path

stage 2:
  integrate ModuleObject storage with shape-backed object storage where useful
  align module.x, globals(), top-level stores, and eventual __dict__ over the
  module-owned namespace without changing LOAD_GLOBAL into attribute lookup

stage 3:
  add active_locals_namespace to frames
  add LOAD_NAME / STORE_NAME / DEL_NAME
  compile class bodies to name ops
  make class-body locals() return active_locals_namespace
  make BuildClass consume the active class namespace
```

## Non-Goals

This refactor should not:

- put shapes on function locals
- make module global lookup invoke module `__getattribute__`
- expose builtin fallback through module attributes or `module.__dict__`
- make functions search caller modules
- make class-body locals ordinary closure parents for methods
- force Python `dict`, symbol scopes, and object shapes into one concrete table
  type

## Open Questions

- Whether the first `Namespace` implementation should be a new type backed by
  shaped object storage, dict-like storage, or a temporary extraction of the
  current scope storage pieces. The end state should not leave value storage,
  runtime namespace entries, or parent lookup indirection on `Scope`; ordered
  name/slot metadata may remain there for scope-derived dictionary presentation.
- Whether builtins should be cached on functions, modules, frames, or only in
  `LOAD_GLOBAL` inline-cache entries.
- How interactive mode should preserve its current shared module namespace while
  no longer exposing it as a lexical parent `Scope`.
- How soon `module.__dict__` should become a real mapping view over the module
  namespace rather than a placeholder API.

## Summary

Modules are not lexical scopes in the same sense as function and class symbol
scopes. They are runtime objects with mutable namespaces.

The compiler should classify names statically and emit the right operation.
`LOAD_FAST` and `LOAD_DEREF` use frame-local static storage. `LOAD_GLOBAL` uses
the frame's globals and builtins namespaces at runtime. Module attribute access
uses object attribute semantics over the same module-owned storage.

That gives module globals and module attributes a shared backing store without
collapsing their different Python lookup rules.
