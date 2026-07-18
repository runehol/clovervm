# Engineering with Coding Agents

This document records working principles for a small team developing software
with coding agents. They are defaults to test and refine through the project,
not a process required for every task.

## Operating Model

Coding agents make producing code, documentation, tests, and alternative
designs much cheaper. They do not make those outputs correct. A useful operating
model is:

> Surround probabilistic proposals with explicit constraints, deterministic
> consistency checks, independent evidence, and accountable judgment.

The human supplies architectural taste and final judgment, priorities,
historical context, and the decision about which complexity is worth accepting.
The agent contributes to judgment by exploring alternatives, tracing
consequences, and challenging assumptions; it also supplies implementation
bandwidth, consistency checks, and the ability to carry decisions through many
mechanical details. The human grants authority and remains accountable for
accepted outcomes. The repository supplies durable shared memory. Measurements,
independent evidence, and deterministic tools arbitrate claims that should not
be settled by confidence or taste.

Determinism provides consistency, not correctness. A checker enforces only the
properties it encodes, and a test can consistently assert the wrong behavior.
Correctness depends on adequate specifications, independent oracles, sound
models, and evidence that the encoded constraints cover the claim being made.

As implementation gets cheaper, validation, architectural judgment, and
integration become the scarce resources. Validation capacity must grow
alongside implementation capacity.

## Keep Judgment in the Loop

Coding agents can increase code-production capacity much faster than a team can
increase experienced review capacity. If architectural judgment remains at the
end of the process, reviewers become bottlenecked reconstructing design intent
from large diffs and rejecting flaws that should have been found before code
generation.

> Move taste and judgment earlier, not merely faster.

Before substantial implementation begins, settle enough of the design to
answer:

- which layer owns the behavior;
- which invariants and external contracts must remain true;
- which alternatives are credible and why one is preferred;
- which tests, verifiers, or measurements will establish success or reopen the
  design.

For consequential work, use the design document as an active object of
collaboration rather than writing it once as a preliminary specification:

```text
architectural context
    -> working design document
       +-> adversarial review -> revision -+
       |                                   |
       +-------- repeat until coherent ----+
    -> explicit decisions, invariants, open questions, and validation plan
    -> milestone implementation plan
       +-> reviewable step -> agent-assisted implementation
       |       -> deterministic checks -> focused review -> coherent commit -+
       |                                                                    |
       +-------------- adjust plan and repeat until complete ---------------+
                              |
                              +-> architectural discoveries return to the
                                  working design document
    -> reconcile design with the implemented system
```

Coding agents are naturally agreeable, so adversarial review must be assigned
explicitly. An agent asked to develop a design may accept its framing and make a
weak premise sound increasingly coherent. Prompt it separately to argue against
the current design: challenge premises, seek contradictions, expose ambiguity,
and compare credible alternatives. Revision turns the surviving conclusions
into constraints on later work.

Some designs need experimental evidence. Use a bounded prototype or benchmark
to answer a specific feasibility question, then make an explicit design
decision before experimental code enters mainline.

The implementation plan should divide work into reviewable milestones, identify
the checks that complete each step, and separate prerequisite experiments from
production changes. Work through it incrementally, reviewing and committing
coherent steps rather than accumulating one large implementation. Implementation
may expose facts the design missed; revise the document and plan rather than
allowing the code to become an unrecorded design decision.

The appropriate degree of autonomy varies by task. Size is a poor proxy for how
safely work can proceed without guidance; the important variables are:

- **Discretion:** how often implementation requires an unsettled choice about
  semantics, ownership, architecture, or policy;
- **Validation:** how independently and mechanically success can be checked;
- **Reversibility:** how expensive a mistaken choice becomes once propagated
  through code, tests, and documentation.

Agent autonomy works best when semantics come from an external authority,
integration follows an established pattern, and completion can be judged by
independent tests. A substantial standard-library module may meet these
conditions, while a small runtime change may carry enough architectural
discretion to require frequent guidance. Low discretion, strong validation,
and cheap reversibility support longer autonomous runs. Shorten the feedback
loop as those conditions weaken and review decisions as they emerge.

Early design work leaves code review focused on implementation mistakes and
integration, with the reviewer already understanding the intended architecture.

As implementation concludes, reconcile the design document with what was
actually built. Rewrite proposals as the present design, record consequential
departures from the original plan, and focus the result on the ownership,
invariants, and reasoning future readers will need. Git already preserves the
incidental steps and discarded implementation details.

## Durable Architectural Memory

Design documents serve as shared external memory and as the basis for
implementation plans. They should state ownership and invariants, distinguish
current commitments from initial policies and open possibilities, and become
simpler when a mechanism has not earned its cost.

Different repository artifacts preserve different kinds of memory:

- design documents describe the current coherent design;
- the [decision log](decision-log.md) preserves consequential historical
  rationale, rejected alternatives, and conditions for reconsideration;
- Git records the concrete sequence of changes;
- tests and verifiers encode executable contracts, within the limits of what
  they actually check.

Project-level decisions belong in the decision log when they shape a whole
subsystem or establish contracts across subsystem boundaries. Locally
refactorable choices should remain fluid. New or materially revised
architectural documents should use the
[Architecture Document Template](architecture-document-template.md).

## Build a Validation System

The engineering system, rather than the agent's reasoning alone, provides
repeatable consistency checking. Its layers include:

- types, ownership rules, verifiers, and constrained representations that make
  invalid states difficult to express;
- focused semantic and regression tests that diagnose failures and document
  durable behavior;
- generative, differential, metamorphic, stress, and fault-injection tests that
  discover interactions outside anticipated examples;
- builds, sanitizers, static analysis, and benchmarks that check integration,
  safety, performance, and code size.

A recurring review finding may reveal a missing guardrail. Convert it into the
cheapest reliable structural check when that costs less than continuing manual
review. Often the right response is a smaller API, clearer ownership boundary,
or representation that excludes the invalid state rather than another test or
analyser. Checks themselves consume execution, maintenance, and attention; a
stream of noisy output provides little protection.

Focused tests and broad tests solve different problems. Focused failures should
identify a broken contract. Broad tests should cross boundaries where
individually correct components may compose incorrectly. Tests should assert
durable semantics and intentionally chosen invariants, not incidental
implementation details. Redundant, obsolete, and flaky tests are validation
defects that impede legitimate refactoring.

An agent may implement a feature and tests that encode the same mistaken
interpretation. Test quantity does not remove this correlated-error risk, and
two implementations derived from the same premise are not independent merely
because their code differs. Stronger oracles come from independent derivation:
an authoritative external implementation for the behavior in scope, a simple
reference model, algebraic or metamorphic properties, a shadow mechanism with
different failure modes, or a separately specified invariant. External
references still need an explicit version and comparison scope; their
implementation details are not automatically required semantics.

## Failure Modes of High-Throughput Development

**Plausibility cascades.** A shaky premise can quickly produce a coherent
design, implementation, tests, and documentation. Substantial mainline code
requires design clarity proportional to its blast radius; a working
implementation does not establish that its architecture is sound.

**Prototype sedimentation.** Exploratory code can become permanent because it
already exists. A successful experiment still requires an explicit adoption
decision and normal implementation standards.

**Architectural drift.** Many locally reasonable changes can erode subsystem
ownership and invariants. Durable design documents and narrow interfaces make
drift visible.

**Guardrail accretion.** Agents readily answer each finding with another focused
test or checker. Periodically consolidate or remove overly specific guardrails
that no longer earn their cost.

## Inspect the Engineering Process

These principles are hypotheses about effective collaboration. Periodically
ask:

- where defects and unresolved design are first discovered;
- which changes require substantial rework or reversion;
- whether milestone steps remain reviewable, architectural discoveries return
  to the design, and completed documents match the implementation;
- which validation mechanisms are noisy, stale, or routinely ignored;
- whether validation or review has become the current bottleneck.

Use the answers to adjust the engineering system as its bottlenecks and failure
modes move.

## Working Principles

- Move taste and judgment earlier, not merely faster.
- Iterate working design documents through explicit adversarial review.
- Derive milestone plans from coherent designs and implement them in reviewable,
  validated steps.
- Grant more autonomy as discretion falls, independent validation strengthens,
  and mistakes become cheaper to reverse.
- Use bounded experiments and measurements to resolve hinge assumptions, and
  revisit the process as bottlenecks move.
- Externalize architectural memory, make commitment levels explicit, and
  reconcile completed designs with the implementation.
- Treat deterministic checks as consistency mechanisms and maintain
  independently derived correctness oracles.
- Combine focused tests for diagnosis with generative tests for discovery.

Success means expanding the scale of software that a very small team can
understand, validate, and sustain.
