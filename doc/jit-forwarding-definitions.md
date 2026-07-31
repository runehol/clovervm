# JIT Forwarding Definitions

| Field | Value |
|---|---|
| Document type | Design revision and transition plan |
| Status | Accepted |
| Implementation | Slices 1 and 2 complete: result-definition metadata and dormant allocator vocabulary are implemented, allocation verification derives semantic value membership from result occurrences, and location publication uses result anchors; no instruction kind is marked forwarding yet |
| Scope | Representing block-local SSA definitions that preserve operand 0's runtime bits without creating a second allocator live range |
| Owning layers | Instruction kinds declare forwarding definitions; allocator preparation forms shared live ranges; target constraints describe instruction execution requirements; register allocation chooses locations and transfers |
| Design authority after integration | [JIT Compiler and IR](jit-compiler-and-ir.md) and [JIT Register Allocation](jit-register-allocation.md) |
| Supersedes | Same-as-input bundle affinity for refining guard results |

## Summary

Some instructions introduce a new SSA result without producing new runtime
bits. A refining guard returns a narrowed value after proving a property of its
input. An effectful operation such as the planned `SetExisting` returns its
receiver after performing the effect. The new result remains a distinct SSA
definition because it carries refined semantic information or orders later uses
after the instruction, but on normal continuation its runtime bits are exactly
operand 0's bits.

Such a result is a **forwarding definition**.

Forwarding definitions do not create a second allocator live range and then ask
the allocator to coalesce it with the input. Allocator preparation maps operand
0 and the forwarding result to one block-local live range. The live range has
one materializing origin and may have several later forwarding definitions.

This is distinct from `SameAsInput`. `SameAsInput` is a target location
constraint for a result whose bits may differ from its input, such as a
destructive two-address operation. A forwarding definition is an
instruction-kind semantic fact that the bits do not change.

## Semantic Contract

An instruction kind with a forwarding definition obeys all of the following:

1. It has one `ProgramValue` result.
2. Operand 0 is a fixed `ProgramValue` operand.
3. Operand 0 and the result have the same `ValueRepresentation`.
4. On every normal continuation, the result's runtime bits are exactly operand
   0's runtime bits.
5. The result is nevertheless a new SSA definition and is unavailable to
   semantic uses before the instruction.
6. The instruction need not be pure or removable. It may guard, side exit,
   mutate state, or otherwise carry effects.
7. The instruction does not physically write a separate result.

The contract applies only to normal continuation. A guard may side exit instead
of producing its forwarding result. An operation may fail or terminate through
another declared control-flow path. Those paths do not weaken the identity of
the result on the path where the result exists.

Operand 0 is the forwarding source by convention. Instruction kinds should put
the forwarded value in that position. The metadata does not carry an operand
index, and the allocator does not support an instruction-specific forwarding
source.

The initial forwarding instruction kinds are expected to include refining
guards and checks whose successful result is their checked value:

```text
ShapeGuard
ValidityCellGuard
InlineTagGuard
InlineTagGuardWithSideExit
CheckNotImplemented
```

The planned `SetExisting` instruction will use the same contract for its
receiver result. Each Core and Machine instruction kind declares the property
independently. Lowering preserves forwarding behavior by selecting a Machine
instruction kind that also declares a forwarding definition.

## Allocator Representation

Allocator occurrences distinguish materializing and forwarding definitions:

```cpp
enum class OccurrenceKind : uint8_t
{
    Use,
    Def,
    ForwardingDef,
    Temporary,
};
```

`Def` means that the instruction physically creates the represented bits.
`ForwardingDef` means that the instruction introduces a new SSA identity for
bits already supplied by operand 0's live range.

`LiveRangeOrigin` continues to identify the instruction result or temporary
that initially supplies the bits represented by the live range. A ProgramValue
live range may also contain later forwarding definitions. They appear as
result occurrences and do not replace or multiply the origin.

Allocator preparation therefore maintains a many-to-one mapping:

```text
ProgramValue InstructionId -> LiveRangeId
```

For example:

```text
%value   = ...
%checked = InlineTagGuard %value, ...
%updated = SetExisting %checked, ...

value_ranges[%value]   = lr7
value_ranges[%checked] = lr7
value_ranges[%updated] = lr7
```

The scanner processes a forwarding instruction as follows:

1. Scan operand 0 as an ordinary use.
2. Find operand 0's existing block-local live range.
3. Map the result `InstructionId` to that same `LiveRangeId`.
4. Add a Late `ForwardingDef` result occurrence.
5. Extend the live range through that occurrence and later uses.

The operand use and forwarding definition give continuous minimum coverage
through the instruction:

```text
operand 0 Use Early:       [Early, Late)
result ForwardingDef Late: [Late, NextEarly)
```

A chain of forwarding definitions in one block naturally continues one live
range. Uses of any semantic value in the chain refer to that range while
retaining their original SSA `InstructionId` in operand anchors, diagnostics,
Snapshots, and side-exit bindings.

## Block Boundaries

Forwarding live-range identity stops at block boundaries. Block parameters
always begin distinct live ranges, even when an incoming edge argument is a
forwarding result.

Ordinary block-edge affinities may merge the argument and parameter bundles
when their ranges and constraints permit it. They are not made into one source
live range during scanning. This preserves the existing representation of
joins, loop-carried values, and genuinely incompatible incoming ranges.

## Constraints and Transfers

A forwarding definition has no independent target result constraint. In
particular, it does not use `SameAsInput`, request a fixed result register, or
create a result affinity.

The operand constraint describes where the instruction consumes operand 0.
Later result uses describe where the forwarded bits are subsequently needed.
The allocator may insert a transfer at any legal split boundary between those
requirements.

A transfer may prepare the forwarding result's eventual location before the
forwarding instruction. This does not make the SSA result semantically
available early. It only makes an existing copy of the physical bits available
in the location the shared live range will use. Def-use ordering continues to
control semantic availability.

The allocator may instead place a transfer after the forwarding instruction or
at a later use when that better fits pressure and constraints. The forwarding
definition is not itself a mandatory split point.

Explicit target result overrides on a forwarding instruction are invalid.
Target preparation must express physical execution requirements on operand 0,
temporaries, and clobbers. Requirements belonging to later consumers remain on
those consumers.

## Clobbers

Operand 0's bits must survive the complete target expansion of a forwarding
instruction. The shared live range therefore spans from the operand use through
the forwarding definition and interferes with every clobber that can destroy
those bits during the instruction.

This includes a register overwritten only in the middle of a multi-instruction
target sequence. A register is not safe for the forwarding live range merely
because its final architectural contents happen to be reconstructed. Any
temporary destruction that can cross an internal side exit or the logical
result point must be represented as a clobber and must conflict with the live
range.

A lowering may internally restore a temporarily used register only when it
does so before every possible side exit and before the forwarding result
becomes available. In that case preservation is part of the lowering's proven
contract rather than an allocator inference. The target must otherwise declare
the clobber for the instruction as a whole.

If operand and clobber constraints leave no location in which the bits can
survive the instruction, ordinary constraint splitting may resolve the conflict
when legal boundaries separate the requirements. An irreducible conflict uses
the allocator's existing constraint-fixup failure path. The scanner must not
silently create a second forwarding-result range.

## Snapshots and Side Exits

Snapshots and side-exit bindings retain semantic `ProgramValueRef`s, not
allocator live-range identities. A Snapshot may therefore capture the original
value while mainline uses consume a later refined forwarding result.

At allocation time both references resolve through the shared block-local live
range. The location is selected at the consuming occurrence or side-exit owner,
so recovery sees the current physical incarnation of the unchanged bits.
Semantic refinement does not leak into recovery: the Snapshot still names the
original interpreter-visible value.

Location publication and transition materialization must not assume that a
ProgramValue live range's origin instruction is the only semantic definition
mapped to that range. They use the referenced ProgramValue's occurrence and the
live range covering the relevant allocation position.

## Verification

Allocation verification checks:

- instruction-kind metadata declares forwarding behavior;
- the instruction has a ProgramValue result and fixed ProgramValue operand 0;
- operand 0 and the result have the same representation;
- operand 0 and the result map to the same live range;
- the result has one Late `ForwardingDef` occurrence;
- an ordinary result has a `Def` occurrence;
- every mapped ProgramValue definition belongs to the live range's block;
- the forwarding live range covers the instruction's clobber position;
- forwarding identity does not cross a block parameter;
- no forwarding instruction has an independent result override.

The verifier no longer requires every ProgramValue result or operand anchor to
equal `LiveRangeOrigin::program_value()`. It instead verifies that the semantic
definition maps to the referenced live range and that non-origin result
definitions are declared forwarding definitions of operand 0.

## Transition Plan

The migration keeps the current allocator functional after each slice.

### Slice 1: Vocabulary and Metadata

- Add `OccurrenceKind::ForwardingDef`.
- Extend `CL_JIT_RESULT` with a `ResultDefinitionKind` argument:

  ```text
  CL_JIT_RESULT(ProgramValue, TaggedValue, Def)
  CL_JIT_RESULT(ProgramValue, TaggedValue, ForwardingDef)
  CL_JIT_RESULT(None, None, NoDef)
  ```

- Store that definition kind in the compact instruction-kind metadata.
- Do not mark any instruction kind yet.

### Slice 2: Remove Single-Origin Assumptions

- Keep the ProgramValue-to-live-range mapping private to live-range scanning and
  discard it when scanning finishes.
- Reconstruct a temporary definition-to-live-range map from result occurrences
  inside allocation verification, use it to validate operand and edge
  occurrences, and then discard it.
- Publish result locations from result occurrence anchors rather than always
  from the live-range origin.
- Keep transfer source recovery origin-based: the origin remains the
  block-local semantic value that initially supplied the unchanged bits and is
  a legal source for allocator-created moves.
- Let side-exit location lookup continue through ordinary semantic
  `ProgramValueRef` assignments; every result occurrence now publishes its own
  location.

No graph should yet contain a forwarding definition, so this slice changes
representation capability without changing generated code.

### Slice 3: Scan Forwarding Definitions

- Teach live-range scanning to reuse operand 0's live range.
- Add the Late `ForwardingDef` occurrence and result mapping.
- Require matching representations and block ownership.
- Reject independent result overrides.
- Verify continuous coverage through target clobbers.

Keep instruction declarations disabled until this path is complete.

### Slice 4: Convert Guards

- Mark the Core and Machine refining guard kinds as forwarding definitions.
- Remove AArch64 guard `SameAsInput` result constraints.
- Leave `SameAsInput` available for genuinely destructive result reuse.
- Confirm that guard results introduce no second live ranges, affinities, or
  result moves.

The existing guard-heavy loop and simple guarded addition are the primary
integration fixtures. They should lose guard-result copies while preserving
side-exit recovery values, block-edge transfers, and the final return-ABI
transfer.

### Slice 5: Complete Documentation

- Transfer the accepted model into
  [JIT Compiler and IR](jit-compiler-and-ir.md) and
  [JIT Register Allocation](jit-register-allocation.md).
- Correct the guard-result and same-as-input sections of the implementation
  roadmap and allocation ledger.
- Record the implemented instruction-kind set.
- Delete this transition document once the main design documents are
  authoritative and the migration is complete.

## Non-Goals

This design does not:

- merge block parameters into predecessor live ranges;
- make semantic SSA values interchangeable;
- infer forwarding behavior from instruction names, effects, or emitted code;
- remove or weaken guard and mutation ordering;
- replace `SameAsInput` for destructive two-address operations;
- introduce general multi-location values;
- require transfers to occur exactly at forwarding instructions.
