# Native C Modules

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Implemented |
| Scope | Building, locating, loading, and initializing native extension modules |
| Owning layers | Native module build, dynamic loader, import system, public C API, and runtime modules |
| Validated against | `8b76359` (2026-05-24) |
| Supersedes | N/A |

This document describes how clovervm should build, locate, and load native C
extension modules. The C API itself is described separately in
[Clover C API](clover-c-api.md).

The native-module system has two goals:

- let standard-library Python modules depend on narrow native primitives through
  ordinary imports
- keep dynamic native modules out of VM C++ internals by loading them through
  the builder-based Clover C API

## Module Layout

Public modules should usually stay in Python. Native modules should usually be
private implementation modules with leading underscores.

```text
stdlib/time.py
  imports _time
  defines public API and policy

native_modules/_time.c
  defines clover_module_init__time
  provides monotonic_ns, sleep, and other narrow primitives
```

The public Python module can re-export native primitives directly when no
wrapper policy is needed:

```python
from _time import monotonic_ns
from _time import sleep


def monotonic():
    return monotonic_ns() / 1_000_000_000
```

## Dynamic Library Names

Native modules should build to a clover-specific extension suffix so they are
not confused with CPython extension modules.

Initial platform names:

```text
_time.clover.dylib
_time.clover.so
_time.clover.dll
```

Plain `.so`, `.dylib`, or `.dll` aliases can be added later, but the first
implementation should prefer clover-specific names.

The initial supported dynamic-loader platforms should be Linux and macOS:

```text
Linux   -> .clover.so
macOS   -> .clover.dylib
Windows -> .clover.dll, deferred until the POSIX loader is working
```

## Init Symbols

Native modules export a module-specific init symbol. The symbol is derived from
the absolute module name by replacing `.` with `_` and prefixing
`clover_module_init_`.

```text
_time          -> clover_module_init__time
pkg._native    -> clover_module_init_pkg__native
pkg.native_os  -> clover_module_init_pkg_native_os
```

Native module names are ASCII-only for now. Names that collide after replacing
dots with underscores are unsupported for native extension modules.

A single dynamic library may export multiple init symbols. Loading
`pkg._a` and `pkg._b` from the same dynamic library still creates separate
module objects and calls the name-specific init symbol for each module.

## Build Output Location

The source-tree `stdlib/` remains the home for importable Python standard
library code. Built native modules should not be written into the source tree.

Instead, CMake should create a build-tree stdlib directory:

```text
<build>/stdlib/
  _time.clover.dylib
  package/
    _native.clover.dylib
```

This directory is the build output for generated or compiled stdlib artifacts.
It can later host bytecode caches or generated support files if needed.

The configured runtime should know both paths:

```text
CL_STDLIB_DIR        = <source>/stdlib
CL_BUILD_STDLIB_DIR  = <build>/stdlib
```

`sys.path` should start with:

```python
["", CL_BUILD_STDLIB_DIR, CL_STDLIB_DIR]
```

The empty string means the current working directory at import time. The build
stdlib path comes before the source stdlib path so a compiled native module
backing a public Python wrapper is found without copying artifacts into the
source tree. Source modules still live under `CL_STDLIB_DIR`.

If `CL_BUILD_STDLIB_DIR` does not exist, startup can still include it in
`sys.path`; the finder will simply miss it.

Loaded native libraries stay loaded for the rest of the process. Clover keeps
the platform library handles in a VM-owned cache keyed by absolute origin path,
but it intentionally never calls `dlclose` manually.

## CMake Helper

The build system should expose a helper for native modules:

```cmake
clovervm_add_native_module(_time
    SOURCES
        native_modules/_time.c)
```

The helper should:

```text
create a CMake MODULE library target
compile against Clover C API headers
name the output with the platform clover extension suffix
write the output under ${CMAKE_BINARY_DIR}/stdlib
avoid linking a second copy of the VM into the module
avoid linking any Clover runtime provider into the module
apply project warning and sanitizer settings
make the module build as part of the normal stdlib/native-module target
```

The initial helper is named `clovervm_add_native_module`. It creates a target
named `clovervm_native_module_<cmake-safe import name>` and adds that target to
the aggregate `clovervm_native_modules` target. Module targets include only the
public `include/` headers, do not link `libclovervm`, and hide symbols by
default; init functions should use `CL_NATIVE_MODULE_EXPORT`.

For package-private modules, the helper should support an import-name/output
subdirectory distinction:

```cmake
clovervm_add_native_module(pkg._native
    SOURCES
        native_modules/pkg_native.c)
```

which outputs:

```text
<build>/stdlib/pkg/_native.clover.<platform suffix>
```

The helper can derive the init symbol name for validation or diagnostics, but
the compiled library is still responsible for exporting that symbol.

## Bring-Up Modules

Bring the native-module system up with one deterministic module first, then one
OS-facing module.

First target:

```text
stdlib/math.py
  from _math import sqrt

native_modules/_math.c
  exports clover_module_init__math
  registers sqrt
```

`math` is the best first module because `sqrt` already exists as a VM builtin
and has deterministic tests. Moving or duplicating that behavior through
`_math.sqrt` proves dynamic loading, function registration, argument
conversion, return values, and error propagation without clock or scheduler
noise.

Second target:

```text
stdlib/time.py
  from _time import monotonic_ns
  from _time import sleep

  def monotonic():
      return monotonic_ns() / 1_000_000_000

native_modules/_time.c
  exports clover_module_init__time
  registers monotonic_ns and sleep
```

`time` is a good second module because it exercises real OS calls and the
public-Python/private-native stdlib split. Tests should avoid relying on exact
sleep duration; `monotonic_ns()` can be tested for type and non-decreasing
behavior.

## Linkage Model

Native modules should not link against a Clover runtime library. They include
the public C API headers, export their module init symbol, and resolve Clover
and CPython-compatible extension API symbols from the active runtime provider
when they are loaded.

The active runtime provider is the single symbol and object-identity universe
for the process:

```text
normal provider mode:
  libclovervm owns the runtime and exports the embedder API plus extension API
  clovervm and embedding hosts link libclovervm

internal provider mode:
  an executable such as test_clovervm or bench_clovervm links VM objects
  directly for internal access and exports the extension API itself
```

In both modes, native modules are built the same way: they do not link
`libclovervm`, and they resolve extension API symbols from whichever provider
is active in the process. A process must not contain two independent Clover
extension API providers for the same VM.

`libclovervm` exports two symbol buckets:

```text
embedder API:
  clover_vm_new, clover_vm_destroy, clover_vm_run_file, ...

extension API:
  native module builder functions, future CPython C API shim functions, and
  identity-bearing globals such as Py_None or exception/type globals
```

Internal-provider executables export only the extension API bucket. They do not
need to expose the embedder API unless native extensions are allowed to call it.

## Portable Loading

The first portable loader should target POSIX dynamic loading on Linux and
macOS through `<dlfcn.h>`.

Platform mapping:

```text
Linux:
  open   -> dlopen(path, flags)
  symbol -> dlsym(handle, symbol_name)
  error  -> dlerror()

macOS:
  open   -> dlopen(path, flags)
  symbol -> dlsym(handle, symbol_name)
  error  -> dlerror()
```

Use `RTLD_NOW | RTLD_LOCAL` for the first implementation.

```text
RTLD_NOW:
  resolve missing symbols at import time rather than failing later when a
  native function is first called

RTLD_LOCAL:
  do not publish one extension module's symbols as global symbols for later
  extension modules
```

`RTLD_GLOBAL` should be avoided unless a concrete extension dependency use case
requires it. If native modules need to share code, prefer linking both modules
against an explicit shared support library.

`dlerror()` is process-global enough to be awkward. Loader code should clear it
before `dlsym`, read it immediately after failures, and copy the message into a
C++ string before doing any other dynamic-loader operation.

Dynamic library handles should stay open after a successful load. Closing a
library while Clover `Function` objects still point at callbacks from that
library would leave dangling native code pointers. A VM-owned cache keyed by
absolute library path is sufficient:

```text
path -> native library handle
```

The handle cache is not a module cache. `sys.modules` remains authoritative for
module objects, and reimport after deleting `sys.modules[name]` still creates a
fresh module object and calls the module-specific init symbol again.

On shutdown, the VM may either keep handles open until process exit or close
them after all managed objects are gone. The first implementation can keep them
open until process exit; correctness is more important than early unload.

Windows loading should be designed later with `LoadLibraryW`,
`GetProcAddress`, and `FreeLibrary`, without changing the higher-level native
module finder/loader contract.

## Finder Integration

The bootstrap finder currently recognizes source files, regular packages, and
single-portion namespace packages. Native modules should become another spec
kind:

```text
ModuleSpecKind::Extension
```

Extension specs should record:

```text
name
origin/path to dynamic library
is_package = false for the first implementation
loader kind = "extension"
has_location = true
```

Package-shaped native extensions can be deferred until there is a concrete
need. Public packages can use Python `__init__.py` files that import private
native submodules.

## Search Order

The first implementation should use an explicit, tested path-entry order.

For a path entry and module leaf name:

```text
leaf/__init__.py
leaf.py
leaf.clover.<platform suffix>
leaf/                         # namespace package
```

This order keeps regular Python packages and modules ahead of native modules.
That is conservative for stdlib development: Python wrappers win when both a
wrapper and native primitive share a name. Private native modules such as
`_time` are still found normally because no `_time.py` exists.

If performance or compatibility pressure later argues for extension modules
before source modules, change the order with tests and document the reason.

## Loading Sequence

Loading an extension spec follows the normal import invariant:

```text
create module object
set import metadata
insert module into sys.modules
open the dynamic library
look up the init symbol
create a module builder for this module instance
call init(builder)
on failure, remove sys.modules[name] only for this module
return the module currently in sys.modules[name]
```

The loader should keep native init multi-instance. Reimport after deleting a
`sys.modules` entry must create a fresh module object and call init again.

The dynamic library handle can be cached by path, but that cache must not imply
a singleton module object.

## Metadata

Native modules should expose metadata consistent with source modules:

```text
__name__      = fully qualified module name
__spec__      = ModuleSpecObject
__package__   = parent package name, or empty string for top-level modules
__loader__    = ModuleLoaderObject, equal to __spec__.loader
__file__      = dynamic library path
__builtins__  = VM builtins module
__doc__       = None initially, unless init sets it
```

Spec/loader fields:

```text
__spec__.name                         = module name
__spec__.origin                       = dynamic library path
__spec__.loader.kind                  = "extension"
__spec__.loader.name                  = module name
__spec__.loader.path                  = dynamic library path
__spec__.submodule_search_locations   = None
__spec__.has_location                 = True
__spec__.parent                       = parent package name or ""
```

## Error Behavior

Failure cases should raise `ImportError` or `ModuleNotFoundError` with useful
messages:

```text
no matching dynamic library             -> continue finder search / ModuleNotFoundError
dynamic library cannot be opened        -> ImportError
init symbol is missing                  -> ImportError
init function returns failure           -> propagate pending exception
init function returns failure without pending exception -> ImportError
```

If an extension init fails after importing other modules as side effects, only
the failing module's `sys.modules` entry is removed.

## Tests

Minimum build-system tests:

```text
CMake helper builds a native module into <build>/stdlib
module output name uses the clover platform suffix
test executable can resolve the C API symbols needed by the module
```

Minimum import tests:

```text
finder discovers _test_native as a native extension from CL_BUILD_STDLIB_DIR
import _test_native loads a dynamic module from CL_BUILD_STDLIB_DIR
native init can populate a module global through the builder API
metadata matches the extension spec
sys.modules contains the module before native init runs
native functions registered by the builder are callable from Python
native callback error values propagate as Python exceptions
failed init removes only the failing sys.modules entry
deleting sys.modules entry and reimporting creates a fresh module instance
one dynamic library can export and load two module init symbols
```
