---
name: clovervm-correctness-review
description: Review clovervm changes for likely correctness bugs. Use when asked to review a commit, diff, branch, PR, recent changes, automation scan, or risky implementation area for regressions involving Python semantics, pending exceptions, ownership/lifetime, GC metadata, integer bounds, release-only behavior, interpreter invariants, parser/codegen lowering, or VM object-model consistency.
---

# clovervm Correctness Review

Use this workflow as a bug-finding review, not a style pass. Findings should lead the answer, ordered by severity, with file/line references when available.

## Workflow

1. Define the review scope.

Identify the exact range or surface:

- staged or unstaged diff
- a commit or commit range
- a PR or branch
- recent commits since an automation memory timestamp
- a subsystem such as parser, codegen, interpreter, object model, native modules, GC, or stdlib

If the scope is ambiguous, inspect local context first. For GitHub PR checks, review threads, or remote metadata, use the GitHub workflow/tooling rather than guessing.

2. Read the risky diff before running broad searches.

Start with:

```bash
git diff --stat
git diff
git show --stat <commit>
git show <commit>
```

Then inspect nearby code and existing tests. Do not rely on commit messages, green tests, or broad grep results alone.

3. Apply the clovervm risk taxonomy.

Prioritize these bug classes:

- Python-visible semantic regressions: evaluation order, descriptor binding, call behavior, scope rules, exception type/timing, deletion, `NotImplemented`, metaclass behavior.
- Pending exception propagation: dropped `Expected<T>`, ignored `Value::exception_marker()`, missing `CL_TRY`/`CL_PROPAGATE_EXCEPTION`, clearing or overwriting pending exception state incorrectly.
- Ownership/lifetime/refcount: borrowed `Value` kept alive without `Owned`, heap object members using `Owned` instead of `Member`, missing retention across allocation or bytecode execution, stale handles after GC-visible operations.
- GC/reclamation metadata: object size/count mismatches, shape/member count errors, missing tracing of `Member` fields, invalid metadata used by reclamation or allocation.
- Release-only bugs: assertions required for correctness, unchecked narrowing, signed/unsigned conversion, overflow, out-of-bounds access hidden in debug-only checks.
- Interpreter invariants: `MUSTTAIL` signature mismatch, opcode handlers setting up hot-path frames, custom instruction length bugs, slow/raising behavior hidden in inline helpers.
- Parser/codegen lowering: AST shape mismatches with Python semantics, binding/global/nonlocal mistakes, register lifetime or not-present checks, jump target and stack/register discipline.
- Cache/invalidation: stale inline cache facts, missing validity-cell guard, terminal fast path skipping observable behavior, shape/class/metaclass mutation not invalidating assumptions.
- Native boundary: C/POSIX error handling, errno mapping, argument conversion, unsupported protocols silently faked, native functions that can set pending exceptions without explicit fallible returns.

4. Try to disprove each suspected issue.

For every candidate finding, check:

- Is there an existing invariant or caller proof?
- Is the path reachable from Python code?
- Does a debug assertion merely document the invariant, or is it required for safety in release?
- Would CPython semantics actually require different behavior?
- Is there a focused test already covering this edge?

Only report high-confidence bugs as findings. Put uncertain concerns under residual risk or open questions.

5. Verify with focused tests when practical.

Prefer narrow tests for suspected issues. For completed local code changes, run:

```bash
ninja -C build-debug all check
```

When reviewing `src/runtime/interpreter.cpp` or hot opcode handlers, also consider the release hot-path checker:

```bash
cmake --build build-release --target check_opcode_frames
```

If a command cannot be run, say exactly why. Do not treat unrun tests as evidence.

6. Produce review output in code-review form.

Lead with findings:

```text
Findings
- [P1] file:line: bug summary
  Explanation of the broken path and why it is observable/reachable.

Open questions / residual risk
- ...

Verification
- ...
```

If no issues are found, say that clearly and include the remaining risk surface and tests run.

## Severity Guide

- `P0`: crash/data corruption/security issue or pervasive VM breakage.
- `P1`: likely semantic bug, release-only memory/metadata bug, or common user-visible failure.
- `P2`: edge-case semantic bug, uncommon but real wrong behavior, missing invalidation, or test gap with credible risk.
- `P3`: maintainability concern only when it materially increases bug risk.

## Guardrails

- Do not pad reviews with style comments.
- Do not report speculative issues as findings.
- Do not propose compatibility layers unless existing repo usage requires them.
- Green tests reduce risk but do not prove correctness.
- Correctness review is separate from performance review; mention performance only when it creates a correctness hazard.
