# Review Findings Ledger

This ledger is the durable record for the repository-wide correctness campaign
defined in the [Comprehensive Review Plan](comprehensive-review-plan.md).

Only reachable, high-confidence defects belong under confirmed findings.
Suspicions that still require proof belong in the investigation queue. Missing
features and intentional Python deviations belong in their existing planning or
deviation documents unless their implementation is itself incorrect.

## Ledger Rules

- Give every entry a stable ID of the form `CVR-NNN`.
- Never reuse an ID, including after an entry is rejected or superseded.
- Record the exact reviewed commit or range.
- Link exact source and test locations using repository-relative paths.
- State reachability and observable impact, not just the suspicious code shape.
- Record attempts to disprove the issue.
- Keep the original finding after resolution; append its disposition and fix.
- Do not silently promote an investigation into a finding. Rewrite it against
  the confirmed-finding template and link the original investigation.

Finding status is one of:

- `open`: confirmed and not yet fixed;
- `fix in progress`: an authorized fix is being developed;
- `resolved`: the fix and regression coverage have landed;
- `accepted`: confirmed behavior intentionally remains;
- `superseded`: replaced by another finding with a more accurate boundary.

Investigation status is one of:

- `untriaged`: recorded but not yet examined deeply;
- `investigating`: active evidence gathering;
- `promoted`: established as a confirmed finding;
- `rejected`: disproved or covered by an existing invariant;
- `deferred`: credible but blocked on a named prerequisite.

## Campaign Baseline

- Starting commit: `8cf6c1c`
- Branch: `main`
- Worktree at baseline: clean
- Baseline verification: `ninja -C build-debug all check`
- Result: 1,237 tests passed in 35 suites; one test disabled

## Confirmed Findings

### CVR-001: Slab allocation advances beyond its mapped extent

- Severity: P2
- Status: open
- Review unit: R1
- Found at: `c289490`
- Affected code: `src/memory/slab_allocator.h:119`
- Affected tests: none

Invariant or semantic rule:

A bump allocator must validate the aligned allocation extent before forming its
next cursor. It must not form a pointer outside the allocation or overflow the
rounding arithmetic.

Reachable path:

`SlabAllocator::allocate()` compares `curr_ptr + n_bytes` with `end_ptr`, then
rounds `n_bytes` up to the 32-byte value granularity and advances by the larger
amount. A refcounted ordinary slab starts at offset 16, leaving a usable extent
that is 16 modulo 32. A small final request can therefore fit before rounding
but advance the cursor 16 bytes past the slab. Dedicated allocations reproduce
the same condition whenever their raw requested size is not 32-byte aligned,
because `GlobalHeap::allocate_large_object()` sizes the slab for the unrounded
payload plus the pointer-tag offset.

Observable impact:

Forming the out-of-range cursor is C++ undefined behavior. A later allocation
also evaluates pointer arithmetic from that invalid cursor. The original
payload can remain within mapped memory, so the defect need not produce an
immediate out-of-bounds write or sanitizer report.

Evidence and reproduction:

At `src/memory/slab_allocator.h:121`, the bounds check uses the original
`n_bytes`; lines 126-128 round and advance afterward. For an ordinary
refcounted slab, `65536 - 16 == 65520`, leaving a final 16-byte remainder after
32-byte bumps. For a dedicated allocation of `LargeAllocationSize + 8`, the
raw payload fits exactly but the rounded bump exceeds the constructed slab
extent.

Disproof attempts:

The audit checked whether callers pre-align sizes. They do not: allocation size
is the concrete `sizeof(T)` or dynamic `T::size_for(...)`, and the slab
allocator owns alignment. Dedicated slab sizing also uses the unrounded size.
Existing debug, release, and focused sanitizer tests pass because they validate
payload behavior, not the internal cursor value.

Recommended fix boundary:

Perform checked rounding before the bounds comparison, avoid pointer addition
until the rounded size is known to fit, and compare against the remaining byte
count. Add ordinary-end and non-aligned dedicated-allocation regression tests.

Verification:

The finding is established by allocator arithmetic and reachable slab layouts.
No fix has been implemented or verified yet.

Disposition:

Open pending authorization to implement the focused allocator fix and tests.

Add findings in descending severity, then ascending ID. Use this template:

```md
### CVR-NNN: Short imperative-free summary

- Severity: P0 | P1 | P2 | P3
- Status: open
- Review unit: R1-R10
- Found at: commit or range
- Affected code: `path:line`
- Affected tests: `path:line`, or none

Invariant or semantic rule:

Reachable path:

Observable impact:

Evidence and reproduction:

Disproof attempts:

Recommended fix boundary:

Verification:

Disposition:
```

## Investigation Queue

No open investigations have been recorded yet.

An investigation is not a finding. Use this template:

```md
### CVR-NNN: Question being investigated

- Status: untriaged
- Review unit: R1-R10
- Raised at: commit or range
- Relevant code: `path:line`

Suspected risk:

Evidence needed:

Known guards or caller proofs:

Next check:

Disposition:
```

## Review Unit Log

Record completed review units even when they produce no findings. This prevents
later readers from interpreting an empty findings list as evidence that a
surface was reviewed.

Use this template:

```md
### Rn: Review unit name at commit-or-range

- Scope:
- Design documents read:
- Code and tests reviewed:
- Confirmed findings: none | CVR-NNN, ...
- Investigations: none | CVR-NNN, ...
- Verification commands:
- Verification result:
- Unreviewed edges:
- Residual risk:
```

### R1: Memory, Ownership, And Reclamation At `c289490`

- Scope: all 33 registered native layouts and their release/object-size
  descriptors; custom teardown; heap construction; refcount and lifecycle
  transitions; ZCT and epoch discovery; slab pins, bitmaps, allocation, and
  reuse; managed root collection; and native/managed re-entry publication.
- Design documents read: `refcounting-and-reclamation.md`,
  `heap-object-metadata.md`, `native-layout-descriptors.md`,
  `typed-handles-and-expected.md`, `native-managed-boundaries.md`, and
  `heap-slab-allocation-and-reuse.md`.
- Code and tests reviewed: `src/memory`, native layout declarations and
  registry, every registered heap record, `Shape` and `CodeObject` custom
  teardown, thread-state call adapters, interpreter native and import re-entry
  paths, and the focused heap, reclamation, layout, dictionary, import, and
  safepoint tests.
- Confirmed findings: CVR-001.
- Investigations: none.
- Verification commands: `ninja -C build-debug all check` at the campaign
  baseline; focused debug and release test filters; the R1-focused ASan+UBSan
  filter with the diagnostic macOS container-overflow workaround; and
  `cmake --build build-release --target check_opcode_frames`.
- Verification result: the baseline and final debug gates each passed 1,237
  tests. The consolidated release R1 filter passed 83 tests, the diagnostic
  ASan+UBSan R1 filter passed 65 tests, and the re-entry filter passed 69 tests.
  The opcode frame checker passed. The full macOS sanitizer binary still aborts
  during GoogleTest static registration with container-overflow checking
  enabled, before CloverVM tests run.
- Unreviewed edges: multithreaded refcount and lifecycle atomics, cycle
  collection, moving-GC barriers, and future JIT root maps are designs rather
  than implemented R1 surfaces and were not reviewed as current behavior.
- Residual risk: static release-span contiguity and custom teardown completeness
  are convention-based rather than mechanically tied to fields. Dynamic count,
  lifecycle, pin, and bitmap corruption is guarded primarily by debug asserts,
  though no reachable invariant break was found. `clover_frame_frontier`
  currently carries both a frame-chain anchor concept and, for arbitrary-frame
  re-entry, a lowest-live-slot scan boundary; the current contiguous scanner is
  safe, but future frame-chain or JIT work must separate those concepts.

## Resolved And Rejected Index

Keep a compact index here after entries acquire final dispositions:

| ID | Type | Final status | Resolution |
| --- | --- | --- | --- |
