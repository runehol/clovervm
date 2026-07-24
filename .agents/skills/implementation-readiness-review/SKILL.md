---
name: implementation-readiness-review
description: Establish a shared concrete implementation direction before coding in CloverVM. Use before implementing a nontrivial new subsystem, compiler pass, runtime mechanism, representation, ownership model, public API, or cross-layer change when an equivalent code sketch has not already been agreed, and when the user asks for an implementation-readiness review, class sketch, code sketch, or readiness check.
---

# Implementation Readiness Review

Establish that the user and Codex mean the same concrete design before editing
code. Treat the code sketch as the shared destination, not as a comprehensive
design report.

## Workflow

1. Read the current conversation, accepted design authority, and nearby source
   patterns. Use prior decisions as inputs, but make every decision that
   materially shapes the implementation visible in the sketch.
2. Identify only material points that remain too vague or contradictory to
   implement safely.
3. Present the smallest code sketch that makes the intended types,
   responsibilities, ownership relationships, phase boundaries, and main call
   path concrete.
4. Use a representative awkward case only when it exposes whether the sketch
   actually supports the accepted design.
5. If questions remain, ask only those that block implementation. If none
   remain, state the first implementation boundary and its verification
   concisely.

## Code Sketch

Include only what is needed to confirm direction:

- intended type and method names;
- fields that express important identity, ownership, or lifetime relationships;
- the main construction or call path;
- what the first slice implements and what it only prepares for later.

Omit ordinary method bodies, boilerplate, exhaustive accessors, and speculative
future APIs.

## Guardrails

- Do not fill a standard report template or produce a wall of text.
- Do not reproduce the design discussion chronologically or repeat settled
  rationale. Make every decision that materially shapes the implementation
  visible in the code sketch, including decisions believed to be settled.
- Do not manufacture open questions after the sketch shows the design is ready.
- Treat an earlier sketch as satisfying the review only when it covers the
  current implementation boundary and subsequent discussion has not materially
  changed it.
- Challenge a proposed representation when nearby code or an authoritative
  external implementation contradicts the remembered model.
- If the review changes an accepted design document, update that authority
  before implementation.
- After agreement, treat the sketch as the implementation direction. If coding
  requires a material change, stop and discuss it instead of quietly diverging.
