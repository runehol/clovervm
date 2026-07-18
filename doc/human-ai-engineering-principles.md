# Human-AI Engineering Principles

This document records working principles for developing clovervm as a small,
high-capability human-AI engineering team. It describes practices that appear
to become more valuable as implementation capacity increases. These are not
claims that every task should use the same process; they are defaults to test
and refine through the project.

## Operating Model

AI makes producing code, documentation, tests, and alternative designs much
cheaper. It does not make those outputs correct. The central engineering model
is therefore:

> Put a probabilistic proposer inside a deterministic system of constraints.

The human supplies architectural judgment, priorities, historical context, and
the decision about which complexity is worth accepting. The AI supplies broad
exploration, implementation bandwidth, consistency checks, and the ability to
carry decisions through many mechanical details. The repository supplies
durable shared memory. Measurements and deterministic tools arbitrate claims
that should not be settled by confidence or taste.

The effective team is:

```text
human judgment and direction
            +
AI exploration and implementation
            +
repository memory and deterministic verification
            +
empirical measurement
```

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

Good engineering practices become leverage rather than administrative
overhead. The objective is not to slow production to the former rate. It is to
increase trustworthy validation capacity to match the expanded implementation
capacity.

## Durable Architectural Memory

Design documents are shared external memory, not merely plans written before
coding. They should:

- preserve decisions across human and AI context boundaries;
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

New or materially revised architectural documents should use the
[Architecture Document Template](architecture-document-template.md) to separate
document type, commitment status, implementation state, layer ownership, and
validation evidence.

Small, intentional commits provide another level of durable memory. A commit
should make one reviewable claim about the system and leave enough context to
understand why the change was made.

## Deterministic Guardrails

The engineering system, rather than the language model alone, provides the
equivalent of deliberate checking. Useful guardrails include:

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

A recurring review finding is evidence that a guardrail is missing. The
preferred progression is:

```text
human or AI notices a bug pattern
    -> document the invariant
    -> add a focused regression test
    -> encode a verifier, type, or analyser when practical
    -> make the invalid state difficult or impossible to represent
```

The strongest general rule is:

> Every recurring review lesson that can become a deterministic check should
> eventually become one.

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

Randomized testing is useful only when failures are reproducible. Every random
test should report and accept a stable seed. Where feasible, a failing program,
heap graph, CFG, or parameter set should be minimized and retained as a focused
regression test.

## Independent Oracles

An AI may implement a feature and write tests that encode the same mistaken
interpretation. Test quantity alone does not remove this correlated-error risk.
Expected results are strongest when obtained independently through:

- CPython behavior;
- a deliberately simple reference interpreter or model;
- two structurally different implementations;
- algebraic or metamorphic properties;
- shadow mechanisms that run beside an authoritative implementation;
- separately specified invariants checked by a verifier.

For example, canonical frame publication can remain authoritative while a
future shadow stack-map walker independently enumerates roots and compares its
result. Agreement between independently structured mechanisms provides much
stronger evidence than tests written directly against either representation.

## CloverVM Verification Opportunities

High-value future systems include:

- generating valid Python programs and comparing clovervm with CPython;
- executing the same bytecode in interpreted and compiled modes and comparing
  values, exceptions, and visible mutations;
- forcing each eligible JIT guard or side exit to fail and checking exact
  interpreter continuation;
- injecting safepoints and collections at every permitted boundary;
- generating heap graphs and comparing collection with a simple reachability
  model;
- running IR verification after every mutating pass in debug builds;
- generating CFGs with block arguments and stressing edge splitting, loops,
  parallel-copy cycles, and critical edges;
- shadowing future compiled-frame stack maps against canonical publication;
- checking pending-exception, ownership, and root-discovery contracts at native
  and interpreter boundaries;
- retaining performance benchmarks whose hypotheses affect architecture.

These mechanisms should be introduced where they protect real implementation
work, not all constructed speculatively before the corresponding subsystem
exists.

## Failure Modes of High-Throughput Development

Expanded implementation reach creates characteristic risks:

**Plausibility cascades.** A mistaken premise can quickly produce a coherent
design, implementation, tests, and documentation. Important premises must be
made explicit and tested independently.

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

**Tool confidence replacing evidence.** Fluent explanations and clean diffs do
not establish correctness or performance. Builds, tests, sanitizers, verifiers,
and measurements remain authoritative.

## Working Principles

- Externalize architectural memory.
- Make commitment levels explicit.
- Keep a narrow, well-verified architectural core.
- Prefer deterministic rejection over relying on careful generation.
- Maintain independent correctness oracles.
- Use focused tests for diagnosis and generative tests for discovery.
- Preserve and minimize randomized failures.
- Convert recurring mistakes into structural constraints.
- Keep changes small enough to review causally.
- Measure hinge assumptions before building around them.
- Let sophisticated mechanisms remain optional until evidence justifies them.
- Treat implementation friction as feedback on the design, not something to
  route around automatically.
- Revisit these principles as the project reveals where human-AI collaboration
  actually succeeds or fails.

The objective is not maximal output. It is to expand the scale of software that
a very small team can understand, validate, and sustain.
