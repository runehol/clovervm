---
name: clovervm-parallel-intent-review
description: Review the concrete intent and implementation boundary of a bounded CloverVM parallel task before coding; use the existing readiness skill for deep single-thread design.
---

# Parallel Intent Review

Make the proposed change cheap for a busy maintainer to accept, redirect, defer,
or reject. This is a compact picture of the intended implementation, not an
exhaustive plan or a claim that all dependencies can be predicted in advance.

## Investigate enough to show the real change

Read current code, applicable instructions, and existing patterns. For Python
semantics use matching CPython sources and bounded probes, with the stdlib skill
where relevant. Test the awkward assumption that could invalidate the proposed
boundary. Some dependencies emerge only during implementation; do not duplicate
all specialist discovery in the coordinator or promise dependency completeness.
Temporary scaffolding, instrumentation, and throwaway prototypes are useful during
investigation, subject to the task's explicit permissions. Keep them identifiable
and separate from production changes; report any remaining repository edits.
Their existence or passing tests does not authorize retaining a production fix.
Preserve unrelated work when cleaning up your own exploratory changes. Before
final review, account for exploratory edits: remove your disposable scaffolding,
preserve useful evidence separately, or explicitly propose retaining it as an
intended production/test change within the agreed scope. Do not silently include
diagnostic edits in the deliverable.

## Show the decision before the detail

Normally use a few short paragraphs, with a tiny code sketch when it communicates
more than prose. Lead with the intended behavior and the consequential change
surface. Show:

- the public API or before/after behavior;
- the owning modules/types and main call path, especially any native/managed split;
- the important representation, ownership, fallibility or cache decision;
- known prerequisites, deliberate compatibility limits, and the verification boundary.

Name actual mechanisms. Say “adds an opcode and changes generic loop acquisition”
instead of obscuring that crossing as “uses a shared helper.” Do not bury the
boundary in a long list of semantics or tests. No mandatory full report template,
exhaustive file inventory, or ceremonial questions when the sketch is clear.

## Core changes require a separate, explicit gate

Parallel implementation does not include core bytecode operations, interpreter
execution machinery, or core code generation changes. New/changed opcodes and
changes to loop acquisition/execution are examples of this boundary. Apply the
gate to the behavior affected, including existing-opcode helpers, continuation
handling and lowering; moving code into a helper does not avoid the gate.
Reading, tests and authorized diagnostic scaffolding in these areas are permitted.

If the task requires crossing it, lead with that fact, state why and what remains
possible without it, and stop before implementation. The user may explicitly
approve the concrete core change. General launch approval or agreement to an
ordinary task sketch does not supply that exception. Do not infer permission
from the fact that such changes appeared among other technical details. Approval
of a proposal that prominently makes the concrete core change its subject is
sufficient; do not demand another ceremonial approval after that decision.

Also flag material object-model, layout, descriptor or dispatch-policy changes
under project instructions; a local helper is not a reason to hide them.

Every task launched with an intent-review gate waits for the human's explicit
agreement before implementation. A simple task gets a shorter sketch, not an
exemption. Honor explicit existing authorization covering the concrete boundary;
routine choices within that boundary need no repeated gate.
If a material assumption fails during coding, stop with evidence and a revised
boundary. Do not silently expand the task or fabricate a missing VM protocol.

## When a dependency ends this attempt

For wave-based work, a blocked chat can finish discovery successfully while its
goal remains unfinished. Give the user/coordinator a short restart brief:

- original goal, acceptance criteria, and the still-useful implementation idea;
- relevant human decisions, accepted compatibility limits, exclusions and rejected
  approaches, linked to their source and distinguished from agent suggestions;
- blocker, minimal reproducer, expected/actual results and exact revisions/configuration;
- what prerequisite would unblock it, and what remains uncertain;
- links to the old chat, evidence and any preserved draft, plus working-tree state.

Report only through authorized communication routes; if messaging the coordinator
is not authorized, leave the brief in this chat for retrieval. Do not create a
prerequisite chat or close/archive anything on your own. Never discard partial
work to make a blocked chat look complete. Keep essential evidence retrievable
through the next wave; a temporary-file path alone is not a durable handoff.

A future attempt starts from current main with this brief and access to the old
reasoning. Redo the compact intent review: the previous sketch and its approval
may be stale after prerequisites change the code. Carry forward standing human
constraints and accepted decisions unless superseded; reassess technical
suitability rather than reopening settled choices without new evidence.
