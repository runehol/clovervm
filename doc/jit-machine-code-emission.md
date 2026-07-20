# JIT Machine-Code Emission

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Complete |
| Scope | Target instruction encoding, macro assembly, code fragments, labels, PC-dependent operation sizing, and final machine-code layout |
| Owning layers | The target backend chooses machine operations and block layout; the machine-code emitter owns exact encoding, code fragments, label resolution, PC-dependent form selection, and final copying |
| Validated against | [Arm A-profile A64 instruction-set architecture, 2026-06 encoding index](https://developer.arm.com/documentation/ddi0602/2026-06/Index-by-Encoding); focused emitter/encoder tests and executable AArch64 leaf/control-flow tests on 2026-07-20 |
| Supersedes | N/A |

This document defines how a target backend turns its final program-order
instruction stream into machine code. It complements
[JIT Compiler and IR](jit-compiler-and-ir.md) and deliberately does not add a
mandatory Machine IR. Ordinary instructions are encoded immediately. The
emitter retains only the structure required to resolve labels and choose among
encodings that depend on final machine-code positions.

[JIT Code Cache and Publication](jit-code-cache.md) owns the stable code and
constant-pool addresses into which finalization writes.

The first target is AArch64. The same retained layout structure supports a
later x86-64 backend, including its short and near conditional jumps.

## Layering and assembler interfaces

Each target provides two instruction-emission interfaces with different
contracts.

The direct assembler represents instructions the processor can encode
directly. Its operations accept only operands valid for that exact instruction
form. Target-specific operand types and exact method names make encoding
restrictions explicit, rather than routing unrelated immediate forms through
one generic integer abstraction. Examples include AArch64 add/subtract,
logical, move-wide, load/store-offset, and PC-relative immediates.

The macro assembler represents convenient target operations that may select or
emit several real instructions. For example, loading an arbitrary non-`Value`
AArch64 integer may select a logical immediate or `MOVZ`/`MOVN` plus `MOVK`
instructions. Python `Value` constants instead use the GC-visible constant pool
defined below. Macro operations must make any scratch-register requirements and
possible expansion visible to their callers.

The macro assembler also owns linking and non-linking direct branches to known
label or absolute targets. The emitter retains these branches when their form
depends on the final source address. A far transfer receives a caller-supplied
general-purpose scratch register, defaulting to AArch64 `IP0` (`x16`), that is
unavailable to the register allocator across that branch.

The direct assembler never silently expands an exact instruction. The macro
assembler owns such expansion and selection. This preserves a useful boundary
between knowing how one instruction is encoded and choosing an instruction
sequence.

The initial `AArch64Assembler` has two construction modes. During ordinary
generation it appends each encoded instruction to an `AArch64Emitter`. During
final direct-branch encoding or relocation it is constructed over the writable
destination and advances that pointer as it emits. Both modes use the same
instruction methods and encoding implementation. `AArch64MacroAssembler`
publicly inherits the direct assembler so ordinary code can mix exact and macro
operations without a forwarding method for every instruction; it can only be
constructed over an emitter.

The exact assembler follows the processor's encoding classes rather than
providing a separate C++ method for every mnemonic. Typed operation enums hold
their architectural field bits in place, W and X operand overloads supply the
instruction-width bit, and an encoding method combines those fields with the
fixed template for that class. The initial families include
`emit_arithmetic_imm12`, `emit_arithmetic_reg`, `emit_logical_reg`, and
`emit_move_wide_imm16`. Operand wrappers and separate enum types prevent fields
from unrelated encoding classes from being mixed.

Methods with irregular immediate fields advertise the restriction in their
names, such as `emit_ldr_unsigned_offset` and
`emit_b_conditional_immediate`. Macro methods use the usual assembly operation
names, including `mov`, `neg`, `cmp`, `ldr`, `b`, and `bl`. Instruction aliases
compose the exact encoding families: for example, register `mov` is an `ORR`
with the zero register and `neg` is a `SUB` from the zero register.

AArch64 register types distinguish both width and the operand-specific meaning
of encoding 31. Shared `GPRRegister<Width>`, `GPRRegisterOrSP<Width>`,
`GPRRegisterOrZero<Width>`, and `GPRAddSubDestination<Width>` templates provide
the implementation, while `XRegister`, `WRegister`, and the corresponding role
types are aliases. The distinct `xsp`/`wsp` and `xzr`/`wzr` values convert only
to the appropriate role, and W and X registers do not interconvert. Thin inline
W/X overloads validate typed operands and pass the width field and raw register
numbers to one shared encoding implementation.

### Encoding-shaped instruction families

Future AArch64 expansion should follow the encoding tables rather than mirror
the mnemonic index. Arm groups many instructions into encoding classes whose
members differ only in a few regularly placed fields. This regularity is
intentional: keeping related operations and operand fields in predictable bit
positions makes the hardware instruction decoder as regular as possible. The
assembler benefits from preserving the same structure instead of reconstructing
a mnemonic-shaped abstraction over it. It represents each independent field
with its own enum,
with values already shifted into the architectural bit positions. A shared
encoder then ORs the fixed class template, width, operation fields, and operands
exactly as the hardware decoder separates them:

```cpp
write_instruction(0x0a000000 | encoding_bits(width) |
                  encoding_bits(operation) | encoding_bits(invert) |
                  encoding_bits(shift) | register_field(source2, 16) |
                  (static_cast<uint32_t>(shift_amount) << 10) |
                  register_field(source1, 5) | destination);
```

`LogicalOp`, `InvertMode`, and `LogicalShift` cannot be accidentally mixed with
fields from another encoding class because they are distinct types. The same
`LogicalOp` values can be reused by another class only when Arm assigns that
class the same field meaning and bit positions.

W and X public overloads retain the operand-role types, validate restrictions
such as legal shift amounts and the meaning of register 31, and pass
`GPRWidth` plus raw register encodings to one private implementation. This keeps
the common call site typed and concise without duplicating the substantial bit-
packing code. `GPRWidth::W` is zero and `GPRWidth::X` contains the architectural
`sf` bit, so the shared implementation consumes it like any other encoding
field. An encoding class whose width lives elsewhere may reposition or map that
same width value locally, as unsigned-offset `LDR` does.

Instruction aliases belong in the macro assembler and compose these encoding
families. They do not receive duplicate exact encoders: `mov` uses `ORR` with
the zero register, `neg` uses `SUB` from the zero register, and `cmp` uses
flag-setting `SUBS` with the zero register as destination. The preferred order
for adding instructions is therefore:

1. identify the architectural encoding class and its independent fields;
2. add or reuse typed field enums whose values match the documented bits;
3. add one shared raw encoder for that class;
4. add thin typed overloads for width or operand-role validation;
5. express mnemonic aliases and multi-instruction selection in the macro
   assembler.

This pattern is documented by the
[Arm encoding index](https://developer.arm.com/documentation/ddi0602/2026-06/Index-by-Encoding)
and is also recorded beside the field enums in `aarch64_assembler.h` so later
instruction work starts from the encoding layout.

## Program-order emission and `CodeFragment`

The target backend emits instructions in final program order. Directly
encodable instructions are appended immediately to a byte buffer in the
current `CodeFragment`. A fixed-size instruction whose fields depend on its
final PC appends an encoded template and a machine-specific relocation. A
direct branch whose size may change terminates the fragment and remains
symbolic until final layout:

```text
CodeFragment
    directly encoded instruction bytes, including fixed-size placeholders
    zero or more RelocationEntry<Relocation> records
        fragment-relative instruction offset
        machine-specific relocation payload
    optional trailing size-varying direct branch
```

A fixed-size PC-dependent operation records the current byte offset, appends its
complete instruction template with zeroed address fields, and pushes a
relocation entry at that instruction offset. It does not end the fragment. A
variable-size direct branch is stored at the end of the fragment and starts a
new fragment for the following instruction stream. Every direct branch records
its minimum and maximum sizes and enough symbolic information to select and
encode one form after layout. Genuine indirect branches such as `RET`, `BR xN`,
or `BLR xN` contain no embedded `CodeTarget` and are encoded immediately.

The fragment container and three-pass finalizer are target-independent and are
implemented as a template over the target's direct-branch and relocation
types:

```text
MachineCodeEmitter<DirectBranch, Relocation>
    vector<CodeFragment<DirectBranch, Relocation>>
```

Its backend-facing surface is deliberately small:

```text
make_label() -> Label
resolve(Label) -> void
emit_bytes(const void *, size_t) -> void
emit_relocatable(const void *, size_t, Relocation) -> void
emit_direct_branch(DirectBranch) -> void
add_value_to_constant_pool(Value) -> ValuePoolEntry
finalize(CodeCache &, maximum_pool_span)
    -> Result<CodeAllocation, JitCodeError>
```

Finalization is single-use. `PoolOutOfRange` asks the driver to discard this
attempt and re-emit in far-pool mode; `AllocationFailure` abandons compilation.
Publication remains a separate code-cache operation.

Executable PCs, outside-unit targets, and pool-slot addresses use an opaque
`MachineAddress`, not an integer alias or C++ pointer. Its deliberately narrow
interface is conceptually:

```text
MachineAddress::offset_by(size_t bytes) -> MachineAddress
MachineAddress::displacement_to(MachineAddress target) -> int64_t
MachineAddress::aligned_displacement_to(
    MachineAddress target, uint8_t alignment_shift) -> int64_t
MachineAddress::offset_within(uint8_t alignment_shift) -> size_t
MachineAddress::bits_for_indirect_target() -> uintptr_t
```

`offset_by` performs checked address advancement for fragment and slot layout.
`displacement_to` computes a checked signed byte displacement without relying
on signed integer overflow or implementation-defined unsigned-to-signed
conversion. The aligned variants expose exactly the page displacement and
within-page offset needed by encodings such as AArch64 `ADRP` plus `LDR`, using
the base-two alignment shift for the architecture's granule rather than the
host OS page size. For example, an AArch64 4 KiB page uses a shift of 12.
`aligned_displacement_to` shifts both addresses down, computes a checked signed
difference, and returns that difference scaled back to bytes. Specifying a
shift makes a non-power-of-two alignment unrepresentable.
`bits_for_indirect_target` exists only so a target macro assembler can
materialize an absolute address before an indirect jump or call. The type has
no implicit pointer or integer conversion and no general arithmetic operators.

Writable instruction destinations use `void *`. The generic finalizer computes
the already-offset writable location before calling a target hook; target code
may cast that pointer only to store encoded fields. It cannot use a writable
pointer as a machine PC.

`Label` and `ValuePoolEntry` are target-independent opaque handles. A label
contains an index into the emitter's label-binding table. A pool entry contains
a byte offset from the beginning of the `Value` pool. Neither exposes its
stored integer or supports arithmetic.

The direct-branch type uses a target-specific tagged-kind representation rather
than a virtual instruction hierarchy. Each branch reports its
minimum and maximum size, selects and stores one concrete form during the
second layout pass, returns that selected size, and encodes only the stored form
during the third pass. Selection is not rerun while encoding. It also provides
the target's maximum pessimistic emission-unit size; the generic emitter does
not hardcode AArch64's 128 MiB limit.

The conceptual target contract is:

```text
DirectBranch
    target() const -> const variant<Label, MachineAddress> &
    min_size() -> uint32_t
    max_size() -> uint32_t
    select(MachineAddress source, MachineAddress target) -> uint32_t
    encode(void *write, MachineAddress source,
           MachineAddress target) const -> void
    static constexpr MaximumUnitSize

Relocation
    target() const -> RelocationTarget
    apply(void *write, MachineAddress instruction_pc,
          MachineAddress resolved_target) const -> void
```

These are compact tagged value types with no virtual dispatch. Selection stores
one small selected-form tag and occurs exactly once. Encoding and relocation
application are const, return no recoverable status, and hard-assert their final
range and template invariants. Recoverable resource failure belongs to the code
cache rather than target encoding.

Direct-branch selection receives a `MachineAddress` executable source PC,
never a writable pointer. Final encoding receives both a `void *` writable
destination at which to store the selected bytes and the corresponding
`MachineAddress` executable PC from which all displacements are calculated.
Those addresses may differ when the code cache uses dual mappings.

The relocation type is also machine-specific and uses a compact tagged-kind
representation. Its enclosing target-independent `RelocationEntry` supplies
the fragment-relative instruction offset. Its payload retains an opaque
`RelocationTarget` and the field layout needed for patching. Initially
`RelocationTarget` is `ValuePoolEntry`; the name deliberately permits a later
variant if another fixed-size relocation target is required. During the third
pass, the generic emitter resolves the target and invokes the relocation with
the `void *` writable instruction location, corresponding `MachineAddress`
executable instruction PC, and resolved target address. The writable location
is used only as the destination of stores; every displacement, page calculation,
and reachability check uses `MachineAddress`. A relocation may
modify only fields reserved by its instruction template; a multi-instruction
template such as AArch64 `ADRP` plus `LDR` uses one composite relocation.
Relocations are consumed by finalization and are not retained as a relocation
table after publication.

`CodeTarget` is a variant of `Label` and `MachineAddress`. Within-unit direct
branches use the label alternative; outside-unit transfers use an already
resolved absolute machine address. No alternative can represent an unresolved
external symbol.

Each direct branch that may need a general-purpose scratch register stores
the concrete scratch selected by its caller. Macro-assembler entry points
default that operand to AArch64 `x16`, but the emitter has no global scratch-
register configuration. The backend must make the supplied register dead
across every form the branch may select. A branch whose maximum form
does not need scratch does not clobber it.

The initial direct-branch set requires at most one implicit scratch at a time.
Macro operations accept register operands and may synthesize at most one
otherwise unavailable address or immediate. A lowering that needs two
simultaneously synthesized values must expose an additional temporary through
its `LocationSummary` and emit multiple operations; it may not consume a second
hidden scratch register. This permits a backend to reserve `x16` globally for
bring-up while allowing a later allocator to provide an ordinary temporary
without changing the emitter interface.

The resulting intended reuse is:

```text
using AArch64Emitter =
    MachineCodeEmitter<AArch64DirectBranch, AArch64Relocation>
using X86_64Emitter =
    MachineCodeEmitter<X86_64DirectBranch, X86_64Relocation>
```

The target assemblers append their already encoded instruction bytes to the
same generic fragment interface and construct their own relocation and direct-
branch types. Target-specific PC bases, displacement fields, selected forms,
scratch registers, and final encoding remain contained in those types.

A linking direct branch returns to the beginning of the following fragment. A
non-linking direct branch has no runtime fall-through; it is used for ordinary
jumps and tail calls.

`CodeFragment` is intentionally not called a basic block. The JIT CFG excludes
non-returning side exits from its block edges, while the emitter must retain
conditional branches to those exits. One compiler basic block can therefore
produce several code fragments:

```text
compiler basic block
    CodeFragment: instructions; guarded side-exit transfer
    CodeFragment: instructions; guarded side-exit transfer
    CodeFragment: instructions; block-edge transfer or no transfer
```

Code fragments describe only linear machine-code layout. They do not
participate in SSA, dominance, loops, block parameters, or recovery semantics.

The backend layer above the emitter chooses compiler-block order, chooses the
fall-through successor, inverts block conditions when useful, resolves
parallel edge moves, and removes an unconditional branch whose target is the
physical fall-through block. The emitter sees only the resulting program-order
instruction stream and does not reconstruct CFG intent.

## Labels

An unresolved label names a code-fragment boundary, not a provisional absolute
byte offset. Resolving a label closes the current fragment first if it contains
any directly encoded bytes; a direct branch has already closed its
fragment. The label is then associated with the current empty boundary, and
subsequent emission supplies that fragment's first byte. Resolving consecutive
labels associates them with the same boundary without creating redundant empty
fragments. A label's target address is always the start address of its fragment
and therefore the address of its first byte when the fragment is nonempty.
Empty target fragments are valid. Their labels resolve to the fragment's start
boundary, which may be the same address as an adjacent empty fragment or the
next nonempty fragment.

The initial emitter has no general code-position or post-layout metadata API.
A polling safepoint side-exits to the interpreter after restoring live values
to their canonical managed-frame slots, so the collector does not stop inside
generated code or require machine-register maps. Other position metadata will
be added only with a concrete consumer.

## Builder invariants and compilation failure

Malformed emitter state and impossible final encodings are compiler invariant
failures. Code-cache allocation failure is instead a recoverable compilation
outcome. Neither kind of failure sets Python pending-exception state.

Labels follow the existing `CodeObjectBuilder` jump-target pattern. Resolving a
label more than once is a hard assertion. Any label that remains unresolved is
a hard assertion when the emitter is finalized. Labels from
different emitter instances must not be mixed. Implementations should assert
that precondition when it is cheaply visible, but the design does not require
per-label ownership metadata solely to diagnose cross-emitter misuse.

Empty target fragments are not errors. Consecutive or otherwise empty
fragments may share one final code address.

The target direct-branch policy's pessimistic unit-size limit is a hard
assertion; it is 128 MiB for AArch64. Failure to allocate code-cache storage
abandons the compilation attempt, releases its fragments and owned pool
values, installs no compiled entry point, and continues execution in the
interpreter. Failure of final encoding or revalidation after form selection is
a hard failure; it indicates a violated emitter invariant rather than a reason
to fall back to interpreted execution.

An outside-unit transfer is constructed with an already resolved absolute
target address. It cannot contain an unresolved external symbol or use an
internal label. Symbolic labels are accepted only by within-unit operations,
making an unresolved external target unrepresentable by construction.

## GC-visible `Value` constant pool

Every machine-code `Value` constant resides in a constant-pool slot, including
SMIs, booleans, and other values whose bits could otherwise be embedded as an
instruction immediate. The finalized code unit exposes the pool base and slot
count separately to the moving garbage collector. The collector may trace and
rewrite every slot without decoding or modifying instruction bytes.

The initial pool contains only `Value` slots. Pool construction follows program
emission order. Appending a constant naturally aligns the next slot, records
its byte offset from the beginning of the pool, and retains ownership of the
`Value` until the finalized code unit assumes the GC-visible reference. The
pool base is aligned to `sizeof(Value)`, typically eight bytes on a 64-bit
target, so every slot is naturally aligned. Any
non-`Value` literal data is excluded from the initial pool and may not be
interleaved with the precisely scanned `Value` slots. A future non-GC literal
pool requires a separate design and must justify its additional layout,
identification, and memory-policy complexity.

The target-independent `MachineCodeEmitter` owns this pool builder alongside
its fragments. Adding a value returns an opaque `ValuePoolEntry` containing its
byte offset. Target relocations retain that handle but cannot inspect or perform
arithmetic on it; only the generic emitter resolves it to a final slot address.

The code cache first proposes stable code and pool addresses within the required
target reach, then commits the final size as an unpublished allocation:

```text
CodeAllocation
    writable code pointer

CodeSlice
    executable address
    committed capacity

ValuePoolSlice
    writable, non-executable Value slots
    pool address
    slot count
```

The slices share the compiled code object's lifetime but may occupy different
mappings. The pool-load width is fixed by the attempt's near or far mode before
placement. A proposal then fixes both addresses without exposing writable
storage. After address-dependent form selection determines the final size,
commit returns a move-only `CodeAllocation` that owns the active platform
code-write mode. The emitter uses the executable code address for all PC
calculations and writes through the allocation's writable pointer. The
code cache guarantees that neither slice moves and that the pool is aligned to
`sizeof(Value)`, separately identifiable, and writable by the moving collector.
Detailed placement and publication policy are defined by
[JIT Code Cache and Publication](jit-code-cache.md).

A constant-pool load with a form fixed by the attempt's pool mode is represented
by encoded template bytes plus a machine-specific relocation. The relocation
entry stores the fragment-relative instruction location; its payload stores the
field layout, target register information needed for validation, and opaque
pool entry. The emitter resolves its slot address as

```text
value_pool_address + constant_pool_offset
```

During the third pass the relocation computes the exact fields from that fixed
address and the instruction's actual executable PC, patches the copied template,
and revalidates its reach. The owned pool values are then copied into their
final slots. Neither code nor pool may move relative to the other afterward.

## Non-`Value` immediate materialization

The initial emitter does not place raw integers, addresses, floating-point bit
patterns, or other non-`Value` data in a literal pool. A target macro assembler
materializes such constants with instructions. Because the constant bits and
the resulting sequence size are known during program-order emission, these
instructions are encoded immediately into the current fragment and do not
create a direct branch.

On AArch64 an arbitrary 64-bit bit pattern requires at most one `MOVZ` or
`MOVN` followed by three `MOVK` instructions. The current macro assembler uses
`MOVZ` plus only the required `MOVK` instructions. A later expansion may choose
`MOVN` or a one-instruction logical immediate when that is shorter.
Materializing an operand for a following instruction uses the
direct branch's or lowering's caller-supplied scratch register when the
destination cannot hold the temporary itself. It does not introduce a second
hidden scratch.

Far absolute calls, jumps, and tail calls use a fixed four-instruction
move-wide sequence followed by the indirect transfer. They remain retained
because the choice between direct and far transfer depends on the final PC, not
because the far target bits require pool layout.

## Conservative three-pass finalization

Every retained direct branch has a pessimistic maximum-size form and,
where the target ISA provides one, a shorter form. Initial layout assigns every
direct branch its maximum size. The pessimistic encoded size of one
emission unit must not exceed the limit provided by the target direct-branch
type; the AArch64 limit is 128 MiB. The emitter requests a placement
proposal for the pessimistic code capacity and constant-pool slots before
choosing forms, giving the unit and pool stable final addresses without yet
advancing allocation frontiers. Failure to obtain a proposal returns a
recoverable compilation-abandoned result to the JIT driver rather than entering
the remaining passes.

Finalization uses three fragment walks, with placement proposed between the
first and second walks and the final size committed between the second and third
walks.

The first pass validates each branch's reported minimum and maximum sizes and
records each fragment's pessimistic start offset as a prefix sum of preceding
maximum sizes:

```text
fragment.max_size = encoded_bytes.size + branch.max_size
fragment[i].max_start = sum(fragment[j].max_size for j < i)
```

For a fragment without a direct branch, its branch maximum size is zero. The
final prefix end is the pessimistic code size. No minimum-layout or movement-
slack calculation is needed. The emitter asserts on an oversized unit and
requests stable proposed code and pool addresses from the code cache. A failed
placement request ends the compilation attempt without publishing code.

The second pass walks fragments in program order, assigns each fragment its
actual start address from a running cursor, and selects its trailing direct
branch's form. A label target inside the unit is tested using the pass-one
pessimistic source and target offsets, including for a forward target whose
actual address is not assigned yet. Shortening fragments between an internal
source and target can only reduce displacement magnitude, so a form that fits
the pessimistic internal layout remains safe. A form that would fit only after
other operations shorten remains long.

For a target outside the unit, the second pass already knows both the allocated
base and the current branch's actual source PC. It therefore tests the
PC-relative form against the exact external target address without additional
movement slack. Later choices cannot move a source address that the running
cursor has already assigned. The selected size advances the cursor to the next
fragment's actual start address. After this pass, the emitter commits the final
code size and receives a normal `CodeAllocation` with writable code and pool
slices.

The third pass walks the now-final fragments and writes directly into the
committed allocation. For each final byte offset it derives a writable
destination from `CodeAllocation::write_pointer` and an independent executable PC
from `CodeSlice::execute_address`. It copies each fragment's already encoded
template bytes, invokes its relocations with both addresses, and encodes its
selected trailing direct branch using the writable address only as a destination
and the executable address as its source PC. It then copies the owned `Value`s
into the stable pool slice. There is no intermediate final-layout buffer. The
final code size may be smaller than its pessimistic capacity; the pool address
does not change.

Form selection is not iterated. The design deliberately accepts conservative
long forms for internal targets in exchange for simple and deterministic
layout.

Final encoding must assert that every selected PC-dependent form still fits its
actual final PC and displacement.

After the third pass, finalization returns the completed unpublished
`CodeAllocation` to its caller. The caller then moves it into the code cache for
publication. Publication, or destruction after abandonment, restores platform
code-write protection exactly once.
Initial bring-up may validate writable bytes without entering them. The
standard backend uses page-rounded code ranges with a one-way RW-to-RX
transition; the preferred macOS AArch64 backend uses 16-byte packed `MAP_JIT`
code. Both keep pool storage on separate RW/NX pages and preserve the same
emitter contract and stable executable addresses.

## AArch64 conditional branches

The AArch64 assembler implements exact `B.cond` immediate encoding. Symbolic
narrow conditional branches can be added using the following short/long form
policy without changing the generic emitter.

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
branch range. The 128 MiB pessimistic unit-size limit guarantees this for every
instruction target within the same unit: AArch64 `B` uses a signed 26-bit
instruction-scaled displacement, and every instruction address in such a unit
is reachable from every other instruction address.

## AArch64 constant-pool loads

The target backend begins an attempt in `NearLiteral` pool mode. It emits each
near AArch64 `Value` constant-pool load as a fixed-size instruction template
with a relocation for its literal displacement:

```asm
    ldr  destination, pool_slot
```

Its signed `imm19 << 2` displacement ranges from -1 MiB through +1 MiB - 4
bytes relative to the instruction address. When that form does not fit, the
far-pool policy uses a fixed eight-byte inline operation and the destination
register itself to form the address:

```asm
    adrp destination, pool_slot@PAGE
    ldr  destination, [destination, pool_slot@PAGEOFF]
```

No additional scratch register is required because the second instruction
replaces the temporary address in `destination` with the loaded `Value`.
`ADRP` has a signed 21-bit page displacement, reaching from -4 GiB through
+4 GiB - 4 KiB relative to the instruction's page. The naturally aligned
eight-byte slot always has a valid scaled `LDR` offset within its page. Final
layout asserts that the complete code-to-pool span satisfies the `ADRP` range;
the 128 MiB code cap leaves ample room for the pool.

Both policies fix instruction size before the load is appended to its fragment.
The assembler writes complete instruction templates with zeroed PC-relative
fields and pushes an AArch64 relocation onto the fragment. The relocation
patches `imm19` for the near form or the `ADRP` page displacement and scaled
`LDR` page offset for the far form during the third pass.

After pessimistic sizing, a near-mode attempt asks the cache whether the code
and pool, rounded under its platform policy, fit within a one-MiB maximum span.
If not, the JIT driver discards only that machine-emission attempt and re-emits
deterministically from retained Core IR in forced `FarPageRelative` mode. The
first emitter, including its fragments, relocations, and temporary pool
ownership, is destroyed completely before the second is constructed. The far
attempt uses the fixed eight-byte form from its first emitted pool load and
supplies the `ADRP` maximum span to the same target-independent query. This
typed retry occurs before allocation, final encoding, or publication, and tests
may force either mode or force near rejection. It is not an allocation failure:
an actual inability to allocate the requested far storage abandons JIT
compilation and leaves execution in the interpreter.

## AArch64 absolute jumps and calls

An absolute-target `AArch64DirectBranch` records both its resolved target address
and whether it links. The macro assembler supplies two variants:

```text
non-linking jump or tail call:
    direct:  B target
    far:     materialize target in scratch; BR scratch

linking call:
    direct:  BL target
    far:     materialize target in scratch; BLR scratch
```

The transfer records its caller-supplied scratch register, which defaults to
`x16` (`IP0`). The far forms clobber that register. Direct `B` and `BL` forms
are chosen only when the absolute-target displacement from the pass-two actual
source PC fits. Otherwise the macro assembler's ordinary constant-
materialization path emits one `MOVZ`, three `MOVK` instructions, and `BR` or
`BLR` for a fixed 20-byte far form. Keeping that selected size fixed avoids
making layout depend on the target's nonzero halfwords. The code allocation's
base address must remain stable after this selection.

## x86-64 reuse

The same code-fragment algorithm supports variable-length x86-64 transfers
without an x86-specific relaxation framework. A conditional branch uses its
near `rel32` form in pessimistic layout and selects `rel8` when that form fits.
An unconditional jump can similarly choose between near and short forms when
the backend has not already removed it as a fall-through transfer. Calls and
jumps to absolute targets use the same pass-two exact-PC rule when choosing a
PC-relative form over a target-specific indirect form.

A `Value` constant-pool load is an encoded instruction template plus an
x86-64 relocation. It uses a 64-bit RIP-relative memory operand:

```asm
    mov destination, qword ptr [rip + pool_slot]
```

The signed `disp32` is relative to the end of the instruction and reaches from
-2 GiB through +2 GiB - 1 byte. The assembler appends the complete prefix,
opcode, ModR/M byte, and a zero `disp32`, then records the displacement-field
offset and instruction length in the relocation. The third pass derives the
executable instruction-end PC and patches the copied `disp32`. This does not
end the fragment. Final layout asserts that the complete code-to-pool span fits
`disp32`.

As on AArch64, selection is non-iterative. Some x86 branches that would fit
only after other branches shrink remain in their near form. This is an
intentional policy, not a correctness limitation.

## Verification

Emitter tests should cover the implemented generic contracts. Target-specific
bullets apply when that target or instruction family is added:

- every exact instruction and immediate-field encoder at its range boundaries;
- rejection of values that an exact instruction cannot encode;
- macro-assembler selection among immediate materialization sequences;
- forward and backward labels, consecutive labels, and branchless fragments;
- short-form branches exactly at both displacement limits;
- long-form selection immediately outside each short range;
- multiple intervening branches shrinking without invalidating an already
  selected short branch;
- one compiler basic block producing several fragments through side exits;
- assertion on a pessimistic emission unit larger than its target policy's
  limit, including AArch64's 128 MiB limit;
- linking and non-linking absolute transfers selecting both direct and far
  forms, including tail calls;
- absolute direct branches selecting forms from their exact pass-two source PC
  near both displacement limits;
- fragment minimum and maximum sizes and pessimistic start offsets computed in
  the first pass;
- actual fragment starts assigned while selecting forms in the second pass;
- direct final-buffer encoding without forward-reference backpatching in the
  third pass;
- separately allocated stable code and pool slices, with a
  `sizeof(Value)`-aligned pool and naturally aligned slots at recorded offsets;
- fixed-size PC-dependent instruction templates and per-fragment relocations
  applied at fragment-relative offsets without creating fragment boundaries;
- relocations and trailing direct branches using distinct writable and
  executable addresses, with all PC-relative calculations based on the latter;
- all `Value` constants, including SMIs and booleans, loaded from pool slots
  rather than embedded in instruction bytes;
- exclusion of non-`Value` literals from the pool and immediate AArch64
  materialization using no more than four move-wide instructions;
- AArch64 literal loads at both signed displacement limits, `ADRP` plus `LDR`
  far-pool policy, and final code-to-pool page-range validation;
- forced AArch64 near-pool emission, forced far-pool emission, and near
  placement rejection followed by deterministic far re-emission;
- x86-64 RIP-relative pool loads at both `disp32` limits, including final
  relocation rewriting;
- code-cache allocation failure abandoning compilation without publication,
  leaking owned pool values, or setting Python pending-exception state;
- simulated GC rewriting of pool slots without changing instruction bytes;
- labels resolved after encoded bytes, after a direct branch, and
  consecutively, each resolving to the target fragment's start boundary;
- empty target fragments sharing their final address with an adjacent boundary;
- assertion on multiply bound and referenced-but-unbound labels;
- linking-transfer return positions at offset zero in the next fragment;
- caller-supplied scratch registers, including the default `IP0` (`x16`), used
  and clobbered only by forms that require them;
- emitted bytes checked through an independent disassembler or reference
  assembler;
- one-scratch maximum expansion and rejection of lowerings that would require
  a second hidden scratch register.

Tests should also verify the intended conservative case: a branch that fails
the pessimistic fit test remains long even when final shortening would have
made its short form fit.

## Future extensions

Possible target-local extensions include better tie-breaking among equal-length
AArch64 non-`Value` immediate sequences and a veneer policy outside the capped-
unit design. They do not change or block the implemented generic emitter.

## Related documents

- [JIT Code Cache and Publication](jit-code-cache.md)
- [JIT Compiler and IR](jit-compiler-and-ir.md)
- [JIT Control-Flow Graph](jit-control-flow-graph.md)
- [JIT Compiler Bring-up Plan](jit-compiler-bring-up-plan.md)
- [Decision Log](decision-log.md)
