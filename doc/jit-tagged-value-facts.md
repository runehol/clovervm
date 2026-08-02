# JIT Tagged-Value Facts and Guards

| Field | Value |
|---|---|
| Document type | Design and implementation plan |
| Status | Accepted design |
| Scope | Tagged-value fact propagation, cheaply encodable tag guards, redundant-guard elimination, and the pointer fact used by shape guards |
| Related documents | [JIT Compiler and IR](jit-compiler-and-ir.md), [JIT Instruction Representation](jit-instruction-representation.md), and [JIT IR Graph Rewrites](jit-ir-graph-rewrites.md) |

## Purpose

Core IR needs to guard arbitrary Python values before operations that inspect
their inline encoding. In particular, `JumpIfTrue` and `JumpIfFalse` may use
the inline truthiness mask only after proving that their condition is not a
pointer. Comparison results are already known booleans, so inserting and then
retaining an inline-value guard between a comparison and its branch would also
break the AArch64 comparison-and-branch fusion pattern.

The optimizer therefore propagates facts about the five low tag bits and
removes guards whose tests are already proved. The analysis representation is
an arbitrary set of possible tags. The instruction representation remains
restricted to tests with cheap direct machine-code lowering.

These are deliberately different types. An arbitrary analysis fact must not
become an instruction merely because it fits in one word.

## Tag Universe

A tagged `Value` has five low tag bits, so all possible tag values fit in one
32-bit bitset. Bit `N` in a tag set means that tag value `N` remains possible.

The analysis universe contains the valid CloverVM tags:

```text
0x00  SMI
0x01  None
0x02  not-present VM sentinel
0x03  exception VM sentinel
0x04  Boolean
0x05  NotImplemented
0x06  Ellipsis
0x08  interned pointer
0x10  refcounted pointer
```

`True` and `False` share tag `0x04`; their value bit is not part of the tag
class. The compiler may rely on the `Value` invariant that other low-tag
patterns do not occur. Adding another valid tag requires updating the tag
universe.

## Arbitrary Analysis Sets

`TaggedValueSet` is the lattice used by fact propagation:

```cpp
class TaggedValueSet
{
public:
    static constexpr TaggedValueSet unknown();
    static constexpr TaggedValueSet impossible();
    static constexpr TaggedValueSet singleton(uint8_t tag);

    constexpr bool is_impossible() const;
    constexpr bool is_subset_of(TaggedValueSet other) const;
    constexpr bool is_disjoint_from(TaggedValueSet other) const;

    constexpr TaggedValueSet intersect(TaggedValueSet other) const;
    constexpr TaggedValueSet merge(TaggedValueSet other) const;

    constexpr uint32_t bits() const;

private:
    friend class TaggedValueTest;

    explicit constexpr TaggedValueSet(uint32_t bits);

    uint32_t bits_;
};
```

The operations are ordinary bitset operations:

```text
refinement on one path:       lhs & rhs
merge of alternative paths:  lhs | rhs
proved membership:            facts is a subset of accepted tags
proved rejection:             facts and accepted tags are disjoint
impossible or unreachable:    empty set
unknown:                      every valid tag
```

This representation preserves facts that known-zero and known-one masks
cannot. Interned and refcounted pointers merge to the set `{0x08, 0x10}` even
though neither pointer-tag bit is individually known to be one.

## Cheap Instruction Tests

IR guards do not accept `TaggedValueSet`. They accept a `TaggedValueTest`, which
can be constructed only in one of the supported cheap forms:

```cpp
enum class TaggedValueTestKind : uint8_t
{
    MaskedEqual,
    MaskedNonZero,
};

class TaggedValueTest
{
public:
    static constexpr TaggedValueTest
    masked_equal(uint8_t mask, uint8_t expected);

    static constexpr TaggedValueTest masked_nonzero(uint8_t mask);

    constexpr TaggedValueTestKind kind() const;
    constexpr uint8_t mask() const;
    constexpr uint8_t expected() const;
    constexpr uint32_t encoded() const;

    constexpr TaggedValueSet accepted_tags() const;

private:
    explicit constexpr TaggedValueTest(uint32_t encoded);

    uint32_t encoded_;
};
```

The packed representation occupies one instruction word:

```text
bits  0..7   mask
bits  8..15  expected bits
bits 16..23  TaggedValueTestKind
bits 24..31  reserved and zero
```

`masked_equal` requires `expected & ~mask == 0`. `masked_nonzero` has an
implicit expected value of zero. Masks and expected values are currently
restricted to `value_tag_mask`, although the byte-sized fields leave room for
a wider tag encoding.

The named tests are:

```text
SMI          MaskedEqual(value_tag_mask, 0)
Boolean      MaskedEqual(value_tag_mask, value_boolean_tag)
SMIOrBoolean MaskedEqual(value_not_smi_or_boolean_mask, 0)
AnyInline    MaskedEqual(value_ptr_mask, 0)
Pointer      MaskedNonZero(value_ptr_mask)
```

`Pointer` is the common `Value::is_ptr()` predicate. The name describes the
tagged-value property and must not be confused with the JIT's untagged
`ValueRepresentation::Pointer`.

Construction through these factories is the validity check. An instruction
constructor takes an already valid `TaggedValueTest`; it does not perform
expensive semantic validation and cannot be passed an arbitrary tag set.

## Test-to-Set Conversion

Fact propagation needs only one conversion direction:

```cpp
constexpr TaggedValueSet TaggedValueTest::accepted_tags() const
{
    uint32_t accepted = 0;
    for(uint8_t tag: valid_tag_values)
    {
        bool matches;
        switch(kind())
        {
            case TaggedValueTestKind::MaskedEqual:
                matches = (tag & mask()) == expected();
                break;
            case TaggedValueTestKind::MaskedNonZero:
                matches = (tag & mask()) != 0;
                break;
        }
        if(matches)
        {
            accepted |= uint32_t{1} << tag;
        }
    }
    return TaggedValueSet(accepted);
}
```

There is no initial conversion from an arbitrary `TaggedValueSet` to a guard
test. The optimizer removes proved tests but does not narrow or synthesize new
ones. If a later optimization demonstrates a need for test synthesis, it can
add an explicitly costed selector without changing the fact lattice.

## Tagged-Value Facts

The first analysis fact is the possible tag set:

```cpp
class TaggedValueFacts
{
public:
    TaggedValueSet possible_tags() const;

private:
    TaggedValueSet possible_tags_ = TaggedValueSet::unknown();
};
```

Instruction results establish facts as follows:

- constants establish their exact low tag;
- SMI arithmetic and logical operations produce the SMI set;
- identity and ordinary comparison operations produce the Boolean set;
- a successful tagged-value guard intersects its input facts with the guard's
  accepted tag set;
- forwarding definitions and semantic moves propagate their source facts;
- unknown parameters and unknown Python results begin with the full valid tag
  universe.

Non-entry block parameters begin at the empty set while the fixed-point
analysis discovers incoming paths. Their fact is the union of the facts for the
corresponding argument on every reachable predecessor edge. Entry parameters
begin unknown. Loop facts monotonically widen until stable.

Tag facts describe immutable bits of an SSA value. Calls, aliasing, and object
mutation do not invalidate them.

## Shape Facts

Exact shape facts are more fragile than tag facts because aliases may mutate an
object's shape without changing the tagged pointer value. The analysis may
eventually carry an optional exact shape, but only when provenance and effects
make its validity explicit:

```cpp
Shape *exact_shape = nullptr;
```

The initial shape policy is conservative:

- a successful shape guard establishes the `Pointer` tag fact;
- it does not by itself attach a persistent exact-shape fact;
- a fresh or otherwise non-aliased producer may establish an exact shape when
  its instruction contract guarantees one;
- merges retain an exact shape only when every reachable input has the same
  exact shape;
- possible aliasing mutation clears the exact shape but leaves the pointer tag
  fact intact.

The pointer check performed as part of shape guarding must become independently
eliminable. The precise lowering shape -- a separate `Pointer` tag guard before
an assuming-pointer shape check, or an equivalent explicit Core rewrite -- is
settled when shape-guard lowering is implemented. It must not be hidden in
target emission where Core fact propagation cannot remove it.

Likely shapes are not facts. Profile or inline-cache predictions may justify
inserting a shape guard, and the successful guard may then establish exact
knowledge with an effect-bounded lifetime. Predictions remain outside this
lattice and can never justify guard removal.

## Redundant Guard Elimination

A tagged-value guard is redundant when its input facts are a subset of the
test's accepted tags:

```cpp
if(input_facts.possible_tags().is_subset_of(
       guard.test().accepted_tags()))
{
    return RewriteResult::replace_with_def(guard.value());
}
```

If the sets are disjoint, the guard is known always to exit. The first pass
leaves such a guard intact; replacing the remainder of the block with an exit
is a separate control-flow optimization.

The pass runs before dead-code elimination. Removing a guard also removes the
guard's use of its Snapshot, after which dead-code elimination removes the dead
Snapshot and restores producer/consumer adjacency.

The motivating conditional sequence is:

```text
comparison
snapshot for conditional bytecode
guard AnyInline(comparison result)
conditional branch(guard result)
```

Comparison facts prove `Boolean`, and `Boolean` is a subset of the tags
accepted by `AnyInline`. Guard elimination rewrites the branch condition to the
comparison result. Dead-code elimination removes the Snapshot, leaving the
comparison immediately before the branch so backend fusion still applies.

## Machine Lowering

`MaskedEqual(mask, 0)` lowers naturally to a test and an equal condition.
`MaskedNonZero(mask)` lowers to the same test and a not-equal condition. A
nonzero expected value uses a masked temporary followed by comparison.

On AArch64, `AnyInline` and `Pointer` are both one flag-setting instruction
plus the conditional side-exit branch:

```asm
tst value, #value_ptr_mask
b.ne side_exit    // AnyInline failed
```

```asm
tst value, #value_ptr_mask
b.eq side_exit    // Pointer failed
```

The low-byte mask and expected fields also permit compact x86-64 tag tests.
Target emitters select their own instruction sequence from the semantic test;
the IR does not encode a target opcode.

Adjacent guard combination is initially restricted to compatible
`MaskedEqual` tests. The existing OR-based combination does not prove that two
independent `MaskedNonZero` tests both succeeded.

## Implementation Order

1. Add `TaggedValueSet` and `TaggedValueTest`, including named tests and
   constexpr test-to-set conversion.
2. Replace `InlineValueClass` instruction attributes with `TaggedValueTest` and
   update printing, side-exit lowering, and AArch64 emission without changing
   behavior.
3. Add fixed-point tag-fact propagation and redundant tagged-value-guard
   elimination before dead-code elimination.
4. Guard `JumpIfTrue` and `JumpIfFalse` with `AnyInline`, passing the guarded
   result to `ConditionalBranch`.
5. Make the pointer portion of shape guarding explicit and eliminable using the
   `Pointer` test when shape-guard lowering is implemented.

The initial implementation does not synthesize narrowed guards, optimize
always-failing guards, retain likely shapes, or preserve exact shapes across
unknown aliasing effects.
