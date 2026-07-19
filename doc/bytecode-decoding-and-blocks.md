# Bytecode Decoding and Block Structure

| Field | Value |
|---|---|
| Status | Implemented |
| Scope | Shared bytecode metadata, semantic decoding, lightweight blocks, and compilation-time inline-cache snapshots |
| Ownership | The bytecode layer owns decoding and bytecode blocks; each JIT owns its IR CFG, state flow, SSA, and later analysis |

## Purpose

Clover bytecode has one producer and several consumers: the interpreter, the
printer, the instruction tracer, and two JITs. The interpreter keeps its direct,
specialized access to encoded bytes. The other consumers share a structured
decoder without requiring the interpreter to use it.

The shared layer consists of `BytecodeInstruction`, `BytecodeBlock`, and
`BytecodeDecoder`.

## Authoritative Opcode Metadata

`bytecode.def` is the source of truth for each encoded opcode's operand format,
control-flow behavior, compound-operation role, and length.

There is only one instruction length. For a compound operator it includes the
encoded `CheckOperatorNotImplemented` continuation byte. Sequential consumers
therefore always advance by the opcode's authoritative length; they do not
calculate a second semantic length or add a continuation adjustment.

The continuation opcode retains its own one-byte format. This allows standalone
decoding when the instruction tracer observes a PC that already points at the
continuation.

## Semantic Instruction Decoding

Standalone decoding accepts a code object and a code-object-relative PC offset.
It does not snapshot or attach inline-cache state, which keeps it suitable for
the printer and instruction tracer.

Decoded instructions distinguish the physical encoded opcode from the semantic
opcode. Compact forms such as `Ldar0` and `Star0` normalize to `Ldar` and `Star`
while preserving their encoded spelling for diagnostics.

Decoded instructions make accumulator and register sources and destinations
explicit. They preserve the distinction between parameters, locals,
temporaries, and the accumulator. Locals with binding semantics are accessed
through local-aware operations; an undefined temporary use is an internal
invariant violation.

Interpreter PCs are globally unique byte pointers. Decoded instructions instead
use offsets within their particular code object. The tracer converts its
interpreter PC to an offset before decoding.

## Compound Operators

JIT consumers see an operator and its not-implemented continuation as one
semantic instruction. The decoded instruction identifies the operator offset,
the continuation offset, and the next offset after the complete protocol. The
continuation offset is always one byte before the next offset.

Structural scanning never returns the continuation as a separate instruction
when it follows its operator. Standalone decoding directly at the continuation
offset remains valid and produces an ordinary one-byte instruction without a
further continuation.

## Inline-Cache Snapshots

`BytecodeDecoder` is compilation-scoped. Its first action is to copy all inline
cache tables, including variable-length cache storage. Every instruction
produced through that decoder refers to the same snapshot, so compilation sees
a coherent view even if it later becomes stale.

The snapshot uses the same cache structures as the code object rather than a
parallel snapshot hierarchy. Decoded instructions retain a separate optional
index for each cache kind; instructions with two cache kinds retain both.

The current implementation relies on the existing single-threaded cache
mutation policy. Concurrent mutation will require synchronization around the
snapshot, without changing decoder consumers. Snapshot lifetime must also keep
any referenced heap objects valid for the compilation.

## Bytecode Blocks

The decoder performs a lightweight structural scan and returns blocks in
ascending PC-offset order. Instructions are decoded lazily while iterating a
block; the decoder does not retain a fully decoded instruction stream.

Block boundaries are created by:

- function entry;
- explicit jump targets;
- conditional fallthrough;
- encoded instructions following non-fallthrough terminators, so unreachable
  code remains representable;
- exception-handler offsets.

Calls, safepoints, and merely fallible instructions do not split blocks.
Exception-table range starts and ends do not split blocks. Blocks contain no
covering-handler metadata and no exceptional edges.

The decoder explicitly identifies the ordinary entry block, which is block
zero, and each distinct exception-handler block. Handler blocks are reported
once each in PC order.

Malformed structure represents a VM or compiler bug, so the decoder uses
assertions rather than a fallible public API. Code objects must be nonempty, and
jump targets and handler offsets must land on semantic instruction boundaries,
not operands or compound continuations.

## JIT Ownership

Bytecode blocks are a frontend convenience, not the JIT's final CFG. One
bytecode block may expand into several IR blocks for guards, calls, operator
continuations, side exits, or backend requirements.

JITs visit bytecode blocks in PC order and pre-create their target IR entries.
Complete architectural state crosses bytecode block boundaries; it is not
pruned by an initial liveness pass. This makes lowering order independent of
dominance and permits backedge arguments to be attached after their target
parameters already exist.

`ForIter` forms have the same accumulator destination on both outgoing edges.
`ForPrepRange` forms expose their range registers as read-modify-write. The
decoder therefore does not need general edge-specific value effects.

After lowering, the JIT CFG is authoritative. Any dominance, loop analysis,
optimization, edge-move resolution, or block layout is performed over that CFG,
not over `BytecodeBlock`.
