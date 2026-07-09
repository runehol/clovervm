---
name: implement-stdlib-module
description: Implement or expand a standard library module in clovervm or another Python-compatible repository. Use when Codex is asked to fill out modules like math/time/os/random/builtins, inspect local Python help/signatures, compare CPython source and tests, handle positional-only/keyword-only/*args/**kwargs behavior, split native primitives from Python policy, adapt tests with license attribution, update stdlib bringup status, and verify repo checks.
---

# Implement Stdlib Module

Use this workflow when implementing a standard library module against a Python or CPython-compatible interface.

## Workflow

1. Discover the target interface from the local reference Python.

```bash
python3 -c "import MODULE; help(MODULE)"
```

Treat this as the public surface to audit, but account for the repository's current runtime limits.

Also inspect signatures and call behavior for functions you plan to implement. Pay attention to positional-only parameters, keyword-only parameters, defaults, varargs, kwargs, aliases, constants, exception types, and return shapes:

```bash
python3 - <<'PY'
import inspect, MODULE
for name in sorted(dir(MODULE)):
    obj = getattr(MODULE, name)
    try:
        print(name, inspect.signature(obj))
    except (TypeError, ValueError):
        pass
PY
```

For builtin or native functions where `inspect.signature()` is missing or misleading, use focused Python probes against local Python to establish observable behavior.

2. Find the CPython implementation and tests for the same module.

Prefer the CPython revision requested by the user. If none is specified, use the local Python version as a guide and browse or fetch CPython sources for:

```text
Modules/<module>module.c
Lib/<module>.py
Lib/test/test_<module>.py
```

For modules split across helper files, search CPython for imported helpers and generated clinic files.

When CPython Argument Clinic metadata exists, use it to cross-check signatures and error behavior, but still verify surprising behavior with probes.

3. Inspect the repository's existing module architecture before editing.

Look for:

- public Python module files, usually under `stdlib/`
- private native modules, usually named with a leading underscore
- native module API docs and existing examples
- self-checking Python test harnesses and C++ test harnesses
- build rules for native modules
- existing argument parsing helpers and keyword-call support
- existing exception construction and pending-exception propagation patterns

Use existing naming, ownership, error propagation, and test patterns.

4. Draw the native/Python boundary deliberately.

Default to an outer public module implemented in Python. If native code is needed, keep it behind a private inner module, usually named with a leading underscore, and have the public Python module import and wrap it. Only put code in the C/native private module when it cannot cleanly be done in Python, such as:

- operating system or libc calls
- IEEE/libm behavior, signed zero, NaN, infinities, errno/domain/range handling
- tuple or structured values that require native precision/state
- conversion APIs that must distinguish builtin runtime types

Keep policy, iteration, combinatorics, argument defaults, wrappers, and simple arithmetic in the public Python module when the VM can express them cleanly.

For clovervm, be especially conservative at the native boundary. Native modules should expose small primitives with explicit fallibility; Python-visible defaults, aliases, keyword handling, and compatibility wrappers usually belong in `stdlib/*.py` unless the VM cannot express them yet.

5. Be explicit about runtime limitations.

Do not smuggle in protocol behavior that the VM does not support. If the runtime lacks `__float__`, `__index__`, `__trunc__`, keyword-only arguments, positional-only enforcement, `*args`, `**kwargs`, `type`, `isinstance`, bytes, iterators, context managers, descriptors, file objects, or other protocol machinery:

- implement behavior for builtin values the VM supports
- skip or omit tests requiring unsupported protocols
- avoid clever probes or side channels to fake missing type checks
- note the limitation in the final summary
- record the blocker in the bringup checklist when it affects module completeness

Do not silently flatten CPython call conventions. If the VM cannot represent a signature faithfully, choose an honest partial API or a Python wrapper that preserves the supported behavior without pretending unsupported call forms work.

6. Preserve Python-visible call behavior where supported.

For each implemented callable, decide and test:

- positional arity and default values
- whether keywords are accepted or rejected
- positional-only and keyword-only behavior
- `*args` and `**kwargs` behavior
- alias functions that share implementation but differ in names or docs
- exception type for bad arity, bad type, domain errors, range errors, and OS errors

Prefer small CPython probes for edge behavior rather than relying only on memory or docs. If clovervm intentionally differs because a general VM feature is missing, document that as a limitation rather than hiding it in implementation details.

7. Adapt CPython tests with attribution.

When copying or adapting CPython test content, preserve attribution in every derived file. Include the CPython source path, the commit/version when known, and make clear that portions are derived from PSF-licensed material:

```python
# Python test set -- <module> module
#
# Portions adapted from CPython's Lib/test/test_<module>.py
# at <cpython commit, tag, or version>.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
```

Prefer small self-checking Python test files that fit the repository's compiler/test harness. Keep local adaptation notes outside the copyright header when useful. Limit bigint-heavy cases to values that fit the current VM integer representation when requested.

8. Test edge behavior intentionally.

Cover representative cases for:

- normal values
- domain errors
- range/overflow errors
- NaN and infinities
- signed zero when observable
- return shapes such as tuples
- arity/default behavior the runtime supports
- keyword, positional-only, keyword-only, varargs, and kwargs behavior the runtime supports
- constants, aliases, and module-level metadata users are likely to inspect
- unsupported protocol cases omitted or documented

If a test exposes a general VM bug unrelated to the module, report it and ask
before expanding the task to fix it. Reshape a test only when it exceeds the
explicitly supported surface, and document the limitation rather than hiding it.

9. Update the stdlib bringup checklist.

For clovervm, update `doc/stdlib-module-bringup-checklist.md` before
finalizing module work. Mark the module's status as `[ ]`, `[~]`, or `[x]`,
and record progress, missing APIs, unsupported Python-visible semantics,
runtime blockers, skipped tests, and other snags in the notes column. If the
work uncovers a dependency on another stdlib module or VM feature, record that
dependency rather than silently treating the module as complete.

10. Verify and commit-ready clean up.

Run the repository-required formatter on touched native/C++ files. For clovervm specifically, run:

```bash
clang-format -i <touched C/C++ files>
ninja -C build-debug all check
```

Also run focused tests while iterating. Before finalizing, check `git diff --check` and summarize any limitations left intentionally out of scope.
