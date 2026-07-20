# JIT Machine-Code Emission

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Not started |
| Scope | Target instruction encoding, macro assembly, code fragments, labels, branch sizing, and final machine-code layout |
| Owning layers | The target backend chooses machine operations and block layout; the machine-code emitter owns exact encoding, code fragments, label resolution, branch-form selection, and final copying |
| Validated against | N/A |
| Supersedes | N/A |

This document defines how a target backend turns its final program-order
instruction stream into machine code. It complements
[JIT Compiler and IR](jit-compiler-and-ir.md) and deliberately does not add a
mandatory Machine IR. Ordinary instructions are encoded immediately. The
emitter retains only the structure required to resolve labels and choose among
distance-dependent branch encodings.

The first target is AArch64. The same retained layout structure supports a
later x86-64 backend, including its short and near conditional jumps.

## Layering and assembler interfaces

Each target provides two instruction-emission interfaces with different
contracts.

The direct assembler represents instructions the processor can encode
directly. Its operations accept only operands valid for that exact instruction
form. Target-specific immediate classes or checked constructors should make
encoding restrictions explicit, rather than routing unrelated immediate forms
through one generic integer abstraction. Examples include AArch64 add/subtract,
logical, move-wide, load/store-offset, and PC-relative immediates.

The macro assembler represents convenient target operations that may select or
emit several real instructions. For example, loading an arbitrary AArch64
constant may select a logical immediate, `MOVZ`/`MOVN` plus `MOVK` instructions,
or eventually a literal load. Macro operations must make any scratch-register
requirements and possible expansion visible to their callers.

The direct assembler never silently expands an exact instruction. The macro
assembler owns such expansion and selection. This preserves a useful boundary
between knowing how one instruction is encoded and choosing an instruction
sequence.

## Program-order emission and `CodeFragment`

The target backend emits instructions in final program order. Directly
encodable instructions are appended immediately to a byte buffer in the
current `CodeFragment`. A distance-dependent direct branch may terminate the
fragment and remain symbolic until final layout:

```text
CodeFragment
    directly encoded instruction bytes
    optional deferred direct branch
```

A deferred branch is stored at the end of the fragment rather than as a
separate fragment. A fragment need not end in a branch. A branch target begins
a fragment, and emitting a deferred branch starts a new fragment for its
fall-through instruction stream. Consecutive labels may name the same fragment
boundary without creating redundant empty fragments.

`CodeFragment` is intentionally not called a basic block. The JIT CFG excludes
non-returning side exits from its block edges, while the emitter must retain
conditional branches to those exits. One compiler basic block can therefore
produce several code fragments:

```text
compiler basic block
    CodeFragment: instructions; guarded side-exit branch
    CodeFragment: instructions; guarded side-exit branch
    CodeFragment: instructions; block-edge branch or no branch
```

Code fragments describe only linear machine-code layout. They do not
participate in SSA, dominance, loops, block parameters, or recovery semantics.

The backend layer above the emitter chooses compiler-block order, chooses the
fall-through successor, inverts block conditions when useful, resolves
parallel edge moves, and removes an unconditional branch whose target is the
physical fall-through block. The emitter sees only the resulting program-order
instruction stream and does not reconstruct CFG intent.

## Labels and code positions

An unresolved label names a code-fragment boundary, not a provisional absolute
byte offset. Binding a label therefore closes a nonempty current fragment when
necessary and associates the label with the next boundary.

Machine-code metadata must also survive branch shortening. Safepoints,
snapshots, source positions, relocations, and other positions emitted before
final layout should be represented by a fragment identity plus an offset within
that fragment. Finalization translates these positions using the fragment's
final absolute offset.

No instruction or metadata consumer may retain an initial absolute offset
across final layout.

## Conservative single-pass branch shortening

Every deferred branch has a pessimistic long form and, where the target ISA
provides one, a shorter form. Initial layout assigns every deferred branch its
long-form size. The emitter then tests all short forms using those pessimistic
source and target offsets and commits each branch independently to short or
long form.

After selection, the emitter computes final fragment offsets once and encodes
and copies the fragments into the final code allocation. Branch shortening is
not iterated.

This is correct because shortening fragments between a branch and its target
can only reduce the magnitude of their displacement. Any branch that fits in
the pessimistic all-long layout therefore still fits in the final layout. A
branch that did not initially fit might have fitted after other branches were
shortened; it remains long. The design deliberately accepts this small loss in
code density in exchange for a simple and deterministic layout algorithm.

Final encoding must assert that every branch selected as short still fits its
actual final displacement.

The finalization sequence is:

1. assign pessimistic offsets with every deferred branch long;
2. select each short form that fits those offsets;
3. assign final offsets using the selected sizes;
4. allocate the final code buffer;
5. copy encoded instruction bytes and encode trailing branches;
6. resolve fragment-relative metadata and remaining relocations;
7. perform the target and platform's instruction-cache synchronization before
   publishing executable code.

## AArch64 conditional branches

AArch64's fixed instruction width still has several materially different
PC-relative branch ranges. In particular, `TBZ` and `TBNZ` use a signed 14-bit
instruction-scaled displacement and reach only approximately 32 KiB in either
direction. These instructions are attractive for frequent tagged-value type
guards, so the emitter should preserve their one-instruction form whenever the
side-exit target is in range.

The short form of a bit-test guard is the natural instruction:

```asm
    tbnz value, #tag_bit, side_exit
```

Its long form inverts the test over an unconditional branch:

```asm
    tbz  value, #tag_bit, .continue
    b    side_exit
.continue:
```

This expansion preserves condition flags, unlike replacing the bit-test branch
with a flag-setting test. The same pattern applies to `TBZ`, `CBZ`, `CBNZ`, and
`B.cond`: invert the narrow conditional branch over an unconditional `B`.

The long sequence assumes its `B` target is within the AArch64 unconditional
branch range. Calls, cross-allocation targets, and exceptionally large code
regions may require nearby veneers. Veneer and literal-pool placement remain
separate target-emitter policies; they must not change the compiler CFG or turn
side exits into compiler basic blocks.

## x86-64 reuse

The same code-fragment algorithm supports variable-length x86-64 branches
without an x86-specific relaxation framework. A conditional branch uses its
near `rel32` form in pessimistic layout and selects `rel8` when that form fits.
An unconditional jump can similarly choose between near and short forms when
the backend has not already removed it as a fall-through transfer.

As on AArch64, selection is non-iterative. Some x86 branches that would fit
only after other branches shrink remain in their near form. This is an
intentional policy, not a correctness limitation.

## Verification

Emitter tests should cover:

- every exact instruction and immediate-field encoder at its range boundaries;
- rejection of values that an exact instruction cannot encode;
- macro-assembler selection among immediate materialization sequences;
- forward and backward labels, consecutive labels, and branchless fragments;
- short-form branches exactly at both displacement limits;
- long-form selection immediately outside each short range;
- multiple intervening branches shrinking without invalidating an already
  selected short branch;
- one compiler basic block producing several fragments through side exits;
- final metadata positions after branches shrink;
- emitted bytes checked through an independent disassembler or reference
  assembler;
- instruction-cache synchronization before generated code is entered.

Tests should also verify the intended conservative case: a branch that fails
the pessimistic fit test remains long even when final shortening would have
made its short form fit.

## Open implementation details

The design does not yet choose:

- concrete C++ representations for immediate fields, labels, fragments, and
  deferred branches;
- the initial AArch64 constant-materialization cost model;
- literal-pool and veneer placement;
- executable-memory allocation and W^X transitions;
- the representation of external-symbol relocations;
- whether direct calls with multiple possible encodings use the same trailing
  deferred-operation mechanism or a separate call policy.

Those choices belong to target-emitter implementation work and must preserve
the direct-assembler/macro-assembler split and the code-fragment layout
invariants above.
