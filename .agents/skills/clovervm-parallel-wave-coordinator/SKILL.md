---
name: clovervm-parallel-wave-coordinator
description: Coordinate human-directed waves of bounded CloverVM chats and worktrees, including discovered prerequisites and later fresh attempts; not an autonomous architectural implementation workflow.
---

# Parallel Wave Coordinator

The coordinator owns the unfinished work; each implementation chat owns a bounded
attempt. A chat finishing or producing a PR does not mean the original goal is
complete. Keep the human's attention on meaningful engineering choices rather
than dependency bookkeeping.

## Keep the backlog in the conversation

Maintain a short explicit list in the coordinator chat: goal, current attempt/chat,
state, and any “waiting on” prerequisite or decision. Link PRs/evidence when useful.
Use a compact table only when it helps. Do not introduce a tracker, persistent
queue file, polling automation or agent hierarchy unless requested. Summarize this
list at wave transitions and handoffs so it survives context changes. Update it
on authorized coordinator turns; ownership does not imply background monitoring
or the ability to act between turns.

Useful states distinguish discovery, awaiting human agreement, implementation,
review/CI, prerequisite blocked, parked/rejected, and landed. Do not force a state
machine or expand the list into routine progress telemetry.

## Form and launch a wave

Choose bounded tasks with established implementation homes and useful reference
behavior. Screen known dependencies and file/subsystem overlaps, without pretending
to discover every dependency before specialist investigation. Avoid concurrent
writers to the same module where a single slice is more coherent.

Create chats only when requested, and message existing chats only with human
authorization. Use the desktop thread tools, not hidden subagents, when the user
requests separate chats. Every new parallel task gets its own worktree from a
verified base, including readiness/research chats and prerequisites launched
from worker chats. The human need not repeat “with a worktree”: this standing
policy applies to those launches too. Use the worktree launch option rather
than the saved project's local checkout, and verify the resulting chat directory
before repository edits. A branch in the primary checkout is not isolation.
Identify the resulting chats and distinguish queued setup from running work.
Do not create duplicate chats for slow setup.

When an existing research chat moves into implementation, first create or attach
its own task worktree and switch the chat to it. Safely transfer any task-owned
edits and preserve unrelated changes in the original checkout. Verify the new
base and working directory before coding. Approval to implement does not waive
worktree isolation; do not wait for the human to notice or request the move.

Each launch must state the goal, concrete exclusions, review gate and publication
permissions and intended end state: readiness only, reviewed uncommitted changes,
or reviewed PR delivery. Carry any existing standing delivery authorization into
the launch faithfully; do not invent it or make the user repeat it. Require
`clovervm-parallel-intent-review`, then explicit human
agreement before implementation, even for a simple task. Honor existing explicit
authorization for the actual boundary rather than repeatedly requesting it. No
commits/pushes/PRs/merges unless authorized; launch approval alone supplies none
of those permissions.

Explicitly exclude core bytecode operations, interpreter execution machinery and
core code generation changes. A discovered need for these requires a prominent,
separate proposal and explicit human approval before implementation. The human
can approve that exception; ordinary sketch agreement is not an implicit waiver.
Apply this to behavior affected, including existing-opcode helpers, continuation
handling and lowering. A prominently presented core-change proposal can receive
approval directly; no extra ceremonial confirmation is needed afterward. Never
let a prerequisite repair inherit approval from the feature it blocks.

Use the stdlib implementation and builtin-dunder skills where relevant. Keep
architectural direction with the human. Follow AGENTS.md review routing and
ensure each worktree can access the selected skills. Uncommitted skill changes
in the coordinator worktree are not automatically available to new chats. Do not
alter project instructions or install skills without authorization.
Do not adopt a large autonomous planning, subagent or review framework just to
execute bounded slices.

## Discover dependencies during the work

The investigating chat has the code context. Retrieve its concrete blocker and
restart brief rather than redo the investigation centrally or demand foresight.
Distinguish a true prerequisite, optional improvement, accepted compatibility
limitation and unrelated bug. Ask for a scope decision only where needed.

When the human chooses prerequisite-first wave scheduling:

1. Record the blocked goal and link its restart brief and original chat, retaining
   human decisions, acceptance criteria, exclusions and necessary evidence. Ensure
   essential evidence will remain available beyond temporary-file cleanup.
2. Launch the authorized prerequisite attempt(s), each in its own worktree with
   its own boundary/gate, including when the launch is delegated to a worker chat.
3. Close the dependent attempt logically as “blocked discovery completed”; retain
   the unfinished goal in the backlog. Archive the chat/worktree only if separately
   authorized and needed work is safely preserved. Do not confuse chat completion
   with deletion or disposal of uncommitted files.
4. Verify prerequisite landing on the actual target branch, not merely an open PR,
   green CI, or a merge label. Record which dependent goals are now candidates.
5. In a later authorized wave, launch a fresh attempt on current main. Include the
   original goal, brief, evidence, prerequisite revision and link to the old chat.
   Require a fresh intent review before implementation; do not transfer stale
   implementation approval automatically. Preserve standing human decisions and
   accepted limitations unless superseded or materially challenged by new evidence.

Do not relaunch a parked/rejected goal just because a dependency landed. Report
that it is restartable and leave scheduling to the user's authorization.

## Share the machine, not just the repository

Use isolated worktrees for parallel changes. Establish resource settings per host
and apply them consistently to chats sharing that host. On the current shared
macOS desktop, the established defaults are exclusive `fcntl.flock` on
`/private/tmp/clovervm-next-wave-build.lock` around substantial builds/full checks,
and isolated existing per-process TMPDIRs. While holding the exclusive lock,
let Ninja use its default parallelism: omit `-j` and remove explicit parallelism
overrides from build wrappers. Keep performance timing exclusive and free of
competing builds/tests. Adapt paths to the actual host and user instructions.
Preserve ccache; follow repository escalation guidance on sandbox errors. Reuse
a verified suitable binary for bounded discovery when useful.
Do not repeatedly rebuild merely for inventory or launch more work than the
machine and human can usefully handle.

Use compact read-only status snapshots and bounded reads. Report changes,
blockers, decisions and completion; do not fill the conversation with unchanged
lock waits or polling. Respect requests not to message chats. Set up follow-ups
only if requested, rather than assuming background monitoring permission.

## Review and report outcomes precisely

Require implementation chats to apply `clovervm-correctness-review` after code and
verification, fix and recheck in-scope findings unless instructed to stop, and
report residual risks. Make this part of the launch so the user need not request
the local review separately. Require its inventory to include intended staged,
unstaged and untracked files, with exploratory edits accounted for. It is distinct
from GitHub automated review and CI. Commands in referenced skills still obey the
wave's host resource controls.

Default to a human delivery gate after the reviewed implementation: show the
result and verification, then suggest the next prompt to commit, run exact-HEAD
checks, push and create the PR. Do not treat that suggestion as authorization or
send it to another chat on the user's behalf without permission.

Where the user has explicitly authorized reviewed PR delivery, the chat should continue
through commit, required exact-HEAD checks, push, and PR creation once local review
and required checks pass, without another publication prompt. Otherwise stop at
the authorized end state. A readiness approval does not silently expand a
no-commit instruction. Merging remains separately governed by user authorization.

Before publication/landing, follow the current repository checks and actual user
authorization. Verify exact head, CI/review findings and target branch where the
requested action depends on them. A clean diff, local green tests, completed
review, green CI and merged ancestry are different claims. If integration changes
the reviewed content, require review of the affected differences and relevant
checks before delivery. Material boundary changes return to the human; commit
hash changes alone do not create a new approval gate.

Name tested configurations and important skips, especially JIT ON versus OFF.
Do not describe a standard release pass with tiering disabled as full JIT coverage.
Preserve known failures and evidence; do not dismiss them as baseline without
reproduction. Record scope rejections and cheap discovery stops as valid outcomes,
not failures that must be overcome by expanding implementation.
