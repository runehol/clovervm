# GC Object Header

| Field | Value |
|---|---|
| Document type | Architecture contract |
| Status | Accepted |
| Implementation | Eight-byte transitional representation implemented; production forwarding and final RC-free layout are not enabled |
| Scope | Eight-byte transitional and final headers, byte-sized GC state, and destructive forwarding |
| Owning layers | Object model owns header access; memory owns construction/placement; GC owns forwarding publication |
| Validated against | Contained-header working tree based on `8d3bb678` (2026-10-31); validation recorded below |
| Supersedes | Earlier sixteen-byte transition, separate forwarding-bit, and generation-preserving packed-word proposals |

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
checks can use a byte load and direct comparison. The native-layout and aux
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

Every byte value whose low three bits are zero is reserved for forwarding:

```cpp
bool forwarded = (state_byte & 0x07) == 0;
// Forwarded byte values: 0x00, 0x08, 0x10, ... 0xf8.
```

The other low-three-bit codes are reserved. During the RC transition bits 3--4
hold RC lifecycle and bits 5--7 are reserved; after RC retirement the upper five
normal-state bits are available for future definitions. No normal state may use a low-three-bit-zero encoding,
regardless of future flags. Unassigned normal encodings are not silently treated
as a valid generation.

A forwarded header contains only an unmodified, non-null `HeapObject *` target
over the full eight bytes. Pointer alignment guarantees a forwarded byte
encoding. Current refcounted and interned pointer address tags, `0x10` and
`0x08`, leave the low three bits clear and are retained in the stored pointer.
Bits 5--7 may be ordinary address bits: forwarding must not be recognized by
comparing the complete byte against only `0x10` or `0x00`.

Publishing forwarding is therefore a plain full-width pointer store. There is
no tag to OR into the pointer and no generation/flag mask to remove when
retrieving it. The byte remains inspectable after that store; an implementation
must use a valid byte view of the representation rather than rely on an inactive
union member. Zero-filled header storage would look forwarded with a null
target, not like a valid new object: constructors explicitly initialize normal
state, initially `Old` during the RC transition.

## Destructive Forwarding Contract

Forwarding replaces the complete normal header. The source's native-layout ID,
aux count, generation, and reserved byte are no longer available there. The
implementation must not promise to preserve or mutate them after forwarding.

The publication order is:

1. Read the source's layout, allocation/copy extents, and any other required
   normal-header state before replacing it.
2. Construct the complete destination state, preserving native-layout ID, aux
   count, and any transferable header state. Set the destination's normal
   generation according to collection policy. Complete every group member
   before publishing its forwarding association, as required by the group API.
3. Store the unmodified destination pointer over the source header.
4. Keep source storage alive until strong and weak repair are complete.

After publication, normal-header queries must first resolve forwarding. In
particular, shape-dependent layout queries must work when the referenced shape
has already been forwarded. A direct query on a forwarded source cannot blindly
interpret pointer bytes as a native-layout ID or aux count.

Source selection and weak resolution must not require an overwritten source
generation. The adapter must recognize forwarding before normal-state reads or
retain the necessary collection-membership information separately. This is a
required adapter contract, not permission to derive all generations from
addresses or to activate copying before that contract exists.

Header access remains named: generation access is for normal headers;
forwarding detection examines the state byte; forwarding-target access is for
forwarded headers. A separate nullable target coexisting with generation is not
the final representation. Returning to normal state requires initializing a
normal header, not clearing a pointer while preserving nonexistent source flags.

## Eight-Byte RC Transition

The representation lives in a standalone
[`HeapHeader`](../src/object_model/heap_header.h).
[`HeapObject`](../src/object_model/heap_object.h) contains one private header
and delegates its object-facing API to it. Every accessor/delegate is defined
inline. RC callers use `refcount()` rather than a public data member; generation,
lifecycle, native layout, and aux count are likewise accessed through methods.
Normal accessors assert that the header is not forwarded.

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
bits 0--2: generation (Young = 1, Old = 2, OldRemembered = 3)
bits 3--4: RC lifecycle (Normal = 0, InZct = 1, Reclaiming = 2, Dead = 3)
bits 5--7: reserved, initially zero
```

Generation access loads the actual byte and masks its low three bits. Lifecycle
access loads that same byte and extracts bits 3--4. Updating either preserves
the other and the reserved bits. Every valid normal combination has nonzero low
three bits, so lifecycle changes cannot accidentally make a forwarded header.
Constructors initialize Old/Normal, native metadata, and zero RC. Lifecycle
users migrate to accessors; no RC transition or reclamation state is discarded.

A forwarding pointer would overwrite all eight bytes, including RC and native
metadata. **This slice does not permit production forwarding of RC objects.**
Encoding tests may use disposable synthetic headers, but no RC reclamation path
may observe a header whose refcount has become pointer bits. This is not a claim
that RC and copying cannot coexist: such a transition would need to preserve RC
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
- Exercise all 256 possible state-byte values against the forwarding predicate;
  exactly the 32 low-three-bit-zero values are forwarding encodings.
- Round-trip valid target pointers with both current address tags and different
  bits 5--7, retaining the exact pointer bits without tagging or masking.
- Verify normal construction never produces a forwarded/null-target header.
- For the final cutover, prove that layout inspection, source selection, and
  weak repair do not read overwritten normal metadata.
- Test all generation/lifecycle combinations and preservation in both setter
  directions; verify RC accesses now use offset four.
- Keep header representation changes separate from movement activation,
  relocation admission, and RC retirement.

## Transitional Implementation Validation

The contained-header working tree based on `8d3bb678` passed 170 focused tests,
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
