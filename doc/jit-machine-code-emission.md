# JIT Machine-Code Emission

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Not started |
| Scope | Target instruction encoding, macro assembly, code fragments, labels, PC-dependent operation sizing, and final machine-code layout |
| Owning layers | The target backend chooses machine operations and block layout; the machine-code emitter owns exact encoding, code fragments, label resolution, PC-dependent form selection, and final copying |
| Validated against | N/A |
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
form. Target-specific immediate classes or checked constructors should make
encoding restrictions explicit, rather than routing unrelated immediate forms
through one generic integer abstraction. Examples include AArch64 add/subtract,
logical, move-wide, load/store-offset, and PC-relative immediates.

The macro assembler represents convenient target operations that may select or
emit several real instructions. For example, loading an arbitrary non-`Value`
AArch64 integer may select a logical immediate or `MOVZ`/`MOVN` plus `MOVK`
instructions. Python `Value` constants instead use the GC-visible constant pool
defined below. Macro operations must make any scratch-register requirements and
possible expansion visible to their callers.

The macro assembler also owns linking and non-linking transfers to absolute
targets. These operations remain deferred when their direct form depends on
the final source address. The same rule applies to PC-relative loads, address
formation, and any other operation whose encoding or selected form depends on
its final PC. A far transfer receives a caller-supplied general-purpose scratch
register, defaulting to AArch64 `IP0` (`x16`), that is unavailable to the
register allocator across that operation.

The direct assembler never silently expands an exact instruction. The macro
assembler owns such expansion and selection. This preserves a useful boundary
between knowing how one instruction is encoded and choosing an instruction
sequence.

## Program-order emission and `CodeFragment`

The target backend emits instructions in final program order. Directly
encodable instructions are appended immediately to a byte buffer in the
current `CodeFragment`. A fixed-size instruction whose fields depend on its
final PC appends an encoded template and a machine-specific relocation. Only an
operation whose size may change terminates the fragment for layout purposes
and remains symbolic until final layout:

```text
CodeFragment
    directly encoded instruction bytes, including fixed-size placeholders
    zero or more RelocationEntry<Relocation> records
        fragment-relative instruction offset
        machine-specific relocation payload
    optional trailing size-varying deferred operation
```

A fixed-size PC-dependent operation records the current byte offset, appends its
complete instruction template with zeroed address fields, and pushes a
relocation entry at that instruction offset. It does not end the fragment. A
variable-size deferred operation is stored at the end of the fragment and
starts a new fragment for the following instruction stream.
`DeferredTransfer` is the branch, jump, and call kind; other deferred kinds may
include variable-size PC-relative loads and address formation. Every deferred
operation records its minimum and maximum sizes and enough symbolic information
to select and encode one form after layout.

The fragment container and three-pass finalizer are target-independent and are
implemented as a template over the target's deferred-operation and relocation
types:

```text
MachineCodeEmitter<DeferredOperation, Relocation>
    vector<CodeFragment<DeferredOperation, Relocation>>
```

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

`Label` and `CodePosition` are not template-dependent. A label identifies only
a fragment boundary. A code position identifies a fragment plus an offset in
its directly encoded bytes. Neither contains an instruction encoding,
displacement rule, register, or other target property.

The deferred-operation type uses a target-specific tagged-kind representation
rather than a virtual instruction hierarchy. Each operation reports its
minimum and maximum size, selects and stores one concrete form during the
second layout pass, reports that selected size, and encodes only the stored form
during the third pass. Selection is not rerun while encoding. It also provides
the target's maximum pessimistic emission-unit size; the generic emitter does
not hardcode AArch64's 128 MiB limit.

The conceptual target contract is:

```text
DeferredOperation
    min_size() -> uint32_t
    max_size() -> uint32_t
    select(const DeferredSelectionContext &) -> void
    selected_size() -> uint32_t
    encode(const DeferredEncodingContext &) const -> void
    static max_unit_size() -> size_t

Relocation
    span_size() -> uint32_t
    apply(const RelocationContext &) const -> void
```

These are compact tagged value types with no virtual dispatch. Selection stores
one small selected-form tag and occurs exactly once. Encoding and relocation
application are const, return no recoverable status, and hard-assert their final
range and template invariants. Recoverable resource failure belongs to the code
cache rather than target encoding.

`DeferredSelectionContext` supplies the executable instruction PC and computes
a displacement to a target with a candidate form's PC bias. This lets AArch64
use an instruction-address PC while x86-64 uses the candidate instruction end.
For an internal label it applies the conservative pass-one layout; for an
absolute target it uses the pass-two executable PC. `DeferredEncodingContext`
supplies the `void *` write destination, executable PC, and now-final target.
`RelocationContext` similarly supplies the writable instruction location,
executable PC, and stable pool base.

Deferred-operation selection receives a `MachineAddress` executable source PC,
never a writable pointer. Final encoding receives both a `void *` writable
destination at which to store the selected bytes and the corresponding
`MachineAddress` executable PC from which all displacements are calculated.
Those addresses may differ when the code cache uses dual mappings.

The relocation type is also machine-specific and uses a compact tagged-kind
representation. Its enclosing target-independent `RelocationEntry` supplies
the fragment-relative instruction offset; the payload supplies the symbolic
data and field layout needed for patching, such as a constant-pool slot offset,
instruction length, or displacement-field offset. It has no size-selection
interface and cannot change fragment layout. During the third pass, after the
fragment template bytes have been copied, the generic emitter invokes every
relocation with the `void *` writable instruction location, the corresponding
`MachineAddress` executable instruction PC, and the finalized code and pool
address context. The writable location is used only as the destination of
stores; every displacement, page calculation, and reachability check uses
`MachineAddress`. A relocation may
modify only fields reserved by its instruction template; a multi-instruction
template such as AArch64 `ADRP` plus `LDR` uses one composite relocation.
Relocations are consumed by finalization and are not retained as a relocation
table after publication.

Within-unit operations are constructed with a `Label` target. Outside-unit
transfers are constructed through separate macro-assembler entry points that
require a resolved absolute code address. The API does not expose one
permissive target type that can accidentally represent an unresolved external
symbol.

Each deferred operation that may need a general-purpose scratch register stores
the concrete scratch selected by its caller. Macro-assembler entry points
default that operand to AArch64 `x16`, but the emitter has no global scratch-
register configuration. The backend must make the supplied register dead
across every form the operation may select. An operation whose maximum form
does not need scratch does not clobber it.

The initial deferred-operation set requires at most one implicit scratch at a
time. Macro operations accept register operands and may synthesize at most one
otherwise unavailable address or immediate. A lowering that needs two
simultaneously synthesized values must expose an additional temporary through
its `LocationSummary` and emit multiple operations; it may not consume a second
hidden scratch register. This permits a backend to reserve `x16` globally for
bring-up while allowing a later allocator to provide an ordinary temporary
without changing the emitter interface.

The resulting intended reuse is:

```text
using AArch64Emitter =
    MachineCodeEmitter<AArch64DeferredOperation, AArch64Relocation>
using X86_64Emitter =
    MachineCodeEmitter<X86_64DeferredOperation, X86_64Relocation>
```

The target assemblers append their already encoded instruction bytes to the
same generic fragment interface and construct their own relocation and trailing
operation types. Target-specific PC bases, displacement fields, selected forms,
scratch registers, and final encoding remain contained in those types.

Terminating a fragment is a layout property, not necessarily a control-flow
property. A linking transfer returns to the beginning of the following
fragment. A non-linking transfer has no runtime fall-through; it is used for
ordinary jumps and tail calls. A trailing variable-size PC-relative non-
transfer operation continues at the beginning of the following fragment.

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

## Labels and code positions

An unresolved label names a code-fragment boundary, not a provisional absolute
byte offset. Binding a label closes the current fragment first if it contains
any directly encoded bytes; a deferred operation has already closed its
fragment. The label is then associated with the current empty boundary, and
subsequent emission supplies that fragment's first byte. Binding consecutive
labels associates them with the same boundary without creating redundant empty
fragments. A label's target address is always the start address of its fragment
and therefore the address of its first byte when the fragment is nonempty.
Empty target fragments are valid. Their labels resolve to the fragment's start
boundary, which may be the same address as an adjacent empty fragment or the
next nonempty fragment.

The code position after any deferred operation, including the return PC of a
linking transfer and the continuation after a PC-relative load, is offset zero
in the following fragment.

Machine-code metadata must also survive form shortening. Safepoints,
snapshots, source positions, relocations, and other positions emitted before
final layout should be represented by a fragment identity plus an offset within
that fragment. Finalization translates these positions using the fragment's
final absolute offset.

No instruction or metadata consumer may retain an initial absolute offset
across final layout.

## Builder invariants and compilation failure

Malformed emitter state and impossible final encodings are compiler invariant
failures. Code-cache allocation failure is instead a recoverable compilation
outcome. Neither kind of failure sets Python pending-exception state.

Labels follow the existing `CodeObjectBuilder` jump-target pattern. Binding a
label more than once is a hard assertion. A referenced label that remains
unbound is a hard assertion when the emitter or label is finalized. Labels from
different emitter instances must not be mixed. Implementations should assert
that precondition when it is cheaply visible, but the design does not require
per-label ownership metadata solely to diagnose cross-emitter misuse.

Empty target fragments are not errors. Consecutive or otherwise empty
fragments may share one final code address.

The target deferred-operation policy's pessimistic unit-size limit is a hard
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
its fragments. Target deferred operations refer to slots through generic
constant-pool offsets; only the instruction used to load a slot is target-
specific.

The code cache first proposes stable code and pool addresses within the required
target reach, then commits the final size as separate writable slices:

```text
CodeSlice
    writable view
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
commit returns the writable slices. The emitter uses the executable code address
for all PC calculations and writes through the code slice's writable view. The
code cache guarantees that neither slice moves and that the pool is aligned to
`sizeof(Value)`, separately identifiable, and writable by the moving collector.
Detailed placement and publication policy are defined by
[JIT Code Cache and Publication](jit-code-cache.md).

A constant-pool load with a form fixed by the attempt's pool mode is represented
by encoded template bytes plus a machine-specific relocation. The relocation
entry stores the fragment-relative instruction location; its payload stores the
field layout, target register information needed for validation, and pool
offset. Its slot address is

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
create a deferred operation.

On AArch64 an arbitrary 64-bit bit pattern requires at most one `MOVZ` or
`MOVN` followed by three `MOVK` instructions. The macro assembler may choose a
one-instruction logical immediate or a shorter move-wide sequence when the bits
permit it. Materializing an operand for a following instruction uses the
deferred operation's or lowering's caller-supplied scratch register when the
destination cannot hold the temporary itself. It does not introduce a second
hidden scratch.

Far absolute calls, jumps, and tail calls follow the same move-wide policy when
their direct form is not selected. They remain deferred because the choice
between direct and far transfer depends on the final PC, not because the far
target bits require pool layout.

## Conservative three-pass finalization

Every deferred PC-dependent operation has a pessimistic maximum-size form and,
where the target ISA provides one, a shorter form. Initial layout assigns every
deferred operation its maximum size. The pessimistic encoded size of one
emission unit must not exceed the limit provided by the target deferred-
operation type; the AArch64 limit is 128 MiB. The emitter requests a placement
proposal for the pessimistic code capacity and constant-pool slots before
choosing forms, giving the unit and pool stable final addresses without yet
advancing allocation frontiers. Failure to obtain a proposal returns a
recoverable compilation-abandoned result to the JIT driver rather than entering
the remaining passes.

Finalization uses three fragment walks, with placement proposed between the
first and second walks and the final size committed between the second and third
walks.

The first pass computes each fragment's minimum and maximum size and records
its pessimistic start offset as a prefix sum of preceding maximum sizes:

```text
fragment.min_size = encoded_bytes.size + deferred.min_size
fragment.max_size = encoded_bytes.size + deferred.max_size
fragment[i].max_start = sum(fragment[j].max_size for j < i)
```

For a fragment without a deferred operation, its deferred minimum and maximum
sizes are zero. The final prefix end is the pessimistic code size; the sum of
minimum sizes is a lower bound on the final code size. The emitter asserts on
an oversized unit and requests stable proposed code and pool addresses from the
code cache. A failed placement request ends the compilation attempt without
publishing code.

The second pass walks fragments in program order, assigns each fragment its
actual start address from a running cursor, and selects its trailing deferred
operation's form. A label target inside the unit is tested using the pass-one
pessimistic source and target offsets, including for a forward target whose
actual address is not assigned yet. Shortening fragments between an internal
source and target can only reduce displacement magnitude, so a form that fits
the pessimistic internal layout remains safe. A form that would fit only after
other operations shorten remains long.

For a target outside the unit, the second pass already knows both the allocated
base and the current operation's actual source PC. It therefore tests the
PC-relative form against the exact external target address without additional
movement slack. Later choices cannot move a source address that the running
cursor has already assigned. The selected size advances the cursor to the next
fragment's actual start address. After this pass, the emitter commits the final
code size and receives a normal `CodeAllocation` with writable code and pool
slices.

The third pass walks the now-final fragments and writes directly into the
committed allocation. For each final byte offset it derives a writable
destination from `CodeSlice::write_pointer` and an independent executable PC
from `CodeSlice::execute_address`. It copies each fragment's already encoded
template bytes, invokes its relocations with both addresses, and encodes its
selected trailing operation using the writable address only as a destination
and the executable address as its source PC. It then copies the owned `Value`s
into the stable pool slice. There is no intermediate final-layout buffer. The
final code size may be smaller than its pessimistic capacity; the pool address
does not change.

Form selection is not iterated. The design deliberately accepts conservative
long forms for internal targets in exchange for simple and deterministic
layout.

Final encoding must assert that every selected PC-dependent form still fits its
actual final PC and displacement.

After the third pass, finalization resolves fragment-relative metadata and
returns the completed code and pool slices to the code cache for publication.
Initial bring-up may validate writable bytes without entering them. The first
executable tier uses page-rounded code ranges in standard `mmap` slabs with a
one-way RW-to-RX transition; the later macOS tier uses 16-byte packed `MAP_JIT`
code. Both keep pool storage on separate RW/NX pages and preserve the same
emitter contract and stable executable addresses.

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

An absolute-target `DeferredTransfer` records both its resolved target address
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
materialization policy synthesizes the complete target address in the supplied
scratch before the indirect transfer. The code allocation's base address must
remain stable after this selection.

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
- assertion on a pessimistic emission unit larger than its target policy's
  limit, including AArch64's 128 MiB limit;
- linking and non-linking absolute transfers selecting both direct and far
  forms, including tail calls;
- absolute transfers and PC-relative non-transfer operations selecting forms
  from their exact pass-two source PC near both displacement limits;
- fragment minimum and maximum sizes and pessimistic start offsets computed in
  the first pass;
- actual fragment starts assigned while selecting forms in the second pass;
- direct final-buffer encoding without forward-reference backpatching in the
  third pass;
- separately allocated stable code and pool slices, with a
  `sizeof(Value)`-aligned pool and naturally aligned slots at recorded offsets;
- fixed-size PC-dependent instruction templates and per-fragment relocations
  applied at fragment-relative offsets without creating fragment boundaries;
- relocations and trailing deferred operations using distinct writable and
  executable addresses, with all PC-relative calculations based on the latter;
- all `Value` constants, including SMIs and booleans, loaded from pool slots
  rather than embedded in instruction bytes;
- exclusion of non-`Value` literals from the pool and immediate AArch64
  materialization using no more than four move-wide instructions;
- AArch64 literal loads at both signed displacement limits, `ADRP` plus `LDR`
  far-pool policy, and final code-to-pool page-range validation;
- forced AArch64 near-pool emission, forced far-pool emission, and near
  placement rejection followed by deterministic far re-emission;
- x86-64 RIP-relative pool loads at both `disp32` limits, including deferred
  rewriting when minimum and maximum operation sizes are equal;
- code-cache allocation failure abandoning compilation without publication,
  leaking owned pool values, or setting Python pending-exception state;
- simulated GC rewriting of pool slots without changing instruction bytes;
- labels bound after encoded bytes, after a deferred operation, and
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

## Open implementation details

The design does not yet choose:

- concrete C++ representations for immediate fields and the payloads of
  deferred PC-dependent operation kinds;
- tie-breaking among equal-length AArch64 non-`Value` immediate sequences;
- any future veneer policy outside the initial capped-unit design.

Those choices belong to target-emitter implementation work and must preserve
the direct-assembler/macro-assembler split and the code-fragment layout
invariants above.

## Related documents

- [JIT Code Cache and Publication](jit-code-cache.md)
- [JIT Compiler and IR](jit-compiler-and-ir.md)
- [JIT Control-Flow Graph](jit-control-flow-graph.md)
- [JIT Compiler Bring-up Plan](jit-compiler-bring-up-plan.md)
- [Decision Log](decision-log.md)
