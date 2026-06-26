# Clover C API

This document describes clovervm's native extension API.

The API is currently intended for in-tree and VM-version-matched native modules,
not for stable third-party binary compatibility. The boundary is still a C
boundary: extension modules should not depend directly on C++ VM object layout,
`Value`, `ThreadState`, `ModuleObject`, ownership handles, allocator internals,
or pending-exception transport.

The API should be shaped so it can grow into a stable external ABI later.

## Scope

Goals:

- Load native extension modules from dynamic libraries through ordinary imports.
- Keep public library policy in Python modules, with private native modules
  providing narrow primitives.
- Initialize each imported native module instance independently.
- Expose a C API rather than a C++ VM-internals ABI.
- Let native function registration and return values match the VM's natural
  "value or exception marker" convention without exposing `Value` layout.

Non-goals:

- Promise cross-version binary compatibility for third-party extensions.
- Expose C++ VM internals through the extension boundary.
- Support non-ASCII native module names.
- Support arbitrary CPython C API compatibility.
- Provide a full packaging story for external native extensions.

## Conventions

All `const char *` strings accepted by the native extension API are UTF-8
encoded unless a function explicitly documents a narrower temporary
restriction. Invalid UTF-8 is an API error. Name arguments such as module
constant names and extension function names must be non-null and non-empty.

Most C API functions return `CLOVER_STATUS_OK` for success and
`CLOVER_STATUS_ERROR` for failure:

```c
typedef enum clover_status
{
    CLOVER_STATUS_OK = 0,
    CLOVER_STATUS_ERROR = -1,
} clover_status;
```

`clover_handle` is an opaque value handle. Extension modules must not inspect or
construct it directly; use `clover_*` helpers instead.

## Module Shape

Native extension modules are ordinary import targets. The intended standard
library pattern is:

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
from _time import monotonic
from _time import sleep
```

This keeps native dependencies explicit in Python source while keeping most
stdlib behavior visible to the compiler, optimizer, and JIT.

Extension modules use clover-specific dynamic library suffixes:

```text
_time.clover.dylib
_time.clover.so
_time.clover.dll
```

The clover-specific suffix is preferred because these extensions are not
CPython extensions.

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

## Module Initialization

The native init function receives an opaque module builder:

```c
typedef struct clover_native_module_builder clover_native_module_builder;
typedef struct clover_context clover_context;

CL_EXPORT clover_status clover_module_init__time(
    clover_context *ctx,
    clover_native_module_builder *builder);
```

The builder is bound to one fresh module object. The VM creates the module and
calls the init function. The init function populates that module through builder
APIs.

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

## Module Values

The builder can add values to the module:

```c
clover_status clover_module_add_value(
    clover_native_module_builder *builder,
    const char *name,
    clover_handle value);
```

`name` is a UTF-8 string. Values are manufactured through the runtime API:

```c
clover_module_add_value(builder, "answer", clover_int_from_int64(ctx, 42));
clover_module_add_value(builder, "greeting",
                        clover_string_from_utf8(ctx, "hello"));
clover_module_add_value(builder, "nothing", clover_none(ctx));
clover_module_add_value(builder, "pair",
                        clover_tuple_from_pair(
                            ctx, clover_int_from_int64(ctx, 1),
                            clover_int_from_int64(ctx, 2)));
```

## Function Registration

Function registration is arity-typed. The API uses one registration function per
supported arity, and each registration function accepts a suitably typed
function pointer.

Fixed arities from 0 through 7 are currently supported:

```c
typedef struct clover_context clover_context;
typedef uintptr_t clover_handle;

typedef clover_handle (*clover_extension_fn_0)(clover_context *ctx);
typedef clover_handle (*clover_extension_fn_1)(clover_context *ctx,
                                              clover_handle arg0);
typedef clover_handle (*clover_extension_fn_2)(clover_context *ctx,
                                              clover_handle arg0,
                                              clover_handle arg1);
/* ... through clover_extension_fn_7 */

clover_status clover_module_add_function_0(
    clover_native_module_builder *builder,
    const char *name,
    clover_extension_fn_0 fn,
    const char *docstring);

clover_status clover_module_add_function_1(
    clover_native_module_builder *builder,
    const char *name,
    clover_extension_fn_1 fn,
    const char *docstring);

/* ... through clover_module_add_function_7 */
```

This avoids passing an arity integer that can disagree with the function pointer
type. C and C++ compilers will reject wrong callback shapes.

Function docstrings are UTF-8 strings; pass `NULL` for no docstring.

## Runtime API

Native function callbacks receive a context and opaque argument handles.
They return a `clover_handle`: a normal value on success, or the context's error
marker after setting a pending exception.

Implemented runtime APIs:

```c
clover_handle clover_propagate_error(clover_context *ctx);
clover_handle clover_none(clover_context *ctx);
clover_handle clover_int_from_int64(clover_context *ctx, int64_t value);
clover_handle clover_float_from_double(clover_context *ctx, double value);
clover_handle clover_string_from_utf8(clover_context *ctx,
                                     const char *utf8_value);
clover_handle clover_tuple_from_array(clover_context *ctx,
                                     const clover_handle *items,
                                     size_t count);
clover_handle clover_tuple_from_pair(clover_context *ctx,
                                    clover_handle item0,
                                    clover_handle item1);
clover_status clover_float_as_double(clover_context *ctx,
                                     clover_handle value,
                                     double *out);
clover_status clover_int_as_int64(clover_context *ctx,
                                  clover_handle value,
                                  int64_t *out);
clover_status clover_is(clover_context *ctx,
                        clover_handle left,
                        clover_handle right,
                        bool *out);
clover_handle clover_raise_overflow_error(clover_context *ctx,
                                         const char *utf8_message);
clover_handle clover_raise_value_error(clover_context *ctx,
                                      const char *utf8_message);
```

Conversion helpers set a pending exception on failure and return
`CLOVER_STATUS_ERROR`. Callers propagate that failure by returning
`clover_propagate_error(ctx)`.

`clover_is` compares the underlying managed values for Python `is`
identity. It must not compare handle storage addresses once handles become
indirect.

Explicit semantic errors use a raise helper directly. Raise helpers set the
pending exception and return the same error marker.

Example:

```c
static clover_handle sleep_fn(clover_context *ctx, clover_handle secs)
{
    double seconds;
    if(clover_float_as_double(ctx, secs, &seconds) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
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

## Error Handling

Failure from a native callback or init function must correspond to pending
exception state in the VM. Native code should set pending exception state
through builder or runtime APIs, not by touching VM internals.

For callbacks:

```text
return a normal clover_handle on success
return clover_propagate_error(ctx) after a helper has set a pending exception
return a raise-helper result for explicit semantic errors
```

For init functions:

```text
return CLOVER_STATUS_OK on success
return CLOVER_STATUS_ERROR after a builder/API helper has set a pending exception
```

## State And Lifetime

Native modules should be instance-oriented.

Do:

```text
store Python-visible state on the module instance
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

## In-Tree Build Integration

The build system provides a helper for in-tree native modules:

```cmake
clovervm_add_native_module(_time
    SOURCES
        native_modules/time_module.c)
```

The helper:

```text
builds a dynamic library/module target
names the output with a clover extension suffix
compiles against the public Clover C API headers
avoids linking in a second copy of the VM
places the output somewhere import search can find it
applies the same warning and sanitizer policy as the rest of the project
```

Executable targets that load native modules must export the extension API
symbols needed by native modules or link through a shared VM support library.

## Intrinsics Versus Extensions

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
  extension callbacks return clover_handle or clover_propagate_error(ctx)
  dynamic library boundary
```

The extension implementation may internally wrap C callbacks in the existing
managed `Function`/native-thunk representation, but that should remain an
implementation detail.
