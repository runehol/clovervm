---
name: implement-stdlib-module
description: Implement or expand a standard library module in a repository, especially Python-compatible modules. Use when Codex is asked to fill out modules like math/time/os/random, inspect the local Python interface with help(), consult CPython source and tests, split native primitives from Python policy, adapt tests with license attribution, and verify repo checks.
---

# Implement Stdlib Module

Use this workflow when implementing a standard library module against a Python or CPython-compatible interface.

## Workflow

1. Discover the target interface from the local reference Python:

```bash
python3 -c "import MODULE; help(MODULE)"
```

Treat this as the public surface to audit, but account for the repository's current runtime limits.

2. Find the CPython implementation and tests for the same module.

Prefer the CPython revision requested by the user. If none is specified, use the local Python version as a guide and browse or fetch CPython sources for:

```text
Modules/<module>module.c
Lib/<module>.py
Lib/test/test_<module>.py
```

For modules split across helper files, search CPython for imported helpers and generated clinic files.

3. Inspect the repository's existing module architecture before editing.

Look for:

- public Python module files, usually under `stdlib/`
- private native modules, usually named with a leading underscore
- native module API docs and existing examples
- self-checking Python test harnesses and C++ test harnesses
- build rules for native modules

Use existing naming, ownership, error propagation, and test patterns.

4. Draw the native/Python boundary deliberately.

Default to an outer public module implemented in Python. If native code is needed, keep it behind a private inner module, usually named with a leading underscore, and have the public Python module import and wrap it. Only put code in the C/native private module when it cannot cleanly be done in Python, such as:

- operating system or libc calls
- IEEE/libm behavior, signed zero, NaN, infinities, errno/domain/range handling
- tuple or structured values that require native precision/state
- conversion APIs that must distinguish builtin runtime types

Keep policy, iteration, combinatorics, argument defaults, wrappers, and simple arithmetic in the public Python module when the VM can express them cleanly.

5. Be explicit about runtime limitations.

Do not smuggle in protocol behavior that the VM does not support. If the runtime lacks `__float__`, `__index__`, `__trunc__`, keyword-only arguments, `type`, `isinstance`, or other protocol machinery:

- implement behavior for builtin values the VM supports
- skip or omit tests requiring unsupported protocols
- avoid clever probes or side channels to fake missing type checks
- note the limitation in the final summary

6. Adapt CPython tests with attribution.

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

7. Test edge behavior intentionally.

Cover representative cases for:

- normal values
- domain errors
- range/overflow errors
- NaN and infinities
- signed zero when observable
- return shapes such as tuples
- arity/default behavior the runtime supports
- unsupported protocol cases omitted or documented

If a test exposes a general VM bug unrelated to the module, either fix it in the proper layer or reshape the test/module code conservatively without hiding the limitation.

8. Verify and commit-ready clean up.

Run the repository-required formatter on touched native/C++ files. For clovervm specifically, run:

```bash
clang-format -i <touched C/C++ files>
ninja -C build-debug all check
```

Also run focused tests while iterating. Before finalizing, check `git diff --check` and summarize any limitations left intentionally out of scope.
