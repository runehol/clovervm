# Import System Design

This document sketches a staged design for Python module imports in clovervm.
It builds on the existing module-global namespace work described in
[Module Global Namespace Design](module-global-namespace-design.md).

The main design constraint is that Python import statements are user-observable:
they call `builtins.__import__`, and user code can replace that function. The
runtime should therefore expose CPython-compatible import entry points without
letting the awkward public `__import__` signature become the internal module
loader architecture.

## Goals

- Support ordinary Python import statements through `builtins.__import__`.
- Preserve the user-observable ability to replace `builtins.__import__`.
- Add `globals()` as the visible interface to the current frame's module global
  namespace.
- Keep the core import mechanism separate from the public `__import__`
  compatibility wrapper.
- Reuse module-object shape-backed storage for module globals instead of adding
  a separate dictionary-owned namespace.
- Preserve CPython's essential import invariants, especially `sys.modules`
  caching and inserting a module before executing its body.

## Non-Goals

- Implement the full CPython importlib protocol in the first step.
- Require module globals to be an exact Python `dict`.
- Add namespace packages, zip imports, extension modules, or bytecode cache
  handling in the first implementation.
- Make global lookup invoke module attribute lookup or descriptors.
- Model the core loader around arbitrary `globals` and `locals` dictionaries.

## Current Foundation

Module globals already live on `ModuleObject`. The module object uses
shape-backed object storage, and top-level global load/store/delete operations
operate on that module storage directly.

That gives imports the right storage substrate:

```text
ModuleObject
  shape-backed own properties
  __name__
  __builtins__
  standard import metadata
```

Implemented pieces used by the import design:

- `globals()` is a builtin implemented through trusted builtins code.
- Module-scope `locals()` is implemented through the same trusted builtins /
  intrinsic path.
- The trusted `__clover_globals__()` and `__clover_locals__()` helpers lower to
  interpreter intrinsics, so ordinary user code cannot call the helpers by name.
- The intrinsics return live `SlotDict` views over the caller's defining module
  storage when the caller is module code.
- A VM-owned immortal `sys` module exists with `sys.modules` and `sys.path`.
- `sys.modules` is the VM-owned imported-modules cache, initially containing
  `"builtins"` and `"sys"`.
- `sys.path` is a mutable list initialized to `[".", CL_STDLIB_DIR]`.
- `CL_STDLIB_DIR` is an absolute build-configured path to `stdlib/`.
- Trusted VM bootstrap code stays in `src/builtins.py`; ordinary importable
  system modules live under `stdlib/`.
- `__main__` is created as a real module, receives the standard entry-module
  metadata, and is inserted into `sys.modules`.
- Source modules and packages are found through an internal C++ source finder,
  preinserted into `sys.modules`, executed in their module object, and removed
  from `sys.modules` on load failure.
- Dotted absolute imports, regular packages, package `__path__`, submodule
  parent binding, relative `from` imports, import aliases, comma import lists,
  and parenthesized `from` import lists are implemented.

## CPython Import Sequence

This section describes the CPython sequence clovervm should use as the semantic
reference. The exact internal implementation can be different, but the
observable order matters.

An import statement does two jobs:

```text
search/load the requested module
bind names in the current local namespace
```

Only the search/load phase is performed by `__import__`. The statement's
compiler/runtime lowering performs the final local binding from the return value
of `__import__`.

For a plain import:

```python
import pkg.mod
```

the public call shape is:

```text
__import__("pkg.mod", globals, locals, fromlist, level)
```

where `fromlist` is empty or `None`, and `level` is `0` for an absolute import.
For a from-import:

```python
from pkg.mod import name
```

the call uses a non-empty `fromlist`, and `__import__` returns the named module
rather than the top-level package.

### 1. Resolve Absolute Or Relative Name

The importer first determines the fully qualified absolute module name.

For absolute imports, `level == 0` and the requested name is already absolute:

```text
name = "pkg.mod"
absolute_name = "pkg.mod"
```

For explicit relative imports, `level > 0`. The package context is derived from
the caller globals passed to `__import__`. CPython's modern source of truth is
`globals["__spec__"].parent`; `globals["__package__"]` is a deprecated fallback,
and older fallback behavior can derive context from `__name__` and whether
`__path__` is present.

Examples:

```python
from . import sibling
from ..parent import item
```

Relative imports are only syntax-valid in `from ... import ...` form. Plain
`import .sibling` is not valid Python syntax.

If the requested relative import walks above the top-level package, or if there
is no known parent package context, import fails before searching for a module.

### 2. Check `sys.modules`

The first real lookup is `sys.modules[absolute_name]`.

Cases:

- if the name exists and the value is a module object, that value satisfies the
  import
- if the name exists and the value is `None`, import raises
  `ModuleNotFoundError`
- if the name is absent, import continues with finder search

`sys.modules` is a writable cache, not just an implementation detail. User code
can delete entries or assign `None`, and imports must observe those changes.

### 3. Import Parent Packages

For a dotted name:

```text
pkg.mod.child
```

CPython imports parents first:

```text
pkg
pkg.mod
pkg.mod.child
```

Each parent import has its own `sys.modules` check and finder search. Once the
parent module exists, submodule search uses the parent package's `__path__`.

If an intermediate parent is not a package, import fails. In Python import
terms, a package is a module with `__path__`.

### 4. Search `sys.meta_path`

If the fully qualified name is not in `sys.modules`, import walks
`sys.meta_path` in order.

Each meta path finder is asked for a module spec:

```text
finder.find_spec(fullname, path, target=None)
```

The `path` argument is:

- `None` for top-level imports
- the parent package's `__path__` for submodule imports

The `target` argument is only used for reload. A finder returns a spec if it can
handle the module, returns `None` to let the search continue, or raises to abort
the import.

CPython's default meta path includes:

- a built-in module finder
- a frozen module finder
- the path-based finder

### 5. Module Specs

A module spec is the import-time record returned by a finder. It is exposed on
the loaded module as `module.__spec__`.

The spec carries information such as:

- `name`: fully qualified module name
- `loader`: object responsible for creating/executing the module
- `origin`: where the module came from, if meaningful
- `cached`: expected bytecode-cache path, if any
- `parent`: containing package name
- `submodule_search_locations`: package search path, or `None` for non-packages
- `has_location`: whether `origin` is a loadable location
- `loader_state`: finder/loader-specific private state

The important split is that finders return specs; loaders execute modules. A
finder and loader may be the same object, but the protocol roles are separate.

### 6. Path-Based Finder And Path Entry Finders

The path-based finder is itself a meta path finder, but it delegates the actual
search of filesystem-like locations to path entry finders.

Inputs:

- `sys.path` for top-level imports
- parent package `__path__` for submodule imports
- `sys.path_hooks` to turn path entries into path entry finders
- `sys.path_importer_cache` to cache the finder selected for each path entry

For each path entry, the path-based finder gets or creates a path entry finder.
That path entry finder then tries to produce a module spec for the requested
module. Default path entry finders know about source files, bytecode files,
extension modules, packages, and, when enabled, zip imports.

For a first clovervm implementation, we do not need the full hook/cache
protocol. The design point to keep is that path search is just one meta path
finder strategy, not the whole import system.

### 7. Create The Module

Once a spec is found, loading starts.

If the loader provides `create_module(spec)`, import calls it. If that returns a
module, that module is used. If it returns `None`, or if the loader has no
custom creation path, import creates a normal module object for `spec.name`.

The loader should not need to initialize the standard import metadata itself;
the import machinery fills that in before execution.

### 8. Initialize Import Metadata

Before executing the module body, import initializes import-related dunder
attributes from the spec.

Important module attributes:

- `__name__`: fully qualified module name; usually equals `__spec__.name`
- `__spec__`: the module spec used for import
- `__package__`: package context for relative imports; usually equals
  `__spec__.parent`, with top-level modules using the empty string
- `__loader__`: loader used to load the module; usually equals
  `__spec__.loader`
- `__path__`: present only on packages; search locations for submodules;
  corresponds to `__spec__.submodule_search_locations`
- `__file__`: optional pathname for file-backed modules; derived from spec
  location/origin when meaningful
- `__cached__`: optional pathname for the bytecode cache; derived from
  `__spec__.cached` when meaningful
- `__builtins__`: builtins binding used by code executing in the module
- `__doc__`: module docstring, or `None`

CPython is moving users toward `__spec__` and its attributes instead of the
individual legacy import attributes. For clovervm, setting the legacy attributes
is still useful because user code and import hooks can observe them.

### 9. Insert Into `sys.modules`

The new module is inserted into `sys.modules[spec.name]` before its body is
executed.

This is non-negotiable for Python semantics. It allows recursive imports to see
the partially initialized module and prevents unbounded recursive loading.

### 10. Execute The Module

The loader executes the module:

```text
spec.loader.exec_module(module)
```

For source modules, this means executing the compiled code with the module's
global namespace as the execution globals.

If execution fails, the failing module entry is removed from `sys.modules`, but
only that failing module entry. Modules that were already cached before the
attempt, and modules successfully loaded as side effects, remain cached.

After execution, CPython returns the module currently stored in
`sys.modules[spec.name]`, not necessarily the exact object that was passed to
`exec_module`. This permits unusual loaders/modules to replace themselves in
`sys.modules`.

### 11. Bind Submodules On Parent Packages

When a submodule is loaded, the child module is bound as an attribute of the
parent package.

The invariant is:

```text
sys.modules["pkg"] exists
sys.modules["pkg.mod"] exists
=> sys.modules["pkg"].mod is sys.modules["pkg.mod"]
```

This binding happens regardless of whether the import was requested with
`import`, `from ... import ...`, `importlib`, or `__import__`.

### 12. Return To Import Statement Binding

Finally, `__import__` returns a module according to its historical return rules.

For empty `fromlist`:

```python
__import__("pkg.mod", globals(), locals(), (), 0)
# returns pkg
```

For non-empty `fromlist`:

```python
__import__("pkg.mod", globals(), locals(), ("name",), 0)
# returns pkg.mod
```

The import statement then performs local name binding from that return value.

## Packages

All packages are modules, but not all modules are packages. A package is a module
with `__path__`.

Regular packages are usually directories containing `__init__.py`. Importing the
package executes that `__init__.py` as the package module body, and the package's
`__path__` is then used for submodule search.

Namespace packages have no single `__init__.py`. They are composed from one or
more package portions found across import search locations. Their `__path__` is
a dynamic iterable of search locations. clovervm can defer namespace packages,
but the regular package design should not make them impossible later.

## CloverVM Bootstrap Subset

clovervm should not start by cloning all of `importlib`. The first import
mechanism should be small enough to bootstrap ordinary multi-file programs and
stdlib-like VM modules, while preserving the same conceptual boundaries CPython
uses.

The subset should implement these invariants first:

- import statements call the mutable `builtins.__import__`
- `__import__` receives CPython-shaped arguments
- absolute module names are resolved and loaded
- `sys.modules` is authoritative and user-visible
- modules are inserted into `sys.modules` before execution
- failed execution removes only the failing module entry
- source modules execute with their module globals mapping as globals
- regular packages are represented as modules with `__path__`
- submodules are bound onto parent packages after successful load

Everything else can be staged behind interfaces that resemble the full system
without immediately exposing all of it to Python code.

### Bootstrap Search Model

The first finder can be an internal source-path finder, not a full public
`sys.meta_path` implementation.

Inputs:

```text
VM import path for top-level modules
parent package __path__ for submodules
module name
```

Supported file shapes:

```text
name.py
name/__init__.py
```

For `import pkg.mod`, the bootstrap importer should:

1. import `pkg`
2. require `pkg` to have `__path__`
3. search `pkg.__path__` for `mod.py` or `mod/__init__.py`
4. load `pkg.mod`
5. bind the loaded child module onto the parent package under the final
   component name

This gives us regular packages without needing namespace packages, path hooks,
zip import, frozen modules, extension modules, or user-defined finders.

### Internal Spec Object

Even the bootstrap importer should produce an internal module spec record.
It does not need to be a complete Python-visible `ModuleSpec` class at first.

Minimum fields:

```text
name
kind: source module | regular package | builtin module
origin
source_path
cached_path, optional and initially None
loader, initially an internal loader enum/object
parent
submodule_search_locations, None for non-packages
```

This lets the loader and module initializer share one structured record, and it
leaves room to expose a Python `ModuleSpec` object later.

The current bootstrap path records this as a C++ `ModuleSpec` and still exposes
`module.__spec__ = None`. That is acceptable only as a temporary bootstrap
state. Relative imports currently use the legacy module metadata path, but
module repr, later `importlib`, and more exact CPython compatibility all want a
real Python-visible spec object.

### Bootstrap Metadata

For source modules, initialize:

```text
__name__      = fully qualified module name
__spec__      = None today; later, an internal/small spec object
__package__   = spec.parent for normal modules, spec.name for packages,
                empty string for top-level non-packages
__loader__    = None today; later, an internal source loader object or marker
__file__      = source path
__cached__    = None or omitted until bytecode cache exists
__builtins__  = VM builtins module
__doc__       = None initially, then module execution may overwrite it
```

For regular packages:

```text
__path__ = list containing the package directory
```

The current relative-import implementation uses `__package__`, `__name__`, and
`__path__` rather than `__spec__`. Moving to a visible spec object should keep
those legacy attributes coherent with the spec.

### `sys` Bootstrap

The first import system needs enough `sys` to host:

```text
sys.modules
sys.path
```

`sys.modules` must be a mutable mapping visible to Python code. `sys.path` can
start as a VM-owned list of strings, with no path hooks. We can defer:

```text
sys.meta_path
sys.path_hooks
sys.path_importer_cache
```

However, the internal code should still be shaped as:

```text
find_spec(fullname, path)
load_from_spec(spec)
```

That way `sys.meta_path` can become a Python-visible list of finder objects
without rewriting the loader.

### `builtins.__import__` In The Subset

The bootstrap `__import__` now supports:

```python
import mod
import pkg.mod
from pkg import mod
from pkg.mod import name
from . import sibling
from ..pkg import name
```

Import aliases, comma import lists, parenthesized `from` import lists, and
explicit relative `from` imports are also implemented.

The important remaining statement form is:

```python
from module import *
```

Star imports are a name-binding feature layered over the same loader behavior,
but they need their own semantics for `__all__`, underscore filtering, and the
set of local/global names that may be updated.

The builtin accepts the full public signature:

```python
__import__(name, globals=None, locals=None, fromlist=(), level=0)
```

The return rules match CPython:

```text
empty fromlist -> return top-level package/module
non-empty fromlist -> return requested module
```

This is cheap to get right early and hard to change later if tests and user code
grow around different behavior.

### Deferrals

The bootstrap importer should deliberately defer:

- public `importlib`
- public `sys.meta_path`
- user-defined finders and loaders
- `sys.path_hooks`
- `sys.path_importer_cache`
- namespace packages
- zip imports
- extension modules
- frozen modules, unless needed for VM boot
- `.pyc` loading and invalidation
- `importlib.reload`
- loader replacement of the module object through `sys.modules`
- exact CPython `ModuleSpec` type compatibility
- exact `dict` requirement for module globals

These are not design dead ends if the initial importer keeps finder, spec, and
loader concepts separated internally.

### Compatibility Boundaries To Preserve

The subset must preserve these boundaries even if the implementation is small:

- `__import__` is public and hookable
- core import loading is not modeled around arbitrary `globals` and `locals`
- module search returns a spec-like record
- module execution consumes a module object and a spec-like record
- package-ness is represented by `__path__` /
  `spec.submodule_search_locations`
- `sys.modules` is checked before finder search and updated before execution

If we keep those boundaries, the full system can grow by replacing the internal
source finder with a real `sys.meta_path` traversal, replacing the internal
loader marker with Python-visible loader objects, and exposing the spec as a
proper `ModuleSpec`.

## Frame Globals

Every executing frame should distinguish its global namespace from its local
execution storage.

For module code:

```text
frame.globals = module globals mapping
frame.locals  = module globals mapping
```

For function code:

```text
frame.globals = defining module globals mapping
frame.locals  = function frame locals
```

This matches the existing rule that functions resolve globals through their
defining module, not through their caller's module. It also gives import
statements the data needed to call `__import__` with CPython's public argument
shape.

The frame-global concept is the real dependency. The `globals()` builtin is only
the user-visible accessor for it.

## Module Globals Mapping

`globals()` should return a live mutable mapping view over the current module's
shape-backed storage. The mapping should be a thin wrapper around a
`ModuleObject` or other object that owns named slots.

Conceptually:

```text
ModuleGlobalsMapping
  target: ModuleObject
```

Mapping operations should delegate to the same object-storage invariants used
by module global operations:

```text
mapping read
  -> target module own-name lookup

mapping write
  -> target module own-name store

mapping delete
  -> target module own-name delete, if deletion is supported
```

The mapping should not manually duplicate shape transition rules throughout the
runtime. It may be thin, but it should still go through a small object/module
storage API so ownership, write barriers, invalidation, and shape transitions
stay centralized.

Minimum useful behavior:

```python
x = 1
assert globals()["x"] == 1

globals()["y"] = 2
assert y == 2

"__name__" in globals()
globals().get("__package__")
```

Deletion should only be exposed if it can delegate to real module global
deletion. If the current object API cannot delete own named storage cleanly, the
first version should leave mapping deletion unsupported rather than fake it.

### Identity

The first implementation returns fresh `SlotDict` view objects over the same
module storage. Mutating any view mutates the module namespace, but view identity
is not stable:

```python
globals() is globals()
# false in clovervm's current implementation
```

This differs from CPython, where `globals()` returns the module dictionary
itself. Stable identity can be revisited with `module.__dict__` if exact dict
compatibility becomes a goal.

## `globals()` Builtin

`globals()` returns a live `SlotDict` view over the current frame's global
mapping.

Today that global mapping is the caller code object's defining module storage.
Future `exec` support may require code-object cloning or another binding layer
for arbitrary globals mappings, but import bootstrapping can use the current
module-backed implementation.

Initial tests should cover:

- top-level `globals()["name"]` observes top-level assignment
- assignment through `globals()` creates a module global
- a function's `globals()` returns its defining module's global mapping
- builtin fallback names do not appear as module own mapping entries

## `locals()` Builtin

Module-scope `locals()` currently returns the same kind of fresh live `SlotDict`
view as `globals()`.

For code objects with a non-null local scope, `locals()` currently raises
`UnimplementedError`. That is intentionally incomplete for functions and class
bodies. The next correct versions are:

- function code: materialize a snapshot from scope metadata and frame storage
- class body code: return the active class namespace mapping

The module-scope behavior is enough for the first import bootstrap because
import statements can pass a real locals mapping from module code without
pretending that fast locals or class namespaces are already correct.

## Core Import API

The internal loader should be the bootstrap subset's `find_spec` /
`load_from_spec` path, not a direct implementation of the public `__import__`
signature. It should use a structured API that separates module resolution from
compatibility details.

The core importer accepts already-derived import context:

```text
ImportRequest
  name
  level
  fromlist
  package context, when relative import is requested
```

Or expose narrower helpers:

```text
import_absolute(name, fromlist)
import_relative(name, package_context, level, fromlist)
```

The core importer owns:

- looking in `sys.modules`
- finding source modules through the bootstrap source-path finder
- creating module objects
- initializing import metadata
- inserting the module in `sys.modules` before execution
- executing the module body
- removing only the failing module from `sys.modules` on execution failure
- binding imported submodules onto parent package objects

This separation keeps the loader testable without forcing every internal caller
to manufacture Python `globals` and `locals` objects. It also gives the later
public `sys.meta_path` implementation a place to plug in without replacing
module execution.

## `builtins.__import__`

The public builtin should be a compatibility wrapper:

```python
__import__(name, globals=None, locals=None, fromlist=(), level=0)
```

Its job is to:

1. validate and normalize arguments
2. derive package context from `globals` when `level > 0`
3. call the core importer
4. apply CPython-compatible return rules

For relative imports, package context comes from the caller globals mapping,
using CPython-like metadata:

```text
__package__
__spec__
__name__
__path__
```

For absolute imports, the core loader should not need caller `locals`, and
usually should not need caller `globals` except for compatibility errors and
future import behavior.

### Return Rules

The wrapper must preserve the strange but user-visible `__import__` return
contract:

```python
__import__("a.b", globals(), locals(), (), 0)
# returns module a

__import__("a.b", globals(), locals(), ("x",), 0)
# returns module a.b
```

This is why user code is usually better served by `importlib.import_module`, but
the import statement itself still uses `__import__`.

## Import Statement Execution

Import bytecode/runtime execution should call `builtins.__import__`, not the
internal importer directly.

For:

```python
import pathlib
```

the call shape is:

```text
__import__("pathlib", frame.globals, frame.locals, None, 0)
```

For:

```python
from os.path import join
```

the call shape is:

```text
__import__("os.path", frame.globals, frame.locals, ("join",), 0)
```

This preserves import hooks:

```python
__builtins__.__import__ = hook
import some_module
```

The VM's import statement path should therefore use ordinary global/builtin
lookup semantics to find `__import__`, or an equivalent builtins lookup that
observes mutations to the builtins module.

## Current Implementation Snapshot

The current implementation has the bootstrap import spine in place:

- `ModuleObject` construction installs standard metadata up front.
- `__main__` is a real module inserted into `sys.modules`.
- `sys` is a VM-owned module exposing `sys.modules` and `sys.path`.
- `sys.modules` is the VM-owned imported-modules cache.
- `sys.path` starts as `[".", CL_STDLIB_DIR]`.
- The C++ source finder walks `sys.path` or parent package `__path__`, ignores
  non-string path entries, and recognizes `name.py` and `name/__init__.py`.
- The C++ `ModuleSpec` records name, origin, package state, and package search
  locations, but it is not yet Python-visible.
- Source modules are inserted into `sys.modules` before read/compile/execute and
  removed on load failure.
- Regular packages receive `__path__`.
- Dotted imports import parents left to right, require package parents, search
  parent `__path__`, and bind loaded submodules onto their parent package.
- `builtins.__import__` is a native builtin with the CPython-shaped public
  signature.
- Import statement bytecode calls the public import hook and binds names through
  the normal store path.
- Absolute imports, dotted imports, from-imports, aliases, comma import lists,
  parenthesized from-import lists, and explicit relative from-imports are
  implemented.

## Remaining Checklist

### Import Statement Forms

- Implement `from module import *`.
- Use `module.__all__` for star import when present.
- Without `__all__`, star import should import public names that do not start
  with `_`.
- Reject or handle function-scope star import according to Python syntax rules.
- Store star-imported names through the same normal binding path as other
  imports.

### Python-Visible Specs And Loaders

- Replace exposed `module.__spec__ = None` with a small Python-visible
  `ModuleSpec`-like object.
- Give modules a meaningful `__loader__` value instead of `None`.
- Add `__cached__` as `None` or a real cache path once the spec surface has a
  place for it.
- Keep the C++ spec-to-module construction path as the internal source of truth.

### Builtin Module Importer

- Add an internal builtin-module finder/loader path.
- Let `import sys` and `import builtins` be satisfied by finder/loader behavior,
  not only by their initial `sys.modules` entries.
- Decide the first Python-visible metadata for builtin modules: origin, loader,
  package, and file absence.

### `sys` And Importlib Surface

- Add public `sys.meta_path` once finder objects exist.
- Add `sys.path_hooks` and `sys.path_importer_cache` when path entry finders are
  split out of the hardcoded source finder.
- Start a minimal `importlib` surface only after specs/loaders are objects.

### Loader Semantics

- Match CPython's behavior when a loader replaces its module in `sys.modules`.
- Preserve already-existing `sys.modules` entries on failed reload/import
  attempts.
- Add import reentrancy/import-lock semantics if recursive imports expose a
  correctness hole.

### Module Namespace Compatibility

- Decide whether and when `module.__dict__` should expose a stable mapping
  identity.
- Decide whether exact `dict` globals are needed for `exec` compatibility, and
  how that interacts with shape-backed module storage.
- Finish function and class-body `locals()` semantics independently of import.

### Search Backends

- Add namespace packages.
- Add bytecode cache loading and invalidation.
- Add frozen modules if VM bootstrap starts needing them.
- Add extension modules only after the native ABI/module initialization design
  exists.
- Add zip imports only after path hooks and path entry finders exist.

## Remaining Tests

Prefer interpreter tests for user-visible import semantics:

- `from module import *` uses `__all__` when present.
- `from module import *` imports public non-underscore names when `__all__` is
  absent.
- `from module import *` stores into module globals through the normal store
  path.
- star import rejects or avoids unsupported function/class scope according to
  the parser/codegen rule we choose.
- builtin module imports work after deleting `sys.modules["sys"]` or
  `sys.modules["builtins"]`, once the builtin finder exists.
- Python-visible `__spec__` exposes the minimum fields needed by relative
  imports and user inspection.
- loader replacement through `sys.modules` returns the replacement module once
  that behavior is implemented.

Add lower-level C++ tests only where interpreter tests cannot pin the intended
structure, such as direct spec fields, finder ordering, or module-storage
invariants.

## Design Risks

The largest risk is pretending the globals mapping is a normal dictionary too
early. CPython exposes a real dict, but clovervm's module namespace is
shape-backed object storage. The first implementation should expose the mapping
operations that import and ordinary Python code need, while leaving exact-dict
compatibility as a separate decision.

The second risk is letting `__import__` leak into the loader architecture.
`globals` and `locals` belong at the Python compatibility boundary. The core
loader should receive explicit import context, not arbitrary namespace objects.

The third risk is bypassing object-storage invariants from the globals mapping.
Direct slot lookup is fine as an implementation detail, but it must not bypass
shape invalidation, ownership semantics, or future cache correctness.
