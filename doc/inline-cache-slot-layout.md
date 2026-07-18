# Inline Cache Slot Layout

| Field | Value |
|---|---|
| Document type | Design |
| Status | Speculative |
| Implementation | Not started |
| Scope | Possible compact slot and side-payload layout for inline caches |
| Owning layers | Bytecode metadata, inline caches, interpreter, and future compiler |
| Validated against | N/A |
| Supersedes | N/A |

This note sketches a possible future inline-cache layout for clovervm. It is
not an implementation plan yet. The motivation is that more Python bytecodes
are becoming cache-bearing: attribute reads and writes, global loads and
stores, calls, method calls, operators, subscription, and arithmetic protocols.
When cache-bearing instructions become common, explicit cache-index operands
and many separate cache vectors become less obviously attractive.

The design goal is many cheap cache slots plus preallocated side payloads for
complex cached behavior. A cache hit may still execute a complex Python-visible
operation, such as descriptor `__get__` or a Python dunder call. "Complex" does
not mean uncached; it means the direct opcode hot path should not carry all of
that state inline.

## Motivation

The current cache entries are convenient complete plans, but several are large
on a 64-bit target:

```text
AttributeReadInlineCache        56 bytes
FunctionCallInlineCache         40 bytes
OperatorInlineCache             88 bytes
KeywordCallInlineCache          80 bytes
```

The large entries are mostly real payload:

- `OperatorInlineCache` stores three operand shape keys, two lookup validity
  cells, a trusted handler, and Python-call replay state.
- `KeywordCallInlineCache` embeds a `std::vector<int8_t>` for keyword remapping.
- `AttributeReadInlineCache` embeds a full `AttributeReadPlan`, including
  binding context for descriptor and bound-function cases.

Those shapes are too large for a naive "one union entry per program counter"
table. Before moving to denser cache addressing, the entries should be split
into compact fixed-slot state and larger side payload state.

## Slot Addressing

A cacheable instruction with start program counter `pc` uses:

```text
slot_index = pc / 2
```

Only instructions with encoded length of at least two bytes may have inline
caches. That rule avoids slot collisions without requiring instruction starts
to be aligned. If an instruction starts at `2k` and is cacheable, it occupies
at least bytes `2k` and `2k + 1`, so no instruction can start at `2k + 1`. If an
instruction starts at `2k + 1`, the preceding instruction at `2k` must be
one byte long and therefore cannot be cacheable.

The builder should enforce this invariant:

```text
if instruction has an inline cache:
    instruction_length >= 2
    fixed_cache_slots[pc / 2] is unused
```

The fixed cache table size is approximately:

```text
ceil(code.size() / 2)
```

Some slots are unused because not every two-byte bucket contains a cacheable
instruction. This is acceptable only if each fixed slot is compact.

## Fixed Slots

The fixed slot is the only cache state addressed directly from bytecode. It
should fit common direct-hit cases and contain the side-payload index for
complex cached hits.

An illustrative shape:

```cpp
struct InlineCacheSlot
{
    uintptr_t guard0;
    uintptr_t guard1_or_data;
    uint32_t data;
    uint16_t payload_index;
    uint8_t kind;
    uint8_t state;
};
```

This exact layout is not prescribed. The important properties are:

- the slot is small enough to allocate for roughly half the bytecode size;
- direct monomorphic cases can be validated and executed from the slot alone;
- complex cached cases can find their preallocated payload through
  `payload_index`;
- simple-cache instructions either ignore `payload_index` or store a sentinel;
- no opcode path grows cache vectors or allocates cache storage while executing.

The slot kind/state should distinguish direct states from complex states. For
example, an attribute read might use:

```text
Empty
ReceiverSlot
ConstantValue
DescriptorGetPayload
BindFunctionReceiverPayload
```

The direct states are candidates for inline execution in the opcode handler.
The payload states are still cache hits, but execution should go through a
cold helper or clearly separated cached-complex path.

## Side Payloads

Some instructions can cache behavior that is too large or too cold for the
fixed slot. Those instructions receive one preallocated side payload at code
object build/finalize time.

The rule is static:

```text
simple-cache-capable instruction:
    fixed slot only

complex-cache-capable instruction:
    fixed slot + exactly one side payload
```

The builder knows which emitted opcodes can need complex cache state, so it can
preallocate side payloads without runtime guessing. The fixed slot stores the
payload index. The interpreter never pushes into a side-payload vector during
execution.

This avoids a runtime cache-pool policy. There is no "payload pool exhausted"
case and no execution-time allocation cliff. If an instruction can enter a
complex cached state, its payload exists before execution begins.

## Cached Complex Paths

Complex payloads are not misses. They are prevalidated cache-hit plans that are
too bulky or too semantically involved for the direct opcode hot path.

Examples:

- attribute read hits that must invoke descriptor `__get__`;
- attribute read hits that bind a function receiver;
- operator hits that replay a Python dunder call;
- reflected operator hits that need continuation metadata;
- keyword calls that need a keyword-to-parameter remap;
- function calls that need defaults, varargs, or other adaptation state.

The opcode handler should first validate the compact guards in the fixed slot.
If the state is direct, it can execute inline. If the state is complex, it uses
`payload_index` to load the side payload and enters the cached-complex helper.

Illustrative flow:

```cpp
InlineCacheSlot &slot = code_object->inline_cache_slots[pc_offset / 2];

if(slot.matches_direct(...))
{
    // Execute the compact fast path directly.
}

if(slot.matches_complex(...))
{
    ComplexInlineCachePayload &payload =
        code_object->complex_inline_cache_payloads[slot.payload_index];
    // Execute a cached complex path using the payload.
}

// Generic miss path resolves semantics and repopulates the preallocated slot
// and, when applicable, its preallocated side payload.
```

For `src/interpreter.cpp`, this split matters because opcode handlers are hot
and constrained by the existing `musttail` dispatch shape. Direct states should
remain small and explicit. Complex states should move descriptor calls, Python
calls, message formatting, and other cold behavior out of the frame-free hot
handler body.

## Candidate Classification

The exact classification needs measurement, but the expected split is:

Fixed slot only:

- module/global read or store plans that fit as validity cell plus slot data;
- simple attribute mutation plans if they can be packed compactly;
- simple fixed-arity function calls if the target identity and entry state fit;
- trusted builtin operator handlers if operand guards and handler identity fit.

Fixed slot plus side payload:

- attribute reads, because descriptor and binding cases are common enough to
  cache but too large for every fixed slot;
- method calls, because they combine attribute lookup and call adaptation;
- operators and subscription, because they may use trusted direct handlers or
  cached Python dunder calls;
- keyword calls, because keyword remap data should not live in every fixed
  slot;
- call opcodes that may need defaults, varargs, keyword-only defaults, or
  constructor adaptation state.

This classification should be decided per opcode, not dynamically per first
observed runtime value. Runtime values choose the slot state; the opcode's
static class decides whether a side payload exists.

## Slimming Targets

This addressing model only makes sense after cache entries are reduced.
Promising reductions:

- split `OperatorInlineCache` into direct trusted-handler state and Python-call
  payload state;
- avoid storing `TrustedHandlerArity` inside `TrustedHandler` when it causes
  pointer-alignment padding;
- move keyword remap vectors out of `KeywordCallInlineCache` fixed state;
- make attribute read plans variant-like so receiver-slot reads do not carry
  descriptor binding context;
- pack storage locations, cache state, and small enums into fixed-slot data
  words where this does not obscure the hot path.

The target should be a fixed slot small enough that `ceil(code.size() / 2)`
entries are acceptable. A 16-byte or 24-byte slot is plausible. A 40-byte or
larger slot probably loses too much memory to unused buckets and simple
instructions.

## Invariants

An implementation should make these invariants explicit in the builder and in
tests:

- only instructions of length at least two bytes can have inline caches;
- at most one cacheable instruction maps to a given `pc / 2` slot;
- every complex-cache-capable instruction has exactly one side payload;
- no simple-cache-capable instruction requires a side payload;
- execution never grows cache storage;
- clearing a code object's inline caches resets both fixed slots and side
  payloads to empty states;
- disassembly can show the fixed slot and, when present, the side payload for a
  cache-bearing instruction.

## Open Questions

- What fixed-slot size is acceptable after measuring realistic code objects?
- Should the fixed slot use raw pointers, compressed indices, or tagged words
  for guards and payload data?
- Which current cache families can share one side-payload union, and which
  deserve separate typed vectors?
- Should side payloads be one per complex-capable instruction, or can some
  opcodes prove that a smaller fixed payload is enough?
- How should invalidation and cache clearing be represented so direct and
  complex states share the same validity checks?
- Which opcode handlers are hot enough that cached-complex hits should tail-call
  a helper instead of branching through a larger inline body?
