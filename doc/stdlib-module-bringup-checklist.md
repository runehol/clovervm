# Standard Library Bringup Checklist

This tracks Python standard library modules that unlock a large amount of
ordinary Python code. The list is intentionally biased toward foundational
runtime behavior, imports, common data structures, text handling, files, and
developer tooling.

## Status Legend

- `[ ]` Not started or not assessed.
- `[~]` Partial support. The notes should say what is missing.
- `[x]` Complete enough for normal use in CloverVM.

Use the status marker and the notes column together. A module can be partially
checked off when the common path works but important Python-visible semantics,
edge cases, platform behavior, or APIs are still missing.

## Tier 0: VM and Runtime Critical

| Status | Module | Why it matters | Not handled / notes |
| --- | --- | --- | --- |
| [~] | `sys` | Runtime identity, import state, paths, module registry, process details. | Built into VM bootstrap. Covers `sys.modules`, `sys.path`, import metadata, core static metadata (`argv`, `builtin_module_names`, `byteorder`, `maxsize`, `maxunicode`, `platform`, `version`, `version_info`), `implementation`, encoding queries, recursion/switch interval metadata, `getsizeof` default handling, `gettrace`, `getprofile`, and `is_finalizing`. Still missing broad CPython surface, standard streams/hooks, active exception APIs, tracing/profiling behavior, `SystemExit`/`sys.exit`, real installation prefixes, and named tuple compatibility for several metadata values. |
| [~] | `builtins` | Names every module assumes exist; exceptions, core types, builtin functions. | Trusted bootstrap exists and now covers core object-introspection helpers: `isinstance`, `issubclass`, `getattr`, `hasattr`, `setattr`, `delattr`, `vars`, `callable`, and a limited `dir`. `print` now accepts keyword-only `sep`, `end`, `file`, and `flush` parameters for stdout output, with `file` limited to `None` until stream support exists. `type` remains the VM's builtin class object rather than a fully compatible callable `type(obj)`/`type(name, bases, dict)`. `dir` enumerates stored own namespaces plus class MRO entries; full CPython behavior around custom `__dir__`, metaclass details, descriptors, signatures, streams, and many other builtin functions remains incomplete. |
| [~] | `types` | Shared type names used by frameworks, decorators, inspection, import machinery. | Pure-Python `types.py` now exposes reachable runtime aliases (`FunctionType`, `LambdaType`, builtin function/method aliases, `MethodType`, `ModuleType`, `CodeType`, `NoneType`), a minimal `SimpleNamespace`, and `get_original_bases`. `Function.__code__` is exposed so `CodeType` comes from a live code object rather than a private builtin hook. Missing descriptor/frame/traceback/generator/coroutine/union/generic-alias types, `new_class`, `prepare_class`, `resolve_bases`, `coroutine`, exact `SimpleNamespace` constructor/representation/equality, and other APIs blocked by callable `type`, richer code object metadata, keyword args, descriptors, and broader runtime object exposure. |
| [~] | `operator` | Functional forms of operators used by collections, sorting helpers, libraries. | Pure-Python first slice covers supported arithmetic/bitwise/comparison/identity/truth helpers, matrix multiplication, item get/set/delete, sequence concat via direct `__add__`, membership-backed `contains`, `countOf`, `indexOf`, `length_hint`, `call` for up to three positional args, CPython 3.14 `__all__` names, and aliases such as `__add__`. Unsupported helpers are explicit stubs where the VM lacks the needed operation/protocol: callable factory helpers (`attrgetter`, `itemgetter`, `methodcaller`) because callable instances/closures are not ready, exact `call(*args, **kwargs)`, richer `length_hint`, and full `__index__` protocol behavior. |
| [ ] | `collections` | `deque`, `defaultdict`, `Counter`, named tuples, ordered helpers. | Not started / not assessed. |
| [ ] | `collections.abc` | Abstract container protocols used by typing, pathlib, importlib, libraries. | Not started / not assessed. |
| [ ] | `abc` | Abstract base classes and metaclass behavior. | Depends on metaclass and descriptor semantics. |
| [ ] | `functools` | Decorators, wrappers, caching, partial application. | Not started / not assessed. |
| [ ] | `itertools` | Common efficient iteration building blocks. | Depends on iterator protocol completeness and native helpers for performance. |
| [ ] | `contextlib` | Context-manager helpers used pervasively in tests and libraries. | Depends on `with` semantics if not already complete. |

## Tier 1: Import and Package Ecosystem

| Status | Module | Why it matters | Not handled / notes |
| --- | --- | --- | --- |
| [ ] | `importlib` | Public import system APIs; many packages touch it directly. | Internal import foundation exists, but public `importlib` surface is not started / not assessed. |
| [ ] | `importlib.util` | Spec helpers, module creation, lazy imports, package checks. | Not started / not assessed. |
| [ ] | `importlib.machinery` | Finder/loader/spec classes and import constants. | Not started / not assessed. |
| [ ] | `pkgutil` | Package walking and loader compatibility helpers. | Not started / not assessed. |
| [ ] | `site` | Startup path customization and site-packages conventions. | Not started / not assessed. |
| [~] | `os` | Environment, paths, file operations, process integration. | POSIX-only `_os` native backend plus public `os.py` now covers core constants, `getcwd`, `chdir`, `listdir` (tuple, not list), process/user/group IDs, environment get/set/unset, `strerror`, `system`, `umask`, `stat`/`lstat` as plain tuples, `access`, `chmod`, `mkdir`/`makedirs`, `rmdir`, `unlink`/`remove`, `rename`/`replace`, and a small `os.path`/`posixpath` subset (`join`, `split`, `dirname`, `basename`, `exists`, `isdir`, `isfile`, `isabs`, `abspath`). Some implementation shape is cleanup debt caused by current string limitations: Python code has explicit `__add__` calls where normal string `+` is not available, cannot iterate or slice strings, and therefore pushes simple path string operations such as split into unnecessary native C. Missing broad CPython surface: bytes/path-like/fd support, `environ` mapping, `OSError` subclasses/errno attributes, named stat result objects, file descriptor I/O, process spawning/exec/wait, symlinks, scandir/walk, full `posixpath`, extended platform constants, and keyword-only dir-fd/follow-symlink behavior. |
| [~] | `posix` / platform backend | Low-level OS primitives backing `os`. | Private `_os` native module backs the first POSIX syscall slice for `os`; no public `posix` module yet. |
| [ ] | `errno` | Stable OS error constants. | Not started / not assessed. |
| [ ] | `stat` | File mode constants and helpers. | Not started / not assessed. |
| [ ] | `pathlib` | Modern path abstraction used by tooling and tests. | Not started / not assessed. |

## Tier 2: Data Formats and Text Handling

| Status | Module | Why it matters | Not handled / notes |
| --- | --- | --- | --- |
| [ ] | `re` | Regular expressions; required by many parsers and validators. | Not started / not assessed. |
| [ ] | `string` | Constants and formatter helpers. | Not started / not assessed. |
| [ ] | `enum` | Enum classes used by stdlib and application APIs. | Depends on class/metaclass behavior. |
| [ ] | `dataclasses` | Common declarative class helper. | Depends on annotations, descriptors, introspection, and class mutation semantics. |
| [ ] | `typing` | Type hint objects imported by modern Python code. | Runtime behavior can start small, but broad compatibility is large. |
| [ ] | `json` | Ubiquitous data interchange. | Not started / not assessed. |
| [ ] | `copy` | Shallow/deep copy protocol support. | Depends on object protocol and dispatch tables. |
| [ ] | `pickle` | Serialization protocol used by tests and tools. | Large surface; likely later than `json`. |
| [ ] | `struct` | Binary packing/unpacking. | Not started / not assessed. |
| [ ] | `codecs` | Encoding registry and stream helpers. | Not started / not assessed. |
| [ ] | `encodings` | Required codec packages for source and text I/O compatibility. | Not started / not assessed. |

## Tier 3: Files, Paths, and Processes

| Status | Module | Why it matters | Not handled / notes |
| --- | --- | --- | --- |
| [ ] | `io` | Text/binary streams, file objects, buffering. | Not started / not assessed. |
| [ ] | `tempfile` | Temporary files/directories used by tests and tools. | Depends on `os`, randomness, and file I/O. |
| [ ] | `shutil` | File copying, recursive removal, terminal helpers. | Depends on `os`, `stat`, and file I/O. |
| [ ] | `glob` | Filesystem pattern expansion. | Depends on `os`, `fnmatch`, and path handling. |
| [ ] | `fnmatch` | Shell-style filename matching. | Not started / not assessed. |
| [ ] | `subprocess` | Running external commands. | Platform-sensitive; requires process and file descriptor support. |
| [ ] | `argparse` | Command-line parsing for tools. | Not started / not assessed. |
| [ ] | `getopt` | Smaller command-line parsing compatibility module. | Not started / not assessed. |

## Tier 4: Time, Math, and Randomness

| Status | Module | Why it matters | Not handled / notes |
| --- | --- | --- | --- |
| [~] | `time` | Clocks and sleeps used by tests, retry loops, logging, benchmarks. | Expanded native `_time` and public `time.py` cover clocks, sleep, `gmtime`, `localtime`, `asctime`, `ctime`, `strftime`, `mktime`, CPU clocks, timezone constants, and clock constants. `ninja -C build-debug all check` passed with 813 tests. Full nanosecond clock coverage depends on arbitrary-size integer support. |
| [ ] | `datetime` | Dates, times, timedeltas, parsing/formatting basics. | Not started / not assessed. |
| [ ] | `calendar` | Date helpers used by `datetime`-adjacent code. | Not started / not assessed. |
| [~] | `math` | Numeric functions and constants. | Native `_math` plus Python wrapper exists with tests; confirm remaining CPython APIs, signatures, errors, and edge cases before marking complete. |
| [ ] | `random` | Tests, IDs, sampling, backoff, simple simulations. | Not started / not assessed. |
| [ ] | `statistics` | Basic statistical helpers. | Not started / not assessed. |
| [ ] | `decimal` | Decimal arithmetic for data and finance code. | Large numeric surface; not started / not assessed. |
| [ ] | `fractions` | Rational arithmetic. | Not started / not assessed. |

## Tier 5: Testing, Debugging, and Developer Tooling

| Status | Module | Why it matters | Not handled / notes |
| --- | --- | --- | --- |
| [ ] | `unittest` | Enables many upstream-style tests. | Not started / not assessed. |
| [ ] | `traceback` | Human-readable exception reports. | Depends on frame/code metadata and exception chaining fidelity. |
| [ ] | `warnings` | Deprecation and runtime warning machinery. | Not started / not assessed. |
| [ ] | `inspect` | Function/class/module introspection used by tooling and decorators. | Depends on rich code objects, signatures, frames, and attributes. |
| [ ] | `pprint` | Debug printing for containers and data structures. | Not started / not assessed. |
| [ ] | `logging` | Common application diagnostics. | Depends on time, traceback, threading/process features eventually. |
| [ ] | `doctest` | Documentation examples and some package tests. | Depends on parsing, traceback, and output capture. |

## Tier 6: Compression, Archives, and Common Protocols

| Status | Module | Why it matters | Not handled / notes |
| --- | --- | --- | --- |
| [ ] | `zlib` | Compression primitive for archives and protocols. | Native extension likely needed. |
| [ ] | `gzip` | Common compressed file format. | Depends on `zlib` and `io`. |
| [ ] | `zipfile` | Wheels, eggs, package data, archives. | Depends on `zlib`, `io`, `struct`, `os`, and path handling. |
| [ ] | `tarfile` | Source archives and package tooling. | Depends on file I/O, paths, compression modules. |
| [ ] | `base64` | Encoding helpers for protocols and data files. | Not started / not assessed. |
| [ ] | `hashlib` | Hashes for packaging, caches, protocols. | Native crypto/hash backend likely needed. |
| [ ] | `hmac` | Protocol authentication helper. | Depends on `hashlib`. |
| [ ] | `urllib.parse` | URL parsing used without requiring networking. | Not started / not assessed. |

## Cross-Cutting Stdlib Notes

- Native extension support now includes helpers for reading tuples and UTF-8
  strings from extension modules, with export-list wiring. This should make
  future native-backed modules less ad hoc, especially modules that need to
  accept structured Python values or text from public wrappers.

## Suggested First Slice

These modules should have the highest payoff for running more real Python
source:

| Status | Module | Not handled / notes |
| --- | --- | --- |
| [~] | `sys` | Fill out high-value attributes used by importlib, traceback, warnings, and tooling. |
| [~] | `builtins` | Continue adding common functions and exact exception/type behavior. |
| [~] | `types` | Pure-Python first slice exists with `CodeType`; many descriptor/frame/generator aliases and dynamic class helpers remain. |
| [~] | `operator` | Pure-Python first slice exists for direct VM-supported operators; callable factory helpers and protocol-heavy edge behavior remain. |
| [ ] | `collections` | Start with pure-Python-compatible pieces, but core containers may need native support later. |
| [ ] | `abc` | Needed before `collections.abc` and many framework patterns. |
| [ ] | `functools` | `wraps`, `partial`, and cache helpers have high ecosystem payoff. |
| [ ] | `itertools` | Start with common primitives once iterator protocol is settled. |
| [ ] | `contextlib` | Useful after context-manager semantics are solid. |
| [ ] | `os` | Start with path/environment basics before process-heavy APIs. |
| [ ] | `io` | Foundational for files, codecs, archives, and testing tools. |
| [ ] | `errno` | Small constants module with broad utility. |
| [ ] | `stat` | Small constants/helpers module with broad utility. |
| [ ] | `pathlib` | Depends on enough `os`/`stat` support to be meaningful. |
| [ ] | `re` | Large but unlocks a lot of parsers and stdlib modules. |
| [ ] | `enum` | Useful for modern Python APIs; depends on class semantics. |
| [ ] | `dataclasses` | High payoff after annotations/class mutation behavior is ready. |
| [ ] | `typing` | Can begin as import-compatible runtime objects before full behavior. |
| [ ] | `json` | High-payoff standalone data format. |
| [ ] | `traceback` | Needed for usable failures and test output. |
| [ ] | `warnings` | Common import-time side effects in libraries. |
