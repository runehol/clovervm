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

No confirmed findings have been recorded yet.

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

No review units have been completed yet. The baseline repository mapping and
test run established the campaign starting point but did not constitute a deep
subsystem review.

## Resolved And Rejected Index

Keep a compact index here after entries acquire final dispositions:

| ID | Type | Final status | Resolution |
| --- | --- | --- | --- |
