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
| [~] | `builtins` | Names every module assumes exist; exceptions, core types, builtin functions. | Ordinary exception registrations now include the arithmetic and lookup families, `SyntaxError` → `IndentationError` → `TabError`, `BufferError`, `EOFError`, `MemoryError`, `ReferenceError`, `RecursionError`, `PythonFinalizationError` (under `RuntimeError`), and `Warning` with its eleven standard subclasses, checked against CPython 3.14.5. This supplies classes and inheritance, without new runtime detection or emission; constructor/formatting and remaining family gaps are recorded in [Python semantic deviations](python-deviations.md#exceptions). Trusted bootstrap defines `abs` through cached `__abs__` dispatch, with trusted int/bigint/float handlers and inherited bool behavior. It also covers core object-introspection helpers: `isinstance`, `issubclass`, `getattr`, `hasattr`, `setattr`, `delattr`, `vars`, `callable`, and a limited `dir`. `print` now accepts keyword-only `sep`, `end`, `file`, and `flush` parameters for stdout output, with `file` limited to `None` until stream support exists. `type` remains the VM's builtin class object rather than a fully compatible callable `type(obj)`/`type(name, bases, dict)`. `dir` enumerates stored own namespaces plus class MRO entries; full CPython behavior around custom `__dir__`, metaclass details, descriptors, signatures, streams, and many other builtin functions remains incomplete. |
| [~] | `types` | Shared type names used by frameworks, decorators, inspection, import machinery. | Pure-Python `types.py` exposes reachable runtime aliases (`FunctionType`, `LambdaType`, builtin function/method aliases, `MethodType`, `ModuleType`, `CodeType`, `NoneType`), a minimal `SimpleNamespace`, and `get_original_bases`. `Function.__code__` supplies the live `CodeType`. `SimpleNamespace` repr is checked against CPython 3.14.5: insertion order, subclass names, empty-name omission, string-subclass keys without invoking their hooks, and direct/mutual/container-mediated namespace recursion with exception cleanup. Internal `slotdict.keys()` returns an ordered list snapshot. Repr guarding assumes the current single-threaded runtime, and `__dict__` remains a slotdict rather than a full dict. Constructor and equality improvements are deferred. Missing descriptor/frame/traceback/generator/coroutine/union/generic-alias types, `new_class`, `prepare_class`, `resolve_bases`, `coroutine`, and other APIs remain blocked by callable `type`, richer metadata, descriptors, and broader runtime object exposure. |
| [~] | `operator` | Functional forms of operators used by collections, sorting helpers, libraries. | Pure-Python first slice covers supported arithmetic/bitwise/comparison/identity/truth helpers; `abs` delegates to builtin special-method dispatch, matrix multiplication, item get/set/delete, sequence concat, membership-backed `contains`, identity-before-equality `countOf`/`indexOf`, `length_hint`, CPython 3.14 supported-name `__all__`, and aliases such as `__add__`. Unsupported callable factory helpers (`attrgetter`, `itemgetter`, `methodcaller`) remain explicit stubs pending callable instances/closures. `call` forwards positional and keyword arguments through the normal call machinery, with a positional-only target and the `__call__` alias; first-class bound methods retain the existing VM limitation. `index` normalizes accepted builtin ints/bools to plain int. `length_hint` validates builtin int/bool/subclass defaults against signed 64-bit platform bounds before callbacks, prefers validated length results, and dispatches `__length_hint__` through the special-method caller with fallback and result validation. Heap integers above the SMI limit remain accepted within platform bounds. General `__index__` support and platform-bound selection for future non-64-bit targets remain deferred. |
| [~] | `collections` | `deque`, `defaultdict`, `Counter`, named tuples, ordered helpers. | Public Python `UserDict` slice, audited against CPython 3.14.5: construction, public `data`, item operations, membership/iteration/length, representation/equality, `get`, live `keys`/`values`/`items`, `update`, `setdefault`, `pop`, first-key `popitem`, `clear`, and exact-base shallow `copy`. Preserves supported subclass hooks and class-level/inherited `__missing__`. See UserDict boundaries below for deferred APIs, VM dependencies, and test limits. Other collections classes remain absent. |
| [ ] | `collections.abc` | Abstract container protocols used by typing, pathlib, importlib, libraries. | Not started / not assessed. |
| [ ] | `abc` | Abstract base classes and metaclass behavior. | Depends on metaclass and descriptor semantics. |
| [ ] | `functools` | Decorators, wrappers, caching, partial application. | Not started / not assessed. |
| [~] | `itertools` | Common efficient iteration building blocks. | Pure-Python lazy iterator classes implement `chain`, class-level `chain.from_iterable`, finite/infinite `repeat`, `islice`, and `starmap` through existing VM iterator/call protocols. Tests cover construction laziness, exhaustion, consumption boundaries, exception propagation/recovery, unpacking, and call conventions against CPython 3.14.5. Integer arguments use `operator.index` for supported builtin ints/bools/int subclasses with signed 64-bit platform bounds; custom `__index__` remains deferred. `from_iterable` lacks instance/classmethod binding and subclass-preserving construction. Callable instances and first-class bound methods remain outside `starmap`'s supported call surface. Inputs are limited to existing VM iterables: string iteration is unsupported, so CPython string-input cases are omitted. The self-checking tests use indexed loop cases (tuple targets are unsupported) and separate functions to stay within the per-code-object global-read-cache limit. Other itertools APIs, repr/pickling/copy/type metadata compatibility, and `repeat.__length_hint__` remain unimplemented; no native performance work is included. |
| [~] | `contextlib` | Context-manager helpers used pervasively in tests and libraries. | Pure-Python module now covers `AbstractContextManager`, `closing`, `nullcontext`, `suppress`, and `chdir` on top of current `with` semantics. Missing generator/decorator-heavy APIs (`contextmanager`, `asynccontextmanager`, `ContextDecorator`, `ExitStack`, async helpers) pending generators, async, file/stream support, and richer decorator/protocol behavior; redirection helpers still need stream support. |

## Tier 1: Import and Package Ecosystem

| Status | Module | Why it matters | Not handled / notes |
| --- | --- | --- | --- |
| [ ] | `importlib` | Public import system APIs; many packages touch it directly. | Internal import foundation exists, but public `importlib` surface is not started / not assessed. |
| [ ] | `importlib.util` | Spec helpers, module creation, lazy imports, package checks. | Not started / not assessed. |
| [ ] | `importlib.machinery` | Finder/loader/spec classes and import constants. | Not started / not assessed. |
| [ ] | `pkgutil` | Package walking and loader compatibility helpers. | Not started / not assessed. |
| [ ] | `site` | Startup path customization and site-packages conventions. | Not started / not assessed. |
| [~] | `os` | Environment, paths, file operations, process integration. | POSIX-only `_os` native backend plus public `os.py` now covers core constants, `getcwd`, `chdir`, `listdir` (tuple, not list), process/user/group IDs, environment get/set/unset, `strerror`, `system`, `umask`, `stat`/`lstat` as plain tuples, `access`, `chmod`, `mkdir`/`makedirs`, `rmdir`, `unlink`/`remove`, `rename`/`replace`, and a pure-Python `os.path`/`posixpath` subset (`join`, `split`, `splitdrive`, `splitext`, `dirname`, `basename`, `exists`, `lexists`, `isdir`, `isfile`, `islink`, `ismount`, `samefile`, `samestat`, `isabs`, `abspath`, `normcase`, `normpath`, `commonprefix`, `commonpath`, `relpath`, `expanduser`, `expandvars`). `commonpath` compares lexical components, preserving `..`; `relpath` retains normalization through `abspath`. `expanduser` handles root, empty, and slash-only home directories without duplicate separators. Missing broad CPython surface: bytes/path-like/fd support, `environ` mapping, `OSError` subclasses/errno attributes, named stat result objects, file descriptor I/O, process spawning/exec/wait, symlink creation/readlink support, scandir/walk, remaining `posixpath` helpers (`sameopenfile`, `realpath`), extended platform constants, and keyword-only dir-fd/follow-symlink behavior. |
| [~] | `posix` / platform backend | Low-level OS primitives backing `os`. | Private `_os` native module backs the first POSIX syscall slice for `os`; no public `posix` module yet. |
| [x] | `errno` | Stable OS error constants. | Complete for supported host platforms. |
| [~] | `stat` | File mode constants and helpers. | Pure-Python module now covers POSIX `os.stat` tuple indexes, mode constants, `S_IMODE`, `S_IFMT`, `S_IS*` predicates, and `filemode`; `os.path.isdir`/`isfile` now use these helpers. Missing platform-specific file flag constants, named stat result integration, and broader `os`/filesystem semantics. |
| [ ] | `pathlib` | Modern path abstraction used by tooling and tests. | Deferred: the bounded POSIX `Path` slice needs read-only properties (`name`, `parent`, `suffix`, `suffixes`, `stem`), but builtin `property` and interpreter execution of descriptor `__get__` are not supported yet. Revisit after descriptor/property support; do not substitute writable attributes or an eager parent chain. Existing filesystem limits also include string-only inputs, absent `os.fspath`, plain stat tuples, and `ValueError` for OS failures. |

## Tier 2: Data Formats and Text Handling

| Status | Module | Why it matters | Not handled / notes |
| --- | --- | --- | --- |
| [ ] | `re` | Regular expressions; required by many parsers and validators. | Not started / not assessed. |
| [~] | `string` | Constants and formatter helpers. | Pure-Python module now exposes common constants (`ascii_*`, digits, hexdigits, octdigits, punctuation, whitespace, printable) and `capwords`. Missing `Template` and `Formatter`, which need richer formatting, regex, mapping, and protocol support. |
| [ ] | `enum` | Enum classes used by stdlib and application APIs. | Depends on class/metaclass behavior. |
| [ ] | `dataclasses` | Common declarative class helper. | Depends on annotations, descriptors, introspection, and class mutation semantics. |
| [ ] | `typing` | Type hint objects imported by modern Python code. | Runtime behavior can start small, but broad compatibility is large. |
| [ ] | `json` | Ubiquitous data interchange. | Not started / not assessed. |
| [~] | `copy` | Shallow/deep copy protocol support. | Bounded Python slice, referenced to CPython 3.14.5: exact None/bool/int/float/str/bytes/tuple/slice/Ellipsis/NotImplemented identity, exact list/dict shallow copies, class identity, class-level inherited `__copy__`, and positional-only `replace(obj, /, **changes)` via `__replace__`. A narrow native `__clover_runtime_type__` helper exposes actual runtime class identity without attribute lookup; callable `type(obj)` remains incomplete. Unsupported copy targets raise `copy.Error`; reduction reconstruction and deepcopy are omitted. Set/bytearray/frozenset and other atomic types remain omitted. Namedtuple/dataclass replacement depends on absent `collections.namedtuple`/`dataclasses` and their generated `__replace__` hooks. General callable-instance hooks and richer descriptor/metaclass behavior remain runtime dependencies. CPython probes confirm staticmethod/classmethod hooks receive the explicit object, instance hooks are ignored, and `replace` observes `obj.__class__` whereas `copy` uses actual type identity; descriptor/property cases are omitted from VM tests until supported. Tests inspect dict size/entries/identity because dict value equality is not implemented. |
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
| [~] | `glob` | Filesystem pattern expansion. | Pure-Python module now covers common POSIX string-path `glob`, list-backed `iglob`, string `root_dir`, `escape`, `has_magic`, deprecated-compatible `glob0`/`glob1` without warnings, hidden-file filtering, and a small recursive `**` directory expansion including zero-directory prefix matches. Missing bytes/path-like support, `dir_fd`, exact generator behavior, symlink-specific recursion behavior, sorted/stable ordering guarantees, and full CPython `translate` semantics. |
| [~] | `fnmatch` | Shell-style filename matching. | Pure-Python module now covers `fnmatch`, `fnmatchcase`, `filter`, Python 3.14 `filterfalse`, string and bytes matching, mixed string/bytes rejection, and representative `translate` output for literal, `*`, `?`, bracket, negated bracket, and simple range patterns. Leading `]` literals and range endpoints are covered in string/bytes matching and regex translation, including negated and descending ranges. Regex translation removes descending ranges without discarding other class members and escapes literal hyphens separately from range separators. Matching uses a direct shell-pattern engine because `re` is not available. Missing CPython's regex translation details for complex bracket escaping/set operations and LRU caching. |
| [ ] | `subprocess` | Running external commands. | Platform-sensitive; requires process and file descriptor support. |
| [ ] | `argparse` | Command-line parsing for tools. | Not started / not assessed. |
| [ ] | `getopt` | Smaller command-line parsing compatibility module. | Not started / not assessed. |
| [~] | `shlex` | Shell-style string splitting and quoting. | Pure-Python slice referenced to CPython 3.14.5: `split(s, comments=False, posix=True)` supports both modes, comments, quote/escape rules, empty tokens, and malformed-input errors; scanning and comment skipping use base `str` length/index operations so string-subclass overrides cannot alter parsed arguments; `quote(s)` preserves the ASCII safe set, apostrophe escaping, and CPython's early falsy-input handling; its safety scan uses base `str` length/index operations so subclass overrides cannot conceal metacharacters or change the scan bounds; `join(split_command)` quotes VM-supported iterables. `split(None)` raises `ValueError`; other non-string split inputs deliberately raise `TypeError` instead of attempting CPython's stream reads. No public configurable `shlex` class, streams, source inclusion, or punctuation configuration. String iteration and unsupported iterator protocols remain runtime blockers for corresponding `join` inputs; list, tuple, and their iterators are supported. Uses the existing private runtime-type helper for quote error messages; no new native code. A supplemental 1,200-case generated oracle exceeded the VM's constant-table capacity as one program; oracle cases are run in smaller batches without compiler/runtime changes. |

## Tier 4: Time, Math, and Randomness

| Status | Module | Why it matters | Not handled / notes |
| --- | --- | --- | --- |
| [~] | `time` | Clocks and sleeps used by tests, retry loops, logging, benchmarks. | Expanded native `_time` and public `time.py` cover clocks, sleep, `gmtime`, `localtime`, `asctime`, `ctime`, `strftime`, `mktime`, CPU clocks, timezone constants, and clock constants. `ninja -C build-debug all check` passed with 813 tests. Full nanosecond clock coverage depends on arbitrary-size integer support. |
| [ ] | `datetime` | Dates, times, timedeltas, parsing/formatting basics. | Not started / not assessed. |
| [ ] | `calendar` | Date helpers used by `datetime`-adjacent code. | Not started / not assessed. |
| [~] | `math` | Numeric functions and constants. | Native `_math` plus Python wrapper exists with tests. `factorial`, `comb`, `perm`, `isqrt`, `gcd`, and `lcm` validate builtin ints/bools and normalize bools to int; custom `__index__` coercion is deferred. Explicit `nextafter` steps use the same builtin int/bool normalization and nonnegative validation. `gcd` uses an explicit integer zero comparison because bigint truthiness is not supported by the VM. Confirm remaining CPython APIs, signatures, errors, and edge cases before marking complete. |
| [ ] | `random` | Tests, IDs, sampling, backoff, simple simulations. | Not started / not assessed. |
| [ ] | `statistics` | Basic statistical helpers. | Not started / not assessed. |
| [ ] | `decimal` | Decimal arithmetic for data and finance code. | Large numeric surface; not started / not assessed. |
| [ ] | `fractions` | Rational arithmetic. | Not started / not assessed. |

## Tier 5: Testing, Debugging, and Developer Tooling

| Status | Module | Why it matters | Not handled / notes |
| --- | --- | --- | --- |
| [~] | `unittest` | Enables many upstream-style tests. | Pure-Python explicit `TestCase(methodName).run(result=None)` and nested `TestSuite.run(result)`, setup/teardown, basic assertions and callable `assertRaises` (ordinary functions, args/kwargs; exception classes or flat tuples). `TestResult` counts runs and stores successes plus `(case, exception)` failures/errors; no traceback strings. Setup failure skips test/teardown, teardown follows test failure, both outcomes retained; `KeyboardInterrupt` propagates (exception type added; OS signal delivery is outside this slice). Named tests invoke ordinary Python methods with an explicit receiver because attribute reads yield unbound functions; bound-method callbacks to `assertRaises` are unsupported. Explicit method name required; no callable case/suite instances (VM limitation), discovery/CLI/text runners, skips/decorators, cleanups, subtests, warning assertions, context-manager raises, rich diagnostics or specialized equality. Suite retains tests for reuse; no CPython suite cleanup or result stop/failfast protocol. Assertion messages are minimal; result constructor takes no runner arguments. |
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

### UserDict boundaries and VM dependencies

- `stdlib/collections.py` exports only `UserDict`. `Counter`, `defaultdict`,
  `namedtuple`, `deque`, `OrderedDict`, `ChainMap`, `UserList`, and `UserString`
  remain absent. `collections.abc` is not supplied by this module.
- `UserDict.fromkeys` is absent until classmethod binding exists. Union operators,
  `__copy__`, pickling, generic aliases, ABC inheritance/registration, and
  hashability parity with arbitrary user classes are outside this slice.
- `copy()` returns an independent shallow copy for exact `UserDict` instances.
  Subclass calls explicitly raise `NotImplementedError`; preserving subclass
  instance state and avoiding constructor/descriptor effects needs `copy` and
  compatible `__new__`/instance-namespace allocation behavior.
- Construction accepts `None`, builtin dictionaries, UserDict instances,
  keys/getitem protocol objects, and supported iterables of supported two-item
  iterables. Updates accept the same sources except `None`. Keywords follow
  positional input. UserDict sources and dict subclasses with a supported
  class-level `__iter__` use iteration ahead of `keys()`; builtin dictionaries
  and dict subclasses without `__iter__` use the keys fallback while native
  dict iteration remains unavailable. Positional-only names such as `self`,
  `dict`, `other`, and `kwds` remain ordinary data keys. Malformed pairs retain
  error types and prior writes; exact diagnostic text is not CPython-compatible. String
  pair elements and string iteration remain blocked by missing `str.__iter__`.
- Direct method calls and class-level `__missing__` dispatch work. Stored bound
  methods remain unusable as general callables. Callable-instance protocol and
  descriptor-based missing handlers are not part of this slice.
- View objects retain the UserDict rather than its current backing dict. They
  support iteration, length, membership, and representation through subclass
  hooks, including replacement of `data`. Set operators/comparisons, public ABC
  view types/registration, generic aliases, and exact generator suspension
  timing are deferred; Python iterator classes replace generators here.
- Keys/values/items iterators each own terminal state: they close permanently
  after exhaustion or an escaping exception, reject re-entry while active, and
  release mapping/iterator references on close. Normal source exhaustion raises
  plain `StopIteration` with empty `args` and `value is None`, discarding any
  delegated iterator's return value. Keys iteration never looks up values;
  values/items convert lookup-raised `StopIteration` to `RuntimeError`.
  Exception chaining (`raise ... from`, `__cause__`, `__context__`) remains
  a general VM gap: the wrapped error type/message is supported, but its original
  StopIteration is not exposed as a cause. Eager key-iterator creation retains
  the existing generator suspension-timing limitation.
- A separate VM lowering gap was observed while implementing iterator cleanup:
  exceptions from a `return` expression inside `try/except/finally` can bypass
  that function's except handlers. The iterators evaluate delegation/lookup into
  a local inside the protected body and return after cleanup. A general codegen
  repair remains outside this slice.
- Equality accepts UserDict and builtin dict operands, honoring `items()`
  overrides and returning `NotImplemented` for other operand types. Supporting
  arbitrary registered Mapping operands needs `collections.abc`/`abc`.
  UserDict materializes `items()` into builtin dicts and delegates to structural
  dict equality. Stored hashes avoid additional user hashing during comparison,
  including nested dict payloads. General recursion-limit enforcement is absent:
  deeply nested or cyclic comparisons are outside the supported slice and may
  exhaust the VM stack rather than reliably raising `RecursionError`.
- The module uses `iter(data.keys())` because `iter(dict)` is unavailable.
  Deletion delegates directly to dict; lookup, deletion, and missing-pop errors
  now retain the original key in `KeyError.args` through generated and native paths.
- Compatibility tests are split across small interpreter source files to stay
  within the 256-entry per-code-object global-read cache limit. They are also
  runnable against CPython 3.14.5; the separate `*_limits.py` file checks only
  CloverVM's intentional reduced surface. No upstream test text is copied;
  module adaptations retain attribution to CPython's collections/mapping code.
- The previously recorded frame assertion is resolved by preserving and checking
  both membership operand shapes during cache replay. A warmed string-key dict
  handler had been reused for other needle shapes, entering general hashing
  through an invalid trusted path. This affected direct and expanded calls.
  UserDict's expanded-call error tests are restored; `membership_cache_replay.py`
  covers changed needle shapes, callback hashing/equality, nested calls, and
  exception propagation. This no longer blocks the UserDict slice.

## Suggested First Slice

These modules should have the highest payoff for running more real Python
source:

| Status | Module | Not handled / notes |
| --- | --- | --- |
| [~] | `sys` | Fill out high-value attributes used by importlib, traceback, warnings, and tooling. |
| [~] | `builtins` | Continue adding common functions and exact exception/type behavior. |
| [~] | `types` | Pure-Python first slice exists with `CodeType`; many descriptor/frame/generator aliases and dynamic class helpers remain. |
| [~] | `operator` | Pure-Python first slice exists for direct VM-supported operators; callable factory helpers and protocol-heavy edge behavior remain. |
| [~] | `collections` | Python UserDict slice exists; classmethod binding, copy/allocation machinery, mapping ABCs, view set operations, and other collections classes remain deferred. |
| [ ] | `abc` | Needed before `collections.abc` and many framework patterns. |
| [ ] | `functools` | `wraps`, `partial`, and cache helpers have high ecosystem payoff. |
| [~] | `itertools` | Initial lazy `chain`, `repeat`, `islice`, and `starmap` slice exists; remaining APIs and protocol/metadata limitations are recorded above. |
| [~] | `contextlib` | First context-manager helper slice exists; generator/decorator/async/stack helpers remain. |
| [~] | `os` | Path/environment/filesystem basics exist; process-heavy, fd, symlink, stat-result, and full path APIs remain. |
| [ ] | `io` | Foundational for files, codecs, archives, and testing tools. |
| [x] | `errno` | Complete for supported host platforms. |
| [~] | `stat` | POSIX mode constants/helpers and `filemode` exist; platform flags and named stat integration remain. |
| [ ] | `pathlib` | Deferred pending descriptor/property support for read-only path attributes; writable attributes and an eager parent chain are excluded. Existing `os`/`stat` support still lacks path-like input handling and CPython-compatible stat results and OS exceptions. |
| [ ] | `re` | Large but unlocks a lot of parsers and stdlib modules. |
| [ ] | `enum` | Useful for modern Python APIs; depends on class semantics. |
| [ ] | `dataclasses` | High payoff after annotations/class mutation behavior is ready. |
| [ ] | `typing` | Can begin as import-compatible runtime objects before full behavior. |
| [ ] | `json` | High-payoff standalone data format. |
| [ ] | `traceback` | Needed for usable failures and test output. |
| [ ] | `warnings` | Common import-time side effects in libraries. |
