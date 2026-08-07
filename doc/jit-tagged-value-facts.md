# JIT Tagged-Value Facts and Guards

| Field | Value |
|---|---|
| Document type | Design and implementation plan |
| Status | Accepted design |
| Implementation | Tag sets and classes, fixed-point CFG analysis, redundant inline-tag guard elimination, guarded-state replacement, `AnyInline` truthiness guards, and pointer-check elimination from shape guards are implemented; exact immutable-shape facts are next |
| Scope | Tagged-value fact propagation, cheaply encodable tag guards, redundant-guard elimination, and the pointer fact used by shape guards |
| Owning layers | Core IR analysis and rewriting, with target-specific lowering of retained guards |
| Validated against | `test_jit_tagged_value_facts.cpp`, `test_jit_core_bytecode_translator.cpp`, and `test_aarch64_execution.cpp` |
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
    static constexpr TaggedValueSet never();
    static constexpr TaggedValueSet unknown();

    static constexpr TaggedValueSet pointer();
    static constexpr TaggedValueSet from_inline_tag(uint8_t tag);
    static constexpr TaggedValueSet from_shape_key(ShapeKey key);

    static constexpr TaggedValueSet smi();
    static constexpr TaggedValueSet boolean();
    static constexpr TaggedValueSet smi_or_boolean();

    static constexpr TaggedValueSet from_class(TaggedValueClass value_class);

    constexpr bool is_never() const;
    constexpr bool is_subset_of(TaggedValueSet other) const;
    constexpr bool is_disjoint_from(TaggedValueSet other) const;

    constexpr TaggedValueSet intersect(TaggedValueSet other) const;
    constexpr TaggedValueSet merge(TaggedValueSet other) const;

    constexpr uint32_t bits() const;

private:
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
never or unreachable:         empty set
unknown:                      every valid tag
```

This representation preserves facts that known-zero and known-one masks
cannot. Interned and refcounted pointers merge to the set `{0x08, 0x10}` even
though neither pointer-tag bit is individually known to be one.

The valid-tag universe is derived from the existing `Value` encoding constants,
not repeated as a second tag enumeration. A helper maps each existing encoded
value or pointer tag to `1 << (value & value_tag_mask)`, and
`valid_tagged_value_set_bits` combines those bits. `unknown()` returns that
bitset; invalid low-tag patterns are never introduced into the lattice.

```cpp
constexpr uint32_t tagged_value_set_bit(uint64_t encoded_value)
{
    return uint32_t{1}
           << uint32_t(encoded_value & value_tag_mask);
}

constexpr uint32_t valid_tagged_value_set_bits =
    tagged_value_set_bit(0) |
    tagged_value_set_bit(value_none) |
    tagged_value_set_bit(value_not_present) |
    tagged_value_set_bit(value_exception) |
    tagged_value_set_bit(value_boolean_tag) |
    tagged_value_set_bit(value_not_implemented) |
    tagged_value_set_bit(value_ellipsis) |
    tagged_value_set_bit(value_interned_ptr_tag) |
    tagged_value_set_bit(value_refcounted_ptr_tag);
```

The construction API reflects the sources of facts:

- `pointer()` contains the interned and refcounted pointer tags;
- `from_inline_tag()` validates a concrete five-bit inline tag and selects its
  single bit;
- `from_shape_key()` selects the concrete tag for an inline key and returns
  `pointer()` for a shape key;
- `smi()` and `boolean()` select their concrete tags, while
  `smi_or_boolean()` merges those sets;
- `from_class()` converts a restricted guard class into the arbitrary set it
  accepts.

`ShapeKey` therefore exposes read-only `is_inline()` and `inline_tag()` queries.
The conversion does not need access to the concrete `Shape *`. A future
promise of an exact lifetime-stable shape extends `TaggedValueSet` itself: the
set then combines `pointer()` with optional exact-shape identity.

Additional named set constructors are added only when instruction semantics
need them. Exact constants can use `from_inline_tag()`; there is no separate
public singleton abstraction.

## Cheap Instruction Classes

IR guards do not accept `TaggedValueSet`. They accept a `TaggedValueClass`, a
restricted and cheaply encodable description of the values admitted by the
guard:

```cpp
enum class TaggedValueClassKind : uint8_t
{
    MaskedEqual,
    MaskedNonZero,
};

class TaggedValueClass
{
public:
    static constexpr TaggedValueClass
    masked_equal(uint8_t mask, uint8_t expected);

    static constexpr TaggedValueClass masked_nonzero(uint8_t mask);

    static constexpr TaggedValueClass smi();
    static constexpr TaggedValueClass boolean();
    static constexpr TaggedValueClass smi_or_boolean();
    static constexpr TaggedValueClass any_inline();
    static constexpr TaggedValueClass pointer();

    constexpr TaggedValueClassKind kind() const;
    constexpr uint8_t mask() const;
    constexpr uint8_t expected() const;
    constexpr uint32_t encoded() const;

private:
    explicit constexpr TaggedValueClass(uint32_t encoded);

    uint32_t encoded_;
};
```

The packed representation occupies one instruction word:

```text
bits  0..7   mask
bits  8..15  expected bits
bits 16..23  TaggedValueClassKind
bits 24..31  reserved and zero
```

`masked_equal` requires `expected & ~mask == 0`. `masked_nonzero` has an
implicit expected value of zero. Masks and expected values are currently
restricted to `value_tag_mask`, although the byte-sized fields leave room for
a wider tag encoding.

The named classes are:

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
constructor takes an already valid `TaggedValueClass`; it does not perform
expensive semantic validation and cannot be passed an arbitrary tag set.

## Class-to-Set Conversion

Fact propagation needs only one conversion direction:

```cpp
constexpr TaggedValueSet
TaggedValueSet::from_class(TaggedValueClass value_class)
{
    if(value_class.kind() == TaggedValueClassKind::MaskedEqual &&
       value_class.mask() == value_tag_mask)
    {
        uint32_t bit = uint32_t{1} << value_class.expected();
        return TaggedValueSet(bit & valid_tagged_value_set_bits);
    }

    uint32_t accepted = 0;
    for(uint8_t tag = 0; tag <= value_tag_mask; ++tag)
    {
        uint32_t bit = uint32_t{1} << tag;
        if((valid_tagged_value_set_bits & bit) == 0)
        {
            continue;
        }

        bool matches;
        switch(value_class.kind())
        {
            case TaggedValueClassKind::MaskedEqual:
                matches = (tag & value_class.mask()) ==
                          value_class.expected();
                break;
            case TaggedValueClassKind::MaskedNonZero:
                matches = (tag & value_class.mask()) != 0;
                break;
        }
        if(matches)
        {
            accepted |= bit;
        }
    }
    return TaggedValueSet(accepted);
}
```

The full-mask equality shortcut handles exact inline classes without scanning
the tag universe. It intersects with the valid-tag bitset, so an exact class
for an invalid pattern produces `never()`. Other classes use the bounded
32-pattern loop.

There is no initial conversion from an arbitrary `TaggedValueSet` to a guard
class. The optimizer removes proved guards but does not narrow or synthesize
new classes. If a later optimization demonstrates a need for class synthesis,
it can add an explicitly costed selector without changing the fact lattice.

## Tagged-Value Analysis

The analysis maps each tagged program value directly to a `TaggedValueSet`.
There is no separate fact wrapper around the set. When exact shape propagation
is added, the optional exact shape becomes another component of
`TaggedValueSet` and its lattice operations.

Instruction results establish facts as follows:

- constants establish their exact low tag;
- SMI arithmetic and logical operations produce the SMI set;
- identity and ordinary comparison operations produce the Boolean set;
- a successful tagged-value guard intersects its input facts with the guard's
  class converted through `TaggedValueSet::from_class()`;
- forwarding definitions and semantic moves propagate their source facts;
- unknown parameters and unknown Python results begin with the full valid tag
  universe.

Non-entry block parameters begin at the empty set while the fixed-point
analysis discovers incoming paths. Their fact is the union of the facts for the
corresponding argument on every reachable predecessor edge. Entry parameters
begin unknown. Loop facts monotonically widen until stable.

Tag facts describe immutable bits of an SSA value. Calls, aliasing, and object
mutation do not invalidate them.

When the translator emits a forwarding guard, it first captures the guard's
pre-operation Snapshot and then replaces every occurrence of the original
definition in the current bytecode state with the guarded definition. Later
uses and outgoing block arguments therefore carry the refined SSA value. If
the same definition occurs at multiple source locations, each later guard
reads the already-refined definition and becomes eligible for redundant-guard
elimination. Ordinary bytecode destination writes are applied after these
state refinements.

## Shape Facts

Exact shape facts are more fragile than tag facts because aliases may mutate an
object's shape without changing the tagged pointer value. The analysis may
eventually carry an optional exact shape, but only when provenance and effects
make its validity explicit:

```cpp
Shape *exact_shape = nullptr;
```

The implemented tag-only shape policy is conservative:

- a successful shape guard establishes the `Pointer` tag fact;
- it does not by itself attach a persistent exact-shape fact;
- `BoxF64` currently establishes only the pointer tag fact;
- merges and alias effects therefore have no exact-shape component to preserve
  or clear yet.

The next extension adds exact immutable-shape facts:

- an instruction whose contract constructs an exact immutable builtin shape
  establishes that shape directly; in particular, `BoxF64` establishes the
  exact builtin-float shape;
- a successful guard for an immutable shape may establish that exact shape;
- merges retain an exact shape only when every reachable input has the same
  exact shape;
- immutable-shape facts survive aliasing effects because the shape cannot
  transition;
- any later exact fact for a mutable shape needs an explicit provenance and
  effect-bounded lifetime; possible aliasing mutation clears that fact while
  leaving the pointer tag fact intact.

The pointer check performed as part of shape guarding is independently
eliminable. The frontend emits `PointerAndShapeGuard`; tagged-value fact
propagation rewrites it to `ShapeOnlyGuard` when pointerness is already proved.
The check is therefore visible in Core optimization rather than hidden in
target emission.

Likely shapes are not facts. Profile or inline-cache predictions may justify
inserting a shape guard, and the successful guard may then establish exact
knowledge with an effect-bounded lifetime. Predictions remain outside this
lattice and can never justify guard removal.

## Redundant Guard Elimination

A tagged-value guard is redundant when its input facts are a subset of the
guard class's accepted tags:

```cpp
if(input_facts.is_subset_of(
       TaggedValueSet::from_class(guard.value_class())))
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
Target emitters select their own instruction sequence from the semantic class;
the IR does not encode a target opcode.

Adjacent guard combination is initially restricted to compatible
`MaskedEqual` classes. The existing OR-based combination does not prove that
two independent `MaskedNonZero` classes both succeeded.

## Implemented Foundation And Next Extension

The tag-class representation, class-to-set conversion, fixed-point analysis,
redundant guard elimination, `AnyInline` branch guards, and independently
eliminable shape-pointer checks are implemented.

The next extension is the exact immutable-shape component described above. It
does not synthesize narrowed tag guards, optimize always-failing guards, or
retain likely shapes. Those remain separate optimizations rather than implicit
consequences of adding exact shape facts.
