# Engineering with Coding Agents

This document records working principles for developing clovervm as a small
software team working with coding agents. It describes practices that appear to
become more valuable as implementation capacity increases. These are not claims
that every task should use the same process; they are defaults to test and
refine through the project.

## Operating Model

Coding agents make producing code, documentation, tests, and alternative
designs much cheaper. They do not make those outputs correct. A useful operating
model is:

> Surround probabilistic proposals with explicit constraints,
> deterministic consistency checks, independent evidence, and accountable
> judgment.

The human supplies architectural taste and final judgment, priorities,
historical context, and the decision about which complexity is worth accepting.
The agent contributes to judgment by exploring alternatives, tracing
consequences, and challenging assumptions; it also supplies implementation
bandwidth, consistency checks, and the ability to carry decisions through many
mechanical details. The human grants authority and remains accountable for
accepted outcomes. The repository supplies durable shared memory. Measurements,
independent evidence, and deterministic tools arbitrate claims that should not
be settled by confidence or taste.

Determinism provides consistency, not correctness. A checker reliably enforces
only the properties it encodes, and a test can consistently assert the wrong
behavior. Correctness depends on adequate specifications, independent oracles,
sound models, and evidence that the encoded constraints cover the claim being
made.

## The Bottleneck Moves to Validation

When implementation is expensive, it is rational to economize on code and
tests. When implementation becomes cheaper, changes can be produced and
propagated faster than they can be trusted. The scarce resource moves from
typing capacity toward:

- deciding what should exist;
- specifying invariants precisely;
- finding independent correctness oracles;
- identifying the measurements that resolve hinge decisions;
- reviewing integration across subsystems;
- maintaining a coherent architectural model.

As implementation gets cheaper, good engineering practices become leverage.
Validation capacity must grow alongside implementation capacity.

## Move Taste and Judgment Earlier

Coding agents can increase code-production capacity much faster than a team can
increase experienced review capacity. If implementation is multiplied while
architectural judgment remains concentrated at the end of the process, senior
engineers become bottlenecked reconstructing design intent from large diffs and
rejecting flaws that should have been found before code generation.

The preferred principle is:

> Move taste and judgment earlier, not merely faster.

Before substantial implementation begins, settle enough of the design to
answer:

- which layer owns the behavior;
- which invariants and external contracts must remain true;
- which existing project pattern the change follows;
- which alternatives were considered and why they were rejected;
- which tests, verifiers, or measurements will establish success;
- which result would cause the design itself to be reconsidered.

For committed implementation, the intended flow is:

```text
architectural context
    -> design review
    -> explicit invariants and validation plan
    -> agent-assisted implementation
    -> deterministic checks
    -> focused human code review
```

Some designs need experimental evidence. Use a bounded prototype or benchmark
to answer a specific feasibility question, then make an explicit design
decision before experimental code enters mainline. Clovervm's experimental
generational-GC write barriers followed this pattern.

An underspecified task followed by voluminous agent-generated code transfers
design work into the review queue. Code review should verify an understood
design and catch implementation mistakes; it should not be the first place
where the team discovers what the change was supposed to mean.

Small teams have an advantage when the person directing the agent also carries
the architectural model and can correct course during generation. Larger teams
need clear ownership, early design gates, shared context, and deterministic
checks to obtain the same leverage. In either case, code generation must stay
within the team's capacity for judgment.

## Durable Architectural Memory

Design documents serve as shared external memory as well as plans for
implementation. They should:

- preserve decisions across human and agent context boundaries;
- state the layer that owns a behavior;
- distinguish permanent invariants from initial implementation policies;
- distinguish agreed decisions from plausible future mechanisms and open
  questions;
- explain why important alternatives were rejected or deferred;
- expose contradictions before they become distributed through code;
- give later implementation and review a stable object to challenge.

A design document must not become an inventory of attractive ideas. It should
record commitment level explicitly and become simpler when investigation shows
that a mechanism has not yet earned its cost.

Different repository artifacts preserve different kinds of memory:

- design documents describe the current coherent design;
- the [decision log](decision-log.md) preserves consequential historical
  rationale, rejected alternatives, and conditions for reconsideration;
- Git records the concrete sequence of changes;
- tests and verifiers encode executable contracts, within the limits of what
  they actually check.

Project-level decisions belong in the decision log only when they shape a whole
subsystem or establish contracts across subsystem boundaries. Locally
refactorable choices should remain fluid rather than appear permanently fixed.

New or materially revised architectural documents should use the
[Architecture Document Template](architecture-document-template.md) to separate
document type, commitment status, implementation state, layer ownership, and
validation evidence.

Small, intentional commits provide another level of durable memory. A commit
should make one reviewable claim about the system and leave enough context to
understand why the change was made.

## Deterministic Guardrails

The engineering system, rather than the agent's reasoning alone, provides
repeatable consistency checking. Useful guardrails include:

- compiler errors and strong types;
- ownership and fallibility contracts;
- IR, bytecode, heap, and frame-layout verifiers;
- assertions and checked debug modes;
- unit, integration, differential, and property tests;
- fuzzers and randomized generators;
- sanitizers and static analysis;
- deterministic formatting and build checks;
- benchmark thresholds and code-size measurements;
- reproducible random seeds and minimized failures.

A recurring review finding is evidence that a guardrail may be missing. The
preferred progression is:

```text
human or agent notices a bug pattern
    -> document the invariant
    -> add a focused regression test
    -> encode a verifier, type, or analyser when practical
    -> make the invalid state difficult or impossible to represent
```

A useful rule is:

> Convert recurring review lessons into the cheapest reliable structural check
> when that check costs less than continuing manual review.

Often, the right response is a representation that excludes the invalid state,
a smaller API, or a clearer ownership boundary rather than another test or
analyser. Checks have runtime, maintenance, false-positive, and attention costs;
a growing stream of noisy output is not effective verification.

## A Layered Testing Portfolio

Many focused tests and a smaller number of broad generative tests solve
different problems. Neither replaces the other.

| Test form | Primary purpose |
|---|---|
| Focused semantic test | Precise diagnosis and durable documentation of one behavior |
| Regression test | Permanently captures a discovered failure |
| Structural lowering test | Pins down a high-value compiler or ABI guarantee |
| End-to-end test | Exercises subsystem integration through the public execution path |
| Differential test | Compares against an independently implemented oracle |
| Property or metamorphic test | Checks relationships over many inputs without enumerating outputs |
| Random program or state generator | Discovers interactions the designers did not anticipate |
| Stress and fault-injection mode | Forces rare GC, deoptimization, exception, and invalidation paths |
| Benchmark | Tests a stated performance or code-size hypothesis |

Focused tests should remain small enough that a failure identifies the broken
contract. Broad tests should cross the boundaries where individually correct
components may compose incorrectly.

Tests should assert durable semantics and intentionally chosen invariants, not
incidental implementation details. Structural tests are appropriate when the
structure itself is a design guarantee. Redundant, obsolete, and flaky tests
are validation defects: they consume review and execution capacity while
making legitimate refactoring harder.

Randomized testing becomes operationally useful when failures are reproducible.
Every random test should report and accept a stable seed. Where feasible, a
failing program, heap graph, CFG, or parameter set should be minimized and
retained as a focused regression test.

## Independent Oracles

An agent may implement a feature and write tests that encode the same mistaken
interpretation. Test quantity alone does not remove this correlated-error risk.
Two implementations are not independent merely because their code differs if
they were derived from the same mistaken premise. Expected results are strongest
when their derivation is independent through:

- an authoritative external implementation for the behavior actually in scope;
- a deliberately simple reference interpreter or model;
- independently derived implementations with structurally different failure
  modes;
- algebraic or metamorphic properties;
- shadow mechanisms that run beside an authoritative implementation;
- separately specified invariants checked by a verifier.

For example, an authoritative implementation can remain active while a new
shadow mechanism independently computes the same externally observable result.
Agreement between independently derived mechanisms provides much stronger
evidence than tests written directly against either representation. Reference
implementations still need an explicit version and comparison scope: their
implementation details and documented project deviations are not automatically
part of the required semantics.

## Failure Modes of High-Throughput Development

Expanded implementation reach creates characteristic risks:

**Plausibility cascades.** A mistaken premise can quickly produce a coherent
design, implementation, tests, and documentation. Important premises must be
made explicit and tested independently.

**Premature implementation.** Cheap code generation makes it easy to turn a
shaky, insufficiently examined design into a substantial implementation before
ownership, invariants, alternatives, and validation are understood. That is
acceptable for an explicitly bounded and disposable prototype whose purpose is
to answer a design question. Code intended to land in the maintained mainline
requires design clarity proportional to its blast radius; the existence of a
working implementation is not evidence that its architecture is sound.

**Prototype sedimentation.** Exploratory code can become permanent because it
already exists, even when the experiment did not justify its architecture.
Successful experiments still require an explicit adoption decision and normal
implementation standards.

**Correlated tests.** Tests written from the implementation may confirm its
misconception. Prefer external, reference, property-based, or shadow oracles for
important semantics.

**Architectural drift.** Many locally reasonable changes can erode subsystem
ownership and invariants. Durable design documents and narrow interfaces make
drift visible.

**Optional machinery becoming mandatory accidentally.** A plausible future
optimization can burden the first implementation. Record extension points, but
require evidence before every mechanism is built.

**Validation debt.** Producing code faster than it can be checked creates an
apparently productive but increasingly fragile repository. Verification work
is part of implementation, not a later cleanup phase.

**Guardrail accretion.** Tests and analysers can grow faster than their signal.
Noisy, redundant, or implementation-specific checks consume the same validation
capacity they were intended to protect.

**Tool confidence replacing evidence.** Fluent explanations and clean diffs do
not establish correctness or performance. Builds, tests, sanitizers, verifiers,
and measurements remain authoritative.

## Inspect the Engineering Process

These principles are hypotheses about effective collaboration and should be
reviewed against project experience. Periodically ask:

- where escaped defects are first discovered;
- how often code review uncovers unresolved design rather than implementation
  mistakes;
- which agent-produced changes require substantial rework or reversion;
- whether validation time is growing faster than implementation capacity;
- which tests and checks are flaky, redundant, or routinely ignored;
- which design documents or decision-log entries have become stale;
- whether commits remain small enough to review causally.

Use these questions to notice when the current bottleneck or failure mode has
moved, then adjust the engineering system accordingly.

## Working Principles

- Move taste and judgment earlier, not merely faster.
- Use bounded experiments when feasibility cannot be settled by reasoning.
- Externalize architectural memory.
- Make commitment levels explicit.
- Keep a narrow, well-verified architectural core.
- Treat deterministic checks as consistency mechanisms, not proof that the
  specification is correct.
- Maintain independent correctness oracles.
- Use focused tests for diagnosis and generative tests for discovery.
- Preserve and minimize randomized failures.
- Convert recurring mistakes into structural constraints.
- Keep changes small enough to review causally.
- Measure hinge assumptions before building around them.
- Let sophisticated mechanisms remain optional until evidence justifies them.
- Treat implementation friction as feedback on the design, not something to
  route around automatically.
- Revisit these principles as the project reveals where collaboration with
  coding agents actually succeeds or fails.

Success means expanding the scale of software that a very small team can
understand, validate, and sustain.
