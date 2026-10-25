---
name: implement-stdlib-module
description: Implement or expand a clovervm standard-library module against CPython behavior.
---

# Implement Stdlib Module

Use this workflow when implementing a standard library module against a Python or CPython-compatible interface.

## Relationship to parallel work

For a bounded parallel CloverVM task, first use
`clovervm-parallel-intent-review` and implement only the agreed slice. For deep single-thread design, retain the
existing `implementation-readiness-review` skill. These are different review
contexts; do not impose both reviews on the same already-agreed boundary.
Follow AGENTS.md routing. If the selected skill is unavailable, report that
integration problem rather than pretending to have applied it.

A parallel-task sketch approval does not authorize core bytecode operations,
interpreter execution machinery, or core code generation changes. If required,
stop, explain the exact crossing and alternatives, and wait for explicit human
approval of that core change. The user can authorize an exception; it must be a
visible decision, not an implementation detail buried in a larger sketch.

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

Do not silently flatten CPython call conventions. Unsupported behaviors can be
excluded when defining the slice, with those limits visible in its review. Once
a behavior is agreed, discovering that it cannot be supported requires a decision
before reducing the API, acceptance criteria, or test coverage. Documenting an
omission does not authorize it. A wrapper may preserve supported call forms; it
must not fake missing VM protocols.

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

If a probe or test exposes a missing dependency or general VM bug, preserve a
small reproducer, expected/actual behavior, reference and VM revisions, and the
relevant configuration. Determine whether it actually blocks the agreed slice.
Fix defects caused by your approved change within its boundary. For an independent
prerequisite, report it before expanding scope and wait for the relevant approval,
including the core-change gate where applicable. Once explicitly authorized,
complete that repair within its agreed boundary. Do not fake the missing protocol
locally. A baseline claim needs baseline evidence. Do not dismiss a crash merely
because it appears pre-existing.

Establish the relevant language/API contract before classifying a CPython
difference as an implementation detail. Sort outcomes under a non-total comparator
or callback-probe counts may differ legitimately, but extra callbacks can also
change exceptions, mutations or other observable behavior, and some APIs promise
specific call counts. Acceptance of one difference is not a blanket exemption for
similar-looking cases. Make material compatibility decisions visible to the user;
neither every difference nor every green differential run decides the right scope.

Reshape a test only when it exceeds the explicitly supported surface, and
document the limitation rather than hiding it. For a blocked parallel task,
produce the restart brief described by `clovervm-parallel-intent-review` and let
the coordinator own scheduling. A prerequisite landing calls for a fresh review
against current main, not automatic reuse of the old implementation approval.
Carry forward the human's standing constraints and accepted compatibility choices;
reopen them only when new evidence materially affects the decision.

9. Update the stdlib bringup checklist.

For clovervm, update `doc/stdlib-module-bringup-checklist.md` before
finalizing module work. Mark the module's status as `[ ]`, `[~]`, or `[x]`,
and record progress, missing APIs, unsupported Python-visible semantics,
runtime blockers, skipped tests, and other snags in the notes column. If the
work uncovers a dependency on another stdlib module or VM feature, record that
dependency rather than silently treating the module as complete.

10. Verify the implementation.

Use the task's established host resource controls for all build/check commands,
including examples here and commands in referenced review skills. In a shared
parallel wave, run substantive checks under the agreed build lock and parallelism
limit. The examples name required checks; they do not waive those controls.

Run the repository-required formatter on touched native/C++ files. For clovervm specifically, run:

```bash
clang-format -i <touched C/C++ files>
ninja -C build-debug all check
```

Also run focused tests while iterating. Before finalizing, check `git diff --check` and summarize any limitations left intentionally out of scope.

11. Review the completed code with `clovervm-correctness-review`.

Before review, account for temporary scaffolding as described in the parallel
intent review when applicable: remove disposable edits, preserve useful evidence,
or explicitly include an agreed production/test change. Do not discard unrelated
work.

After implementation and verification, apply that skill to all intended task
changes and relevant surrounding code without waiting for another user prompt.
Inventory modified, staged and untracked files; ordinary `git diff` alone omits
untracked new modules and tests. Read those files explicitly. State the reviewed
files and content/revision so later changes can be identified. This is
a bug-finding pass, not a summary of the implementation or an inference from
green tests. Name the reviewed scope and report findings, residual risk, and
verification. Do not imply an independent reviewer was used unless one was.

Fix confirmed in-scope defects, add meaningful regressions, and rerun affected
checks. Re-review the changed paths and any affected assumptions before declaring
the result ready. Do not repeat unchanged checks merely for ceremony; repository
requirements, including exact-HEAD checks before push, still apply. Follow any
explicit user instruction to stop on a finding rather than fix it. Material scope
changes and unrelated prerequisites retain their approval gates.

12. Complete the authorized delivery stage.

Keep the requested end state explicit: readiness only, reviewed uncommitted
changes, or a reviewed PR. The default after a clean local review is a human
delivery gate: present the concrete reviewed result, checks and remaining limits,
and leave it uncommitted for inspection. Suggest a concise next prompt such as
“Commit the reviewed changes, run the required checks at that commit, push, and
open a PR.” This is a proposed human instruction, not permission to execute it.
Do not suggest publication while unresolved blockers prevent readiness.

Implementation approval and local correctness review do not by themselves
authorize publication. If the user already authorized
commit, push, and PR creation after a clean review and passing required checks,
complete those steps without asking again or stopping at “ready to commit.”
Respect a “no commits or pushes” instruction until the user supersedes it.

For authorized PR delivery, commit only the intended changes, run the required
release checks at the exact HEAD being pushed, push the branch, create and attach
the PR using the available project tools, and report the commit and PR link with
verification and limitations. Follow the repository branch/base and integration
rules. Tie the local review and human delivery approval to the reviewed content.
After a rebase, conflict resolution or other integration edit, inspect what
changed from that content, review the affected differences and assumptions, and
rerun relevant checks. Do not claim the earlier review covers new edits merely
because tests pass. Return to the user if the implementation boundary materially
changes; a changed commit hash alone does not require renewed approval. Required
exact-HEAD pre-push checks still apply.

Do not claim GitHub CI or automated review passed before checking them.
PR creation does not authorize merging, posting a review, or ongoing monitoring;
follow the user's actual authorization for those actions.
