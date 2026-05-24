# Clover C API

This document describes the first native extension API for clovervm.

The immediate goal is not a stable third-party binary ABI. The goal is a narrow
C boundary for in-tree and VM-version-matched native modules, so extension
modules do not depend directly on C++ VM object layout, `Value`, `ThreadState`,
`ModuleObject`, ownership handles, allocator internals, or pending-exception
transport.

The API should still be shaped so it can grow into a stable external ABI later.

## Goals

- Load native extension modules from dynamic libraries through ordinary imports.
- Keep public library policy in Python modules, with private native modules
  providing narrow primitives.
- Initialize each imported native module instance independently.
- Avoid CPython's legacy single-phase/singleton-extension trap.
- Expose a C API rather than a C++ VM-internals ABI.
- Let native function registration and return values match the VM's natural
  "value or exception marker" convention without exposing `Value` layout.
- Keep the first API small enough to implement and audit.

## Non-Goals

- Promise cross-version binary compatibility for third-party extensions.
- Expose C++ VM internals through the extension boundary.
- Support non-ASCII native module names.
- Support arbitrary CPython C API compatibility.
- Provide a full packaging story for external native extensions.

## Import Shape

Native extension modules are ordinary import targets.

The intended standard-library pattern is:

```text
stdlib/time.py
  public Python module
  imports _time
  defines user-facing policy and compatibility

_time.clover.dylib / _time.clover.so / _time.clover.dll
  private native extension module
  defines OS/VM primitives
```

Example Python wrapper:

```python
from _time import monotonic_ns
from _time import sleep


def monotonic():
    return monotonic_ns() / 1_000_000_000
```

This keeps native dependencies explicit in Python source while keeping most
stdlib behavior visible to the compiler, optimizer, and JIT.

## Dynamic Library Lookup

The import system should search extension-module suffixes as part of its finder
chain. A first suffix set can be platform-specific and clover-specific:

```text
_time.clover.dylib
_time.clover.so
_time.clover.dll
```

Compatibility aliases such as plain `.so` or `.dll` can be added later if they
are useful. The clover-specific suffix is preferred because these extensions are
not CPython extensions.

The import sequence for a native module is:

```text
check sys.modules
check built-in/frozen modules, if any
search source/package/module paths
search extension-module suffixes
open the dynamic library
find the module-specific init symbol
create module object and import metadata
preinsert module in sys.modules
call native init with a module builder
remove only the failing module on init failure
return sys.modules[name]
```

The source-vs-extension search order should be explicit in the import-system
design and tests. The important invariant is that extension loading is a normal
finder/loader result, not a separate magic global-injection path.

## Init Symbols

Native extension modules use a CPython-inspired symbol convention, but without
Unicode/punycode support.

```text
init symbol = clover_module_init_ + module_name_with_dots_replaced_by_underscores
```

Examples:

```text
_time          -> clover_module_init__time
math._fast     -> clover_module_init_math__fast
pkg.native_os  -> clover_module_init_pkg_native_os
```

Native extension module names are ASCII-only for now. The mapping is not
reversible and may collide for names that differ only by dots versus
underscores. Such collisions are unsupported for native extensions.

A single dynamic library may export more than one init symbol. Importing two
module names from the same library still creates two separate module objects and
calls the name-specific init function for each module.

## Module Builder

The native init function receives an opaque module builder:

```c
typedef struct clover_native_module_builder clover_native_module_builder;

typedef enum clover_status
{
    CLOVER_STATUS_OK = 0,
    CLOVER_STATUS_ERROR = -1,
} clover_status;

CL_EXPORT clover_status clover_module_init__time(
    clover_native_module_builder *builder);
```

The builder is bound to one fresh module object. The VM creates the module,
sets import metadata, inserts it into `sys.modules`, and then calls the init
function. The init function populates that module through builder APIs.

The init contract is:

```text
return CLOVER_STATUS_OK on success
return CLOVER_STATUS_ERROR on failure, with an exception set through the builder/API
do not create or return a module object
do not cache Python-visible module-owned objects in process-global C state
store per-module state on or through the module instance
```

This makes native module initialization multi-instance from the beginning. If
user code deletes `sys.modules["_time"]` and imports `_time` again, clovervm
creates a fresh module object and calls the init function again for that
instance.

## String Encoding

All `const char *` strings accepted by the native extension API are UTF-8
encoded unless a function explicitly documents a narrower temporary
restriction. Invalid UTF-8 is an API error. Name arguments such as module
constant names and extension function names must be non-null and non-empty.

## Native Function Registration

Function registration should be arity-typed. The API uses one registration
function per supported arity, and each registration function accepts a suitably
typed function pointer.

The naming convention is:

```c
clover_module_add_function_0
clover_module_add_function_1
clover_module_add_function_N
```

and so on for additional fixed arities. Arity 0 and 1 are implemented first.

Sketch:

```c
typedef struct clover_call_context clover_call_context;
typedef uintptr_t clover_value;

typedef clover_value (*clover_extension_fn_0)(clover_call_context *ctx);
typedef clover_value (*clover_extension_fn_1)(clover_call_context *ctx,
                                              clover_value arg0);

clover_status clover_module_add_function_0(
    clover_native_module_builder *builder,
    const char *name,
    clover_extension_fn_0 fn);

clover_status clover_module_add_function_1(
    clover_native_module_builder *builder,
    const char *name,
    clover_extension_fn_1 fn);
```

This avoids passing an arity integer that can disagree with the function
pointer type. C and C++ compilers will reject wrong callback shapes.
`clover_value` is sized so callbacks can pass and return it by value, but it is
still opaque: extension modules must create and inspect values through API
helpers.

## Native Call Context

Native function callbacks receive a call context and typed opaque argument
handles. They return a `clover_value`: a normal value on success, or the
context's error marker after setting a pending exception.

Initial implemented surface:

```c
clover_value clover_error(clover_call_context *ctx);
clover_value clover_none(clover_call_context *ctx);
clover_value clover_int64(clover_call_context *ctx, int64_t value);
```

Planned conversion and allocation helpers:

```c
int clover_is_error(clover_value value);

clover_status clover_value_as_int64(clover_call_context *ctx,
                                    clover_value value,
                                    int64_t *out);

clover_status clover_value_as_double(clover_call_context *ctx,
                                     clover_value value,
                                     double *out);

clover_value clover_double(clover_call_context *ctx, double value);
clover_value clover_string_utf8(clover_call_context *ctx, const char *value);

clover_value clover_raise_type_error(clover_call_context *ctx,
                                     const char *message);
clover_value clover_raise_value_error(clover_call_context *ctx,
                                      const char *message);
clover_value clover_raise_import_error(clover_call_context *ctx,
                                       const char *message);
```

Conversion helpers set a pending exception on failure and return
`CLOVER_STATUS_ERROR`. Callers propagate that failure by returning
`clover_error(ctx)`.

Explicit semantic errors use a raise helper directly; raise helpers set the
pending exception and return the same error marker.

Example:

```c
static clover_value sleep_fn(clover_call_context *ctx, clover_value secs)
{
    double seconds;
    if(clover_value_as_double(ctx, secs, &seconds) != CLOVER_STATUS_OK)
    {
        return clover_error(ctx);
    }

    if(seconds < 0.0)
    {
        return clover_raise_value_error(
            ctx, "sleep length must be non-negative");
    }

    sleep_seconds(seconds);
    return clover_none(ctx);
}
```

The exact first set of conversions should be driven by the first native module.
For example, a `_time` module likely needs integer return values, float or int
argument parsing for `sleep`, `None` returns, and error raising.

## Constants

The builder should expose simple constant registration helpers:

```c
clover_status clover_module_add_int_constant(
    clover_native_module_builder *builder,
    const char *name,
    int64_t value);

clover_status clover_module_add_string_constant(
    clover_native_module_builder *builder,
    const char *name,
    const char *utf8_value);

clover_status clover_module_set_doc(
    clover_native_module_builder *builder,
    const char *doc);
```

`name`, `utf8_value`, and `doc` are UTF-8 strings.

Additional helpers can be added as native modules need them. The bar should be
real use, not anticipatory completeness.

## Error Handling

C API functions return `CLOVER_STATUS_OK` for success and
`CLOVER_STATUS_ERROR` for failure unless a more specific convention is
documented for that function.

Failure from a native callback or init function must correspond to pending
exception state in the VM. Native callbacks signal failure by returning a value
for which `clover_is_error(value)` is true. Native code should set pending
exception state through builder or call-context APIs, not by touching VM
internals.

The loader must treat init failure like source module execution failure:

```text
remove sys.modules[name] only for the module currently being initialized
preserve other modules loaded as side effects
return/propagate the pending exception
```

## Testing Expectations

The first implementation should include direct C/C++ tests for the C API
boundary as well as import-level tests.

Minimum low-level coverage:

```text
clover_is_error() is false for ordinary values
clover_is_error() is true for clover_error(ctx)
conversion helper failure sets a pending exception
raise helpers set a pending exception and return an error value
extension callback success returns its clover_value result
extension callback error propagates through the managed Function wrapper
```

Minimum import-level coverage:

```text
native module import populates module globals through the builder
native module metadata is coherent: __name__, __loader__, __spec__, __file__
sys.modules is populated before native init runs
failed native init removes only the failing module entry
deleting sys.modules[name] and reimporting creates a fresh module instance
multiple init symbols can be loaded from one dynamic library
```

## State

Native modules should be instance-oriented.

Do:

```text
store Python-visible state on the module instance
use explicit per-module native state once the builder exposes it
write native functions that receive all needed Python-visible state through args
```

Do not:

```text
store module-owned Clover objects in process-global C/C++ variables
assume init runs only once per process
assume sys.modules permanently owns the first module object
```

If native modules need private C state, add an explicit module-state facility to
the builder. That state should be owned by the module instance and finalized
with it, rather than hidden behind C globals.

## Build-System Shape

The build system should provide a helper for in-tree native modules:

```cmake
clovervm_add_native_module(_time
    SOURCES
        native_modules/time_module.c)
```

The helper should:

```text
build a dynamic library/module target
name the output with a clover extension suffix
compile against the public Clover C API headers
avoid linking in a second copy of the VM
place the output somewhere import search can find it
apply the same warning and sanitizer policy as the rest of the project
```

Executable targets that load native modules must export the symbols needed by
the native module or link through a shared VM support library. The first
implementation should make this explicit in CMake rather than relying on
platform accidents.

## Relationship To Existing C++ Native Functions

The current VM has an internal C++ native-function mechanism used for builtins
and implementation code. It can use overloaded C++ helpers and direct `Value`
return conventions because it is compiled as part of the VM.

The C API described here is a different boundary:

```text
intrinsic functions:
  C++ implementation convenience
  direct Value and ThreadState access
  no dynamic library boundary

extension functions:
  opaque handles
  module builder
  arity-typed C function registration
  extension callbacks return clover_value or clover_error(ctx)
  dynamic library boundary
```

The extension implementation may internally wrap C callbacks in the existing
managed `Function`/native-thunk representation, but that should remain an
implementation detail.
