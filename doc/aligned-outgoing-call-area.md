# Aligned Outgoing Call Area

This note describes the intended stack and call-frame layout for CloverVM when
call frames are aligned to 16-byte boundaries while `Value` remains an 8-byte
type.

The goal is to make CloverVM frames compatible with native-stack-style frame
alignment without widening `Value`, and to reserve call-frame metadata in a
place that ordinary temporary reuse will not overwrite. This matters for lazy
exception unwinding: after a call returns, the caller may continue executing
without immediately materializing the callee frame, so the old callee frame
header must remain intact until the next Python call boundary.

## Basic Invariants

- `Value` stays 8 bytes.
- Stack allocation for call ABI purposes happens in pairs of `Value`.
- Frame pointers are 16-byte aligned.
- The first argument/parameter slot is 16-byte aligned.
- Argument and parameter counts are physically rounded up to an even number.
- Odd argument counts leave one unused padding slot between the last real
  argument and the callee frame header.
- Ordinary locals and temporaries are separate from the outgoing argument area.
- The outgoing argument area lives below all ordinary locals and temporaries.

The high-level frame shape is:

```text
higher addresses

    p0
    p1
    p2
    padding if parameter count is odd
    frame header
fp  previous frame pointer
    return metadata
    r0
    r1
    ...
    a0
    a1
    ...

lower addresses
```

The disassembler should use these names:

- `pN`: inbound parameter slot for the current function.
- `rN`: ordinary local or temporary register slot for the current function.
- `aN`: outgoing argument area slot used to prepare calls made by the current
  function.

This replaces the old ambiguous use of `aN` for callee parameters.

## Parameter Layout

Parameters are still addressed relative to `fp`, but their physical positions
are based on a padded parameter count:

```cpp
padded_parameter_count = round_up_to_even(n_parameters);
```

For example, a function with three parameters has four physical parameter
slots:

```text
higher addresses

    p0
    p1
    p2
    padding
    frame header
fp  previous frame pointer

lower addresses
```

The padding slot is not a logical parameter. It exists only so both `p0` and
`fp` satisfy 16-byte alignment.

## Outgoing Argument Area

Each `CodeObject` records an outgoing argument area size, for example:

```cpp
uint32_t n_outgoing_call_slots;
```

This area is part of the current frame and sits below all ordinary locals and
temporaries. It is reused for calls made by the function. Reuse is intentional:
starting a new Python call is the point where lazy exception unwinding may need
to materialize any delayed frame state.

The outgoing area must be large enough for:

- the largest explicit call emitted by codegen,
- internal protocol calls that opcodes may need to perform, such as binary
  operator dispatch through `__add__`,
- descriptor and method-call adaptation needs,
- default-argument filling once supported.

The size is rounded up to an even number of `Value` slots.

## Codegen Strategy

Codegen should keep using the existing simple register-addressed instruction
set. In particular, common bytecodes such as `Star`, `Ldar`, `CallSimple`, and
`CallMethodAttr` should continue to access slots as direct `fp[reg]` operands.

We should not add special opcodes whose effective address is computed from
`n_locals + n_temporaries` at runtime. That would make otherwise simple
instructions depend on `code_object` for stack addressing.

Instead, codegen emits calls with provisional register operands and records
relocation entries for every register operand involved in outgoing argument
buildup. After ordinary temporary layout is finalized, codegen patches those
operands to point into the fixed outgoing argument area.

A relocation record can be shaped like:

```cpp
struct OutgoingArgRelocation
{
    uint32_t operand_offset;
    uint32_t outgoing_slot_offset;
};
```

where `operand_offset` is the bytecode offset of an encoded register operand,
and `outgoing_slot_offset` is the desired slot within the outgoing argument
area.

For a direct call such as `f(x, y)`, codegen can still evaluate directly into
the eventual outgoing call slots:

```text
Star provisional_call_slot_0   ; callable or call target staging
Star provisional_call_slot_1   ; arg0
Star provisional_call_slot_2   ; arg1
CallSimple provisional_call_slot_0, 2
```

Finalization patches these provisional operands to `aN` offsets below the
ordinary temporary area.

This preserves direct argument evaluation into call slots, avoiding an
additional runtime copy for normal explicit calls.

## Function Entry

Function entry uses the final argument count after call adaptation:

- explicit arguments,
- inserted `self`,
- default-filled arguments,
- any later keyword or vararg adaptation.

The final count is rounded up to an even physical count:

```cpp
padded_arg_count = round_up_to_even(n_args);
```

The caller lays out arguments so the first argument slot is 16-byte aligned.
Arguments are stored contiguously downward from the first argument. If `n_args`
is odd, the slot below the last real argument is padding. The callee frame
header is placed below the padded argument span.

For three arguments:

```text
higher addresses

    arg0
    arg1
    arg2
    padding
    frame header
fp  previous frame pointer

lower addresses
```

For two arguments:

```text
higher addresses

    arg0
    arg1
    frame header
fp  previous frame pointer

lower addresses
```

The frame-entry helper should assert in debug builds that the new `fp` is
16-byte aligned.

## Method Calls

Method call adaptation must produce canonical outgoing arguments starting at
`a0`.

If method lookup binds `self`, then runtime call preparation writes:

```text
a0 = self
a1 = first explicit user argument
a2 = second explicit user argument
...
```

If method lookup does not bind `self`, the explicit arguments must be copied up
so that:

```text
a0 = first explicit user argument
a1 = second explicit user argument
...
```

The no-`self` path must not merely adjust the first-argument pointer by one
slot. The outgoing layout, padding, and frame header placement are part of the
ABI, so the argument values need to occupy the canonical slots.

## Internal Python Calls From Opcodes

Some bytecodes may need to call Python code even though the source program did
not contain an explicit call at that point. For example, a future slow path for
`op_add` may need to resolve and call:

```text
a.__add__(a, b)
```

These internal calls must use the same outgoing argument area recorded on the
current `CodeObject`. They should not construct ad hoc argument windows in the
ordinary temporary area.

This keeps all Python call frames below ordinary temporaries and gives lazy
exception unwinding a single place to reason about delayed callee frame
materialization.

## Disassembly

Disassembly should classify encoded registers by physical frame region.

Positive offsets are parameters:

```text
p0, p1, p2, ...
```

Negative offsets first cover ordinary locals and temporaries:

```text
r0, r1, r2, ...
```

Negative offsets below the ordinary register count are outgoing argument area
slots:

```text
a0, a1, a2, ...
```

This makes patched call setup readable:

```text
Star a0
Star a1
Star a2
CallSimple a0, 2
```

and makes it clear when bytecode is operating on inbound parameters, local
working storage, or outbound call slots.

## Open Implementation Notes

- The existing `encode_reg` logic should use the padded parameter count when
  encoding or decoding parameter slots.
- Function and class-body codegen must reserve any parameter padding needed by
  the new convention.
- `get_lowest_occupied_frame_offset()` should include ordinary locals,
  ordinary temporaries, and the outgoing argument area.
- Debug assertions should check frame alignment at interpreter entry and after
  every function or class-body frame transition.
- The outgoing area can be a single reusable area per frame. Multiple outgoing
  call windows are not required unless a future optimization needs them.

## Implementation Staging

The migration should be staged so each patch establishes one visible invariant.

1. Done: rename disassembled parameters from `aN` to `pN`.

   This is only a naming change. It should not alter bytecode layout or runtime
   behavior. Update tests and docs that assert disassembly text. This clears the
   vocabulary before `aN` starts meaning outgoing argument area. Update
   `doc/function-calling-convention.md` so it uses `pN` for parameters.

2. Done: add ABI alignment helper.

   Add a small helper such as `round_up_to_abi_alignment`. This should be a
   mechanical utility step with little or no behavior change.

3. Done: fix method no-`self` adaptation.

   Method calls that do not bind `self` must copy explicit arguments into the
   canonical outgoing argument slots starting at `a0`. They must not merely
   adjust the first-argument pointer by one slot. This needs to happen before
   padded parameter layout, because pointer-shifting relies on the old
   contiguous argument/header relationship. Update
   `doc/function-calling-convention.md` with the method-call adaptation rules.

4. Use padded parameter layout.

   Parameter encoding and decoding should use
   `round_up_to_abi_alignment(n_parameters)`. Function and class-body codegen
   must reserve parameter padding when needed. Add tests covering odd and even
   parameter counts. Update
   `doc/function-calling-convention.md` to describe padded parameter slots and
   the padding between odd parameter lists and the frame header.

5. Add frame-alignment assertions.

   Ensure the initial interpreter frame is 16-byte aligned and assert alignment
   after function and class-body frame transitions.

6. Introduce `n_outgoing_call_slots` and disassemble `aN`.

   Add a `CodeObject` field for the outgoing call area size. Include it in
   frame bounds calculations. Teach disassembly to classify negative offsets
   below ordinary locals and temporaries as `aN`. Initially this area can be
   zero-sized until call relocations start using it. Update
   `doc/function-calling-convention.md` with the `pN` / `rN` / `aN` frame
   regions.

7. Add codegen relocations for explicit calls.

   Record patch locations for register operands used in call argument buildup.
   After final ordinary temporary layout is known, patch those operands into the
   outgoing argument area. Normal explicit calls should continue evaluating
   directly into their eventual call slots. Update
   `doc/function-calling-convention.md` so explicit calls are documented as
   using the outgoing area rather than the current temporary frontier.

8. Make frame entry use padded argument counts.

   Update function and class-body frame-entry helpers so callee frames are
   placed below `round_up_to_abi_alignment(n_args)`. Add tests for
   odd-argument calls and nested calls. Update
   `doc/function-calling-convention.md` with the final argument-padding and
   frame-header placement rules.

9. Use the outgoing area for internal Python calls.

   Interpreter slow paths that may call Python code, such as future protocol
   dispatch for `op_add`, should build their calls in the same outgoing argument
   area recorded on the current `CodeObject`. Update
   `doc/function-calling-convention.md` when the first internal Python-call
   slow path lands.
