# GC Object Header

| Field | Value |
|---|---|
| Document type | Architecture contract |
| Status | Accepted |
| Implementation | Eight-byte transitional representation implemented; production forwarding and final RC-free layout are not enabled |
| Scope | Eight-byte transitional and final headers, byte-sized GC state, and destructive forwarding |
| Owning layers | Object model owns header access; memory owns construction/placement; GC owns forwarding publication |
| Validated against | Generation-preserving encoding based on `21303dce` (2026-11-01); earlier validation recorded below |
| Supersedes | Earlier sixteen-byte transition and unmodified-pointer forwarding encoding |

## Final Eight-Byte Layout

After tracing becomes lifetime authority and RC/lifecycle bookkeeping can be
removed, the normal header occupies exactly eight bytes:

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 1 | `uint8_t gc_state` |
| 1 | 1 | Reserved; potentially future locking state |
| 2 | 2 | 16-bit native-layout ID |
| 4 | 4 | 32-bit auxiliary count |

The header word is eight-byte aligned. The intended normal representation is:

```cpp
// Design sketch, not current HeapObject fields.
struct alignas(uintptr_t) NormalGcHeader
{
    uint8_t gc_state;
    uint8_t reserved;
    uint16_t native_layout;
    uint32_t aux_count;
};
```

`gc_state` is an actual byte field, not a C++ bitfield or an accessor that must
load a 64-bit word and extract generation bits. Frequent normal-state generation
checks use a byte load and low-two-bit mask, in both header forms. The native-layout and aux
fields have their own 16-/32-bit widths; widening their current implementation
types and limits is a migration step, not something already implemented.

The reserved byte initially has no locking semantics and is initialized to zero.
No spare bits are reclaimed from the native-layout or aux fields to encode GC
state. GC state is explicit metadata, not inferred generation from addresses.

## Byte Encoding And Forwarding

The current target platforms are little-endian, so the byte at offset zero also
contains the low eight bits of a pointer stored over the header word. This
representation requires 64-bit pointers and at least eight-byte pointer
alignment, including any preserved address tags. These requirements must be
checked explicitly when implemented; this is not an endian-neutral wire format.

Normal state initially uses these byte values:

```cpp
enum class HeapGenerationState : uint8_t
{
    Young         = 0x01,
    Old           = 0x02,
    OldRemembered = 0x03,
};
```

The state byte uses the following normal representation:

```text
bits 0--1: generation (Young = 1, Old = 2, OldRemembered = 3)
bit 2:     forwarded flag (zero in normal headers)
bits 3--5: spare, initially zero
bits 6--7: RC lifecycle during the transition; otherwise reserved
```

Forwarding is recognized independently of generation:

```cpp
bool forwarded = (state_byte & 0x04) != 0;
auto generation = state_byte & 0x03;
```

Generation zero is invalid in either form. The discriminator still tests only
bit 2, not generation validity or the entire byte. Zero-filled storage is not a
valid normal header; constructors initialize Old/Normal explicitly.

A forwarded header contains an encoded, non-null target plus the source's
generation. Eight-byte target alignment leaves three low bits available.
Publishing forwarding preserves only the source generation, never its spare
bits or RC lifecycle:

```cpp
uintptr_t encoded = reinterpret_cast<uintptr_t>(target)
                  | uintptr_t{0x04}
                  | static_cast<uintptr_t>(generation());
std::memcpy(this, &encoded, sizeof(encoded));
```

Reading uses `memcpy` into `uintptr_t`, clears bits 0--2, and converts back to
`HeapObject *`. All other pointer bits survive exactly, including address tags
`0x10` and `0x08` and address bits 6--7. On a forwarded header those upper byte
bits belong to the address, not RC lifecycle. The accessor must not clear them.
There is no inactive-union access or pointer-typed alias of normal fields.

## Destructive Forwarding Contract

Forwarding replaces the complete normal header. The source's native-layout ID,
aux count, refcount, lifecycle, and reserved byte are no longer available there.
The source generation remains readable and is not changed to the destination's
generation. No setter mutates a forwarded source header.

The publication order is:

1. Read the source's layout, allocation/copy extents, and any other required
   normal-header state before replacing it.
2. Construct the complete destination state, preserving native-layout ID, aux
   count, and any transferable header state. Set the destination's normal
   generation according to collection policy. Evacuation handles one object
   at a time; overflow storage is discovered through an ordinary strong slot.
3. Store the encoded destination pointer plus the source generation over the
   source header.
4. Keep source storage alive until strong and weak repair are complete.

After publication, normal-only metadata queries must first resolve forwarding;
generation is the exception. Shape-dependent layout queries must work when the
referenced shape has already been forwarded. A direct query on a forwarded source cannot blindly
interpret pointer bytes as a native-layout ID or aux count.

Source selection and weak resolution can read the source generation directly
before or after forwarding. For example, a Young source forwarded to an Old
destination still reports Young. No source-space lookup is needed merely to
recover erased generation. This does not replace collection selection or space
membership where those are independently required.

Header access remains named: generation is readable in both forms; all normal
metadata accessors and setters assert that the header is not forwarded.
Forwarding-target access asserts that the header is forwarded. A separate
nullable target coexisting with normal metadata is not the representation.
Returning to normal requires initializing a normal header.

## Eight-Byte RC Transition

The representation lives in a standalone
[`HeapHeader`](../src/object_model/heap_header.h).
[`HeapObject`](../src/object_model/heap_object.h) contains one private header
and delegates its object-facing API to it. Every accessor/delegate is defined
inline. RC callers use `refcount()` rather than a public data member; generation,
lifecycle, native layout, and aux count are likewise accessed through methods.
Normal-only accessors assert that the header is not forwarded; generation reads
are the explicit exception.

Both directions of forwarding use `memcpy` within HeapHeader. The write is
exactly `sizeof(HeapHeader)`, never `sizeof(HeapObject)` or a derived object.
There is no inactive-union access or pointer-typed alias of the stored fields.
Size/alignment/offset assertions belong to the header representation, and tests
exercise it independently plus verify that object-level forwarding leaves
derived payload bytes untouched.

Keep RC until tracing replaces its lifetime contract, but retain the current
eight-byte total size rather than adding a second word:

| Offset | Bytes | Transitional field |
|---:|---:|---|
| 0 | 1 | `uint8_t gc_state`: generation plus RC lifecycle |
| 1 | 1 | Current 8-bit native-layout ID |
| 2 | 2 | Current 16-bit auxiliary count |
| 4 | 4 | `int32_t refcount` |

The header is eight-byte aligned. RC moves from offset zero to four; native ID
moves from five to one, and aux count from six to two. Payload offsets must be
audited rather than assumed to move: Float remains 32 bytes with a 32-byte
allocator stride, unlike the superseded sixteen-byte transition.

The existing lifecycle state remains necessary. Pack it into the state byte:

```text
bits 0--1: generation (Young = 1, Old = 2, OldRemembered = 3)
bit 2:     forwarded flag, zero in normal headers
bits 3--5: spare, initially zero
bits 6--7: RC lifecycle (Normal = 0, InZct = 1, Reclaiming = 2, Dead = 3)
```

Generation access loads the actual byte and masks its low two bits. Lifecycle
access loads the same byte and extracts bits 6--7. Updating either normal-state
component preserves the other, the spare bits, and the zero forwarded bit.
Constructors initialize Old/Normal, native metadata, and zero RC. Lifecycle
users migrate to accessors; no RC transition or reclamation state is discarded.

A forwarding pointer would overwrite all eight bytes, including RC and native
metadata. **This slice does not permit production forwarding of RC objects.**
RC lifecycle is not preserved after forwarding: reclamation and forwarding
must not operate on the same source concurrently or without an explicit
transition protocol. Encoding tests may use disposable synthetic headers, but
no RC reclamation path may observe a header whose refcount has become pointer
bits. This is not a claim that RC and copying cannot coexist: such a transition
would need to preserve RC
in the destination and retarget, skip, or follow forwarded sources in ZCT,
epoch, and reclamation bookkeeping before RC reads. Precise mutator-root repair
alone does not establish that protocol. It remains a separate activation gate;
this header slice does not change the accepted tracing-cutover staging.
The first slice changes header representation, not allocator policy, barriers,
nursery allocation, marking storage, or collector activation.

When RC and lifecycle bookkeeping can be removed, repack to the final layout
above: the freed four bytes fund the wider native-layout ID, wider aux count,
and reserved byte. Widening still requires auditing declarations, accessors,
count limits, and layout formulas together.

Before/after Release measurements, representative layout/stride checks,
descriptor tests, RC load/store audits, and JIT offset checks remain required.

## Future Lock State

If the reserved byte later holds lock state, preserve it in the destination
before overwriting the source header with forwarding. Destructive forwarding
must not discard live lock state.

Copying that byte alone does not define a movable-lock protocol: waiters,
ownership, atomic publication, and any address-based lock bookkeeping must also
be relocation-aware. No locking or concurrent-collector protocol is introduced
by reserving the byte. Current header operations are designed for the existing
stop-the-world collection contract.

## Implementation Checks

- Assert total size, alignment, and every normal field offset.
- Verify byte loads/comparisons for normal generation access in Release output.
- Exercise all 256 possible state-byte values against the bit-2 discriminator;
  128 have that flag set, but generation zero is invalid in either form.
- Round-trip aligned targets with both address tags and all low-byte address
  patterns. Verify exact pointer recovery after masking only bits 0--2 and
  source-generation preservation regardless of source lifecycle/spare bits.
- Verify constructors produce Old/Normal, and generation zero is rejected.
- Verify a Young source forwarding to an Old destination still reports Young.
- Verify normal-only accessors and setters reject forwarded headers.
- For the final cutover, prove that layout inspection, source selection, and
  weak repair do not read overwritten normal metadata.
- Test all generation/lifecycle combinations and preservation in both setter
  directions; verify RC accesses now use offset four.
- Keep header representation changes separate from movement activation,
  relocation admission, and RC retirement.

## Transitional Implementation Validation

The generation-preserving revision based on `21303dce` passed 153 focused
header/layout/reclamation/GC tests, full Debug `all check` (2,342 passed), and
full Release `all check` (2,328 passed). Each full suite has three existing
JIT-disabled skips and one disabled test. New Debug death tests reject normal
metadata access and mutation after forwarding, invalid generation zero,
unaligned/null targets, and repeated forwarding. Object/header size and offsets
remain unchanged. Release generation reads remain a byte load plus mask; target
decoding adds one mask. The frame checker reports no diagnostics before or after.

The [focused A/B results](gc-header-benchmarks.md#generation-preserving-forwarding-revision)
retain both initial and longer confirmation runs. Refcounted writes are
essentially unchanged; construction is noisy and changes sign. Reclamation is
approximately 1--3% slower in the two series, with variation and no established
instruction-level cause. These RC-mode benchmarks do not exercise forwarding.

The following measurements describe the earlier unmodified-pointer encoding,
not the generation-preserving revision. The contained-header working tree based
on `8d3bb678` passed 170 focused tests,
full Debug `all check` (2,329 passed), and full Release `all check` (2,318 passed).
Both full suites report three existing runtime-JIT-disabled skips and one
disabled test. The hot-opcode frame checker reports no new warnings.

The Release layout probe checked 52 rows covering all registered types,
HeapObject, and four Instance capacities. No allocator stride changed. Float
stays 32 bytes with its value at offset 24. HeapObject alignment changes from
four to eight; ValidityCell grows from 12 to 16 bytes but retains a 32-byte
stride. Accessors inline, and memcpy forwarding folds to one pointer load/store.

The [full A/B report](gc-header-benchmarks.md) includes every one of the 92
benchmark cases and balanced A/B/B/A confirmation on 16 cases. Results are
mixed: repeated medians show method keyword calls 4.48% slower, construction
without __init__ 2.91% slower, and memory reclamation 6.62% faster. These are not
grounds for a blanket performance-neutral claim. Review accepted the mixed
profile as broadly neutral for this change while retaining the individual
slowdowns. Short profiles do not isolate their instruction-level cause.
