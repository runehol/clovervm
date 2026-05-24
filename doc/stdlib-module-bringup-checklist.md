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
| [~] | `sys` | Runtime identity, import state, paths, module registry, process details. | Bootstrap module exists with `sys.modules` and `sys.path`; broad CPython surface is still missing or unverified. |
| [~] | `builtins` | Names every module assumes exist; exceptions, core types, builtin functions. | Trusted bootstrap exists; many builtin functions and exact CPython semantics remain incomplete. |
| [ ] | `types` | Shared type names used by frameworks, decorators, inspection, import machinery. | Not started / not assessed. |
| [ ] | `operator` | Functional forms of operators used by collections, sorting helpers, libraries. | Not started / not assessed. |
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
| [ ] | `os` | Environment, paths, file operations, process integration. | Not started / not assessed. |
| [ ] | `posix` / platform backend | Low-level OS primitives backing `os`. | Not started / not assessed. |
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
| [~] | `time` | Clocks and sleeps used by tests, retry loops, logging, benchmarks. | Native `_time` plus Python wrapper exists; broad API and platform edge cases remain incomplete. |
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

## Suggested First Slice

These modules should have the highest payoff for running more real Python
source:

| Status | Module | Not handled / notes |
| --- | --- | --- |
| [~] | `sys` | Fill out high-value attributes used by importlib, traceback, warnings, and tooling. |
| [~] | `builtins` | Continue adding common functions and exact exception/type behavior. |
| [ ] | `types` | Add runtime type names needed by importlib, inspect, and decorators. |
| [ ] | `operator` | Good candidate for a mostly pure-Python early module. |
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
