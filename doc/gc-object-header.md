# GC Object Header

| Field | Value |
|---|---|
| Document type | Architecture contract |
| Status | Accepted design; not implemented |
| Implementation | Current HeapObject remains the eight-byte RC/native-layout header |
| Scope | Final eight-byte header, byte-sized GC state, destructive forwarding, and relationship to the RC transition |
| Owning layers | Object model owns header access; memory owns construction/placement; GC owns forwarding publication |
| Validated against | Header design agreed 2026-10-31; source inspected at `1f56180b` |
| Supersedes | Earlier separate forwarding-bit and generation-preserving packed-word proposals in the copying-adapter investigation |

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

The other low-three-bit codes and the upper five normal-state bits are reserved
for future definitions. No normal state may use a low-three-bit-zero encoding,
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

## Relationship To The Sixteen-Byte RC Transition

Do not remove RC before tracing replaces its lifetime contract. The first
transitional header retains the existing eight-byte prefix, including its
current native-ID/aux widths and field offsets, and appends one eight-byte GC
word. In normal state that second word has the byte-sized `gc_state` at offset
eight and zeroed reserved bytes. In forwarded state the second word holds the
unmodified target pointer. The total is sixteen bytes, aligned to eight.

This replaces the earlier proposal to retain generation in two low bits beside
a forwarding pointer. Generation is not preserved in the forwarded GC word.
The legacy prefix remains physically intact during the transition, but new GC
code must not depend on it to evade the final destructive-forwarding contract.
No production forwarding is enabled merely by adding the word.

Removing the legacy RC prefix and migrating native-layout/aux access into the
final eight-byte normal representation is a later, explicit cutover. The
16-/32-bit final field widths do not silently widen the legacy fields in the
first transition slice. Widening requires auditing declarations, accessors,
count limits, and layout formulas together.

The transitional word changes object sizes and payload offsets. For example,
Float grows from 32 to 40 bytes and crosses from a 32-byte to a 64-byte allocator
stride. Before/after Release measurements, representative layout/stride checks,
descriptor tests, and JIT offset audits remain required before implementing that
slice. The final eight-byte layout is a path back from this temporary cost, not
a promise that widening count limits has no other cost.

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
- Keep header representation changes separate from movement activation,
  relocation admission, and RC retirement.
