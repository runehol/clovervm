# JIT IR Graph Rewrites

| Field | Value |
|---|---|
| Document type | Design |
| Status | Proposed |
| Implementation | Not started |
| Scope | Forward instruction rewriting within published JIT IR basic blocks |
| Owning layers | The graph rewriter owns traversal, operand substitution, instruction placement, and commit; the instruction schema owns reconstruction; individual passes own matching and semantic legality; CFG editing owns successor and predecessor changes |
| Validated against | N/A |
| Supersedes | If accepted, the incremental mutable-operand rewrite direction in [JIT Instruction Representation](jit-instruction-representation.md) and [JIT Compiler and IR](jit-compiler-and-ir.md) |

JIT IR instructions are immutable. A graph rewrite constructs a replacement
instruction stream rather than editing instruction payloads or rewriting use
slots in place. The rewriter walks a basic block forward, remembers replacements
for definitions already visited, and reconstructs each later instruction with
canonical operands before presenting it to the pass.

This design covers local instruction rewrites, lowering one instruction to an
instruction sequence, erasure, and passes such as dead-code elimination. It
does not cover changes to the CFG topology.

## Core Model

For each original instruction, the rewriter performs:

```text
original instruction
    -> substitute operands using earlier replacements
    -> normalized instruction
    -> invoke the pass callback
    -> emit zero or more instructions
    -> remember the result replacing the original definition
```

Given:

```text
a = Add(x, y)
b = Multiply(a, z)
c = Negate(b)
```

if the callback replaces `a` with `a2`, the callback is subsequently shown
`Multiply(a2, z)`, not the original `b`. If it keeps that normalized multiply,
the rewriter records `b -> b2`, so the callback then sees `Negate(b2)`.

The pass therefore never observes an instruction with an operand that has
already been replaced earlier in the block.

This model relies on the current Core IR rule that an instruction result is
used only later in the same block. Block parameters are definitions available
before the instruction stream and are initially not rewritten by this API.
Cross-block SSA values would require a graph-level renaming design.

## Callback API

The primary API is callback-based:

```cpp
RewriteSummary rewrite_block_forward(
    Block &block,
    InstructionRewriteCallback callback);
```

The callback receives the normalized instruction and a factory for constructing
unplaced instructions:

```cpp
RewriteResult rewrite_instruction(
    const Instruction &instruction,
    InstructionRewriter &rewriter);
```

`InstructionRewriter::make_instruction<T>(...)` follows the existing
construction vocabulary: it allocates an instruction without placing it.
Instruction-result arguments are resolved through replacements already
established by the walk.

The callback does not mutate the block, attach instructions, or modify operand
slots. It returns the complete output for the current position.

## Rewrite Results

A rewrite result contains:

```text
instructions    zero or more instructions emitted at this position
replacement     the canonical result replacing the original definition
```

Convenience constructors express the common cases:

```cpp
RewriteResult::keep();
RewriteResult::erase();
RewriteResult::replace(instruction);
RewriteResult::replace(sequence, replacement);
RewriteResult::replace_with_value(existing_definition);
```

Their meanings are:

| Result | Emitted instructions | Remembered replacement |
|---|---|---|
| `keep()` | The normalized instruction | The normalized instruction, when it has a result |
| `erase()` | None | Erased |
| `replace(new)` | `new` | `new` |
| `replace(sequence, result)` | The sequence in order | `result` |
| `replace_with_value(value)` | None | `value` |

A replacement sequence separates execution from value identity. For example,
lowering one operation to an AArch64 immediate materialization followed by an
add emits both instructions but maps later uses of the old result to the add:

```cpp
return RewriteResult::replace({arm_imm12, add}, add);
```

A one-to-one rewrite such as a pointer-valued `Const` becoming `LoadConst` is:

```cpp
return RewriteResult::replace(load_const);
```

The instructions emitted for one callback are not revisited during the same
walk. Fixed-point rewriting, if required, is a separate driver or another pass.

## Result and Sequence Invariants

The rewriter validates each result before commit:

- emitted instructions are unplaced or are the normalized instruction being
  kept at the current position;
- sequence order satisfies definition-before-use;
- every operand refers to a block parameter, an earlier committed result, or an
  earlier instruction in the same sequence;
- a replacement for a `ProgramValue` has the same `ValueRepresentation`;
- a replacement for a `Snapshot` is another `Snapshot` result;
- an instruction with `ResultClass::None` has no replacement result;
- a result-producing instruction has a compatible replacement unless it is
  explicitly erased;
- no non-final sequence instruction is a block terminator.

The pass owns semantic correctness. Structural acceptance of erasing an unused
call, for example, does not prove that discarding its effects is valid.

## Erasure

Erasure is sequence replacement with an empty sequence. The replacement map
retains an explicit erased state rather than silently dropping the original
definition.

Erasing a value or Snapshot producer is valid when it has no later uses. If a
later instruction refers to an erased definition, operand normalization reports
a compiler error identifying the erased producer and its user. Because the
rewrite is staged, this does not expose a partially rewritten block.

The graph-rewrite mechanism does not itself decide whether an instruction is
dead. A dead-code-elimination pass requests a use index, makes its own effect
and liveness decisions, and returns `erase()` for removable instructions. A
simple post-rewrite DCE pass can remove constants made dead by an earlier
folding or lowering pass.

## Instruction Reconstruction

The instruction schema generates a generic reconstruction operation:

```cpp
Instruction *rebuild_with_operands(
    const Instruction &instruction,
    OperandResolver resolver,
    InstructionRewriter &rewriter);
```

It reconstructs the same concrete instruction kind with resolved typed operands
and unchanged attributes. It returns the original instruction when no operand
changed. Reconstruction is generated from `src/jit/instruction.def`; it does
not mutate raw slots or introduce handwritten cloning switches.

When reconstruction creates a new instruction, keeping it is still a structural
replacement. The rewriter records the original definition as mapping to the
reconstructed definition so the substitution propagates transitively.

## Traversal Direction

Mutating graph rewrites initially walk forward only. Forward order is what
allows every callback to receive already-normalized operands.

A backward mutating walk cannot provide that guarantee: it visits a use before
the callback has decided how to rewrite its producer. Supporting it would
require stale callback inputs or a separate decision and reconstruction phase.

Blocks may expose ordinary read-only forward and reverse traversal for analyses.
A backward rewrite driver is deferred until a concrete pass establishes its
required semantics.

## Staging and Commit

The rewriter builds a new instruction-pointer vector while the original block
list remains unchanged. After the final instruction:

1. it validates the completed output and replacement map;
2. it replaces the block's instruction vector in one commit;
3. it detaches instructions no longer present in the graph;
4. it advances the graph mutation generation and invalidates affected attached
   analyses;
5. debug and test configurations verify the completed graph.

Arena allocations created during the rewrite need no rollback. Allocation
failure abandons the compilation session under the existing JIT failure model.
Ordinary passes cannot observe the staged block.

The callback-based API is the initial public surface. A cursor may later wrap
the same staging engine if a pass needs manual traversal control, but it must
preserve the same normalization and commit rules.

## Terminators and CFG Changes

Instruction rewriting and CFG editing remain separate responsibilities.

Initially:

- a terminator cannot be erased;
- replacing a non-terminator cannot emit a terminator;
- a sequence replacing a terminator ends in exactly one terminator;
- a replacement terminator preserves the original successor edges.

Changing successors, redirecting edges, splitting blocks, and replacing one
operation with a multi-block region require a CFG editor that updates target
predecessor indexes and invalidates CFG analyses. They are not implicit side
effects of `rewrite_block_forward()`.

## Analysis Interaction

The graph does not maintain permanent use lists merely to support rewriting.
Passes request a `UseIndex` when their decisions require use counts or user
enumeration. A completed structural rewrite invalidates attachments by default;
an analysis may later consume the rewrite summary to update itself when that is
worth the complexity.

The rewrite summary records at least whether instructions changed and whether
the terminator changed. Since this API preserves successor edges, ordinary
instruction rewrites preserve CFG topology even when they invalidate
instruction-indexed analyses.

## Related Documents

- [JIT Instruction Representation](jit-instruction-representation.md)
- [JIT Control-Flow Graph](jit-control-flow-graph.md)
- [JIT Compiler and IR](jit-compiler-and-ir.md)
- [JIT Machine-Code Emission](jit-machine-code-emission.md)
