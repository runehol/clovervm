# Architecture Document Template

Use this metadata table for documents that establish or explore subsystem
designs, cross-layer contracts, or implementation directions. The table
separates a document's role, project commitment, and implementation state; do
not collapse those into one free-form status sentence.

```markdown
# Document Title

| Field | Value |
|---|---|
| Document type | Architecture contract / Design / Implementation plan / Investigation / Historical note |
| Status | Draft / Proposed / Accepted / Experimental / Speculative / Superseded / Rejected |
| Implementation | Not started / Partial / Implemented / N/A |
| Scope | Subsystem or behavior covered |
| Owning layers | Layer ownership, separated by concern when necessary |
| Validated against | Commit and date, or N/A |
| Supersedes | Links, or N/A |
```

## Metadata Values

**Document type** describes the document's role:

- **Architecture contract:** normative cross-layer behavior or invariants.
- **Design:** a coherent subsystem design, accepted or proposed.
- **Implementation plan:** staging and verification for an accepted direction.
- **Investigation:** evidence and alternatives before a decision.
- **Historical note:** retained context that is no longer active direction.

**Status** records commitment level:

- **Draft:** incomplete and not ready for a decision.
- **Proposed:** ready for review but not accepted.
- **Accepted:** active project direction.
- **Experimental:** deliberately being tested before broader commitment.
- **Speculative:** plausible future mechanism with no project commitment.
- **Superseded:** replaced by a linked document or decision.
- **Rejected:** considered and deliberately not selected.

**Implementation** records code state independently:

- **Not started:** no implementing code is expected to have landed.
- **Partial:** some accepted behavior is implemented and remaining work is
  stated explicitly.
- **Implemented:** the documented accepted surface is represented in the code.
- **N/A:** the document does not specify an implementation.

`Validated against` should name a commit and date when the document claims to
describe current code. It is `N/A` for a purely future proposal or historical
record. Validation means the document was checked against that repository
state, not merely edited at that time.

After the table, organize the document in whatever form makes the design easiest
to understand and challenge. State ownership, invariants, current policy,
alternatives, evidence, and revisit conditions where they materially help; do
not add standard sections merely to satisfy the template.
