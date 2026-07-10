# Comprehensive Correctness Review Plan

This document defines the repository-wide correctness review campaign for
CloverVM. It is a review plan, not a development roadmap. Feature priority
remains in [Development Priorities](development-priorities.md), while confirmed
review results are recorded in the
[Review Findings Ledger](review-findings-ledger.md).

The review is organized around VM invariants and Python-visible behavior rather
than a flat file-by-file reading. Each review unit should be small enough to
inspect deeply, challenge suspected failures, and verify findings with focused
tests.

## Objectives

- Find reachable correctness defects, especially those hidden by passing tests.
- Check that implementation and design documents describe the same invariants.
- Exercise release-only behavior, ownership boundaries, and cache invalidation.
- Add focused regression tests for confirmed defects when fixes are authorized.
- Keep uncertain concerns separate from confirmed findings.
- Produce a durable record that later reviews can extend without repeating work.

Performance is in scope only where it creates a correctness constraint, such as
the interpreter's frameless `musttail` dispatch contract. General performance
investigation remains separate.

## Evidence Standard

A confirmed finding must establish all of the following:

1. The affected invariant or Python-visible rule.
2. A reachable path from supported VM or Python behavior.
3. Why an existing caller proof, guard, or invariant does not prevent it.
4. The observable consequence in debug or release builds.
5. A focused reproduction or equally strong code-path proof.

Green tests reduce risk but do not disprove a finding. Conversely, a broad
search result, suspicious assertion, stale document, or unsupported Python
feature is not a finding by itself.

Use these severities:

- `P0`: crash, data corruption, security issue, or pervasive VM breakage.
- `P1`: likely semantic bug, release-only memory or metadata bug, or common
  user-visible failure.
- `P2`: uncommon but real semantic failure, invalidation error, or credible
  correctness gap.
- `P3`: maintainability defect only when it materially increases bug risk.

## Baseline

The campaign began from commit `8cf6c1c` on `main` with a clean worktree.

The initial baseline command was:

```sh
ninja -C build-debug all check
```

It passed 1,237 tests in 35 suites, with one disabled test. Each review unit
must record its own commit or commit range because the baseline will move as
work lands.

## Invariant Map

Before reviewing a subsystem, extract its applicable contracts from nearby code
and the focused design documents. At minimum, consider:

- pending-exception creation, propagation, handling, and clearing;
- borrowed `Value` and `TValue<T>` lifetimes;
- `Owned<...>` local roots and `Member<...>` heap fields;
- root visibility across allocation, VM dispatch, and native re-entry;
- native layout size, tracing, member-count, and teardown metadata;
- shape transitions, validity cells, and inline-cache invalidation;
- bytecode operand widths, instruction lengths, register lifetimes, and jumps;
- interpreter handler signatures, dispatch shape, and `musttail` constraints;
- Python evaluation order, error type and timing, and protocol fallback;
- checked integer conversions, overflow, and SMI/BigInt boundaries.

When implementation and documentation disagree, determine which behavior is
intentional before classifying the mismatch. Do not silently choose one as the
source of truth.

## Review Units

### R1: Memory, Ownership, And Reclamation

Primary surfaces are `src/memory`, native layout descriptors, heap object
definitions, direct heap fields, and native re-entry root publication.

Check object sizes and dynamic member counts, tracing and teardown coverage,
borrowed handles that cross allocation, slab lifecycle transitions, zero-count
table invariants, and correctness that depends on debug-only assertions.

### R2: Pending Exceptions And Fallible Boundaries

Trace producers and consumers of `Expected<T>`, `Value::exception_marker()`,
pending exception state, `CL_TRY`, and `CL_PROPAGATE_EXCEPTION`.

Check ignored fallible results, successful-looking returns with pending state,
exception replacement during cleanup, opcode handlers using the wrong
propagation path, and helpers whose return types conceal fallibility.

### R3: Dictionaries, Hashing, And Equality

Review exact-string and general dictionary shapes, one-way promotion, generated
method bytecode, trusted handlers, probe generations, insertion order, views,
hash normalization, and equality callbacks that can mutate or raise.

This unit should explicitly distinguish VM-owned exact-string tables from
Python-visible dictionaries such as `sys.modules`.

### R4: Bytecode And Interpreter Integrity

Follow each operation through code generation, bytecode encoding and metadata,
printing, dispatch installation, handler execution, and instruction advance.

Check missing handlers, mismatched operand widths, invalid register windows,
unrooted temporaries, release-only bounds failures, hidden raising work, and hot
handler frame generation.

### R5: Object Model, Attributes, Descriptors, And Caches

Review attribute reads, writes, and deletes separately. Check lookup precedence,
descriptor classification and binding, cached payload ownership, mutation
invalidation, trusted-handler requirements, and observable behavior skipped by
cache replay.

Keep descriptor execution, generic callable dispatch, and the direct method-call
bridge as separate contracts even where their cache machinery composes.

### R6: Calls, Frames, And Argument Adaptation

Review fixed positional calls, defaults, explicit keywords, callee `*args` and
`**kwargs`, native calls, constructors, and direct methods independently.

Check caller-window versus callee-frame register interpretation, fresh container
ownership, keyword ordering and collisions, error timing, default copying, and
call-cache guards. Unsupported caller unpacking is not a defect unless the VM
claims to support it or mishandles it before producing the intended error.

### R7: Builtin Types And Operators

Split the work into numeric types, strings and Unicode, sequences and iterators,
dictionaries and views, hashing, and equality.

For each operation test exact types, subclasses, reflected dispatch,
`NotImplemented`, wrong operand types, callback mutation, overflow and
narrowing, pending exceptions, bool-as-int behavior, and SMI/BigInt transitions.

### R8: Compiler And Python Semantics

Trace supported syntax through tokenizer, parser and AST, scope analysis,
codegen, and interpreter behavior.

Prioritize evaluation order, assignments, globals and locals, class scope,
calls, exception control flow, short-circuiting, comparisons, membership, and
imports. Prefer interpreter-level differential probes against CPython; reserve
codegen tests for structural guarantees.

### R9: Imports, Native Modules, And Public APIs

Review module namespace semantics, partial import initialization, loader and spec
state, filesystem and `errno` conversion, native argument checking, extension
ownership, and native-managed transitions.

### R10: Cross-Cutting Adversarial And Release Review

Combine subsystem boundaries: callbacks that mutate caches or containers,
allocation during adaptation, exceptions during cleanup, failed imports,
native re-entry, large integers entering SMI-sized consumers, and all behavior
whose safety differs under `NDEBUG`.

## Review Procedure

For each review unit:

1. Record the exact commit, files, and semantic boundary being reviewed.
2. Read the relevant design documents and implementation paths.
3. Inspect recent risky diffs before broad mechanical searches.
4. Apply the invariant map and construct adversarial cases.
5. Try to disprove every suspected defect using guards, caller proofs, existing
   tests, and CPython behavior where relevant.
6. Add only high-confidence defects to confirmed findings.
7. Put unresolved but credible concerns in the investigation queue.
8. Record commands, test results, unreviewed edges, and residual risk.

The default verification gate remains:

```sh
ninja -C build-debug all check
```

Interpreter or hot-opcode work should also run:

```sh
cmake --build build-release --target check_opcode_frames
```

Memory and release-sensitive units should use dedicated ASan and UBSan builds.
Relevant release benchmarks should be run after correctness fixes that touch hot
paths, but benchmark changes do not determine finding severity.

## Campaign Order

Run the units in this order unless evidence reveals a stronger dependency:

1. R1 memory, ownership, and reclamation
2. R2 pending exceptions and fallible boundaries
3. R3 dictionaries, hashing, and equality
4. R4 bytecode and interpreter integrity
5. R5 object model, descriptors, and caches
6. R6 calls, frames, and adaptation
7. R7 builtin types and operators
8. R8 compiler and Python semantics
9. R9 imports, native modules, and public APIs
10. R10 cross-cutting adversarial and release review

The first four units establish the substrate needed to reason confidently about
the higher-level semantic reviews.

## Completion Criteria

The campaign is complete when every review unit has:

- a recorded commit range and reviewed surface;
- confirmed findings or an explicit statement that none were found;
- verification results and residual risks;
- disposition for every investigation opened during that unit; and
- links to any fixes and regression tests that subsequently landed.

Completion means the defined surfaces received evidence-backed review. It does
not claim that the VM contains no undiscovered defects.
