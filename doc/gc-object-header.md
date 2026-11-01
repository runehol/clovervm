# GC Object Header

| Field | Value |
|---|---|
| Document type | Architecture contract |
| Status | Accepted |
| Implementation | Eight-byte NonMoving/pointer-tag header protocol implemented; initial construction policy, production forwarding, and final RC-free layout remain pending |
| Scope | Eight-byte transitional and final headers, byte-sized GC state, and destructive forwarding |
| Owning layers | Object model owns header access; memory owns construction/placement; GC owns forwarding publication |
| Validated against | NonMoving working tree based on `6ebce070` (2026-11-01); earlier encoding validation recorded below |
| Supersedes | Earlier sixteen-byte transition, unmodified-pointer forwarding, and explicit bit-2 forwarding discriminator |

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
representation requires 64-bit pointers and valid VM object-pointer encodings:
32-byte allocation strides with address tag `0x08` or `0x10`. In particular,
the low three address bits are clear. These requirements must be
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
bit 2:     NonMoving (0 = movable, 1 = permanently non-moving)
bits 3--4: zero in normal headers; pointer-tag discriminator in forwarded words
bit 5:     spare, initially zero
bits 6--7: RC lifecycle during the transition; otherwise reserved
```

Forwarding is recognized independently of generation:

```cpp
bool forwarded = (state_byte & value_ptr_mask) != 0; // value_ptr_mask = 0x18
bool non_moving = (state_byte & 0x04) != 0;
auto generation = state_byte & 0x03;
```

All bit numbers here are zero-based. The pointer-tag bits are bits four and
five when counting from one; reusing their discriminator frees `0x04` for
NonMoving. Normal headers must keep `0x18` clear. A forwarded target has exactly
one valid pointer tag (`0x08` or `0x10`), not both. The broad discriminator tests
for a pointer tag independently of generation validity; generation zero remains
invalid in either form. Zero-filled storage is not a valid normal header.

A forwarded header contains an encoded, non-null target plus the source's
generation. Only a movable source may be forwarded. Publishing forwarding
preserves only source generation, never spare bits or RC lifecycle:

```cpp
uintptr_t encoded = reinterpret_cast<uintptr_t>(target)
                  | static_cast<uintptr_t>(generation());
std::memcpy(this, &encoded, sizeof(encoded));
```

Reading uses `memcpy` into `uintptr_t`, clears only bits 0--1, and converts back to
`HeapObject *`. All other pointer bits survive exactly, including address tags
`0x10` and `0x08` and address bits 6--7. On a forwarded header those upper byte
bits belong to the address, not RC lifecycle. The accessor must not clear them.
There is no inactive-union access or pointer-typed alias of normal fields.

Because a valid target has `0x04` clear, the forwarded source naturally reports
NonMoving=false: that describes its old, movable allocation. The destination
may independently have NonMoving=true, as in promotion into stable storage.
Resolve forwarding before asking about the current logical object's placement.

## Per-Object NonMoving State

NonMoving is independent of generation, remembered state, native layout,
immutability, and the pointer value's address tag. An ordinary managed pointer
does not change to the interned tag merely because its allocation becomes stable.
Non-moving objects still participate in tracing, weak liveness, and barriers.

Layout/storage policy supplies the initial value. Initial non-moving categories
include stable PyObject proxies, extension-owned C objects, CodeObjects, Shapes
whose addresses are used by JIT comparisons, and existing custom-destruction or
immortal/stable categories such as JitCodeObject. C API representation details
remain owned by the wrapper design; this does not prepend HeapHeader to
extension-controlled bodies or alter PyObject_HEAD.

The state is per allocation, not just per layout: a movable string or backing
may later need stable placement for a raw buffer export. Promotion is one-way.
Copy to stable storage, initialize the destination as NonMoving, repair incoming
references, and only then obtain and expose the new address. The old source
remains a movable forwarding source until retirement. Merely setting a nursery
source's bit in place does not stabilize it or make nursery disposal safe.

Full pinning support, including pin bits/counts, is deferred until the
reference-mapping work is in place. No pin representation or incoming-reference
index is selected by this change. Slab reclamation pins remain a separate
existing mechanism.

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
   generation and NonMoving state according to collection/destination policy.
   Evacuation handles one object at a time; overflow storage is discovered
   through an ordinary strong slot.
3. Store the encoded destination pointer plus the source generation over the
   source header.
4. Keep source storage alive until strong and weak repair are complete.

After publication, normal-only metadata queries must first resolve forwarding;
generation and the old source's movable state are the exceptions. Initially
Shapes are non-moving, so shape-derived extent queries do not need to follow a
forwarded Shape. Revisit that contract only if Shape movement is later admitted.
A direct query on a forwarded source cannot blindly
interpret pointer bytes as a native-layout ID or aux count.

Source selection and weak resolution can read the source generation directly
before or after forwarding. For example, a Young source forwarded to an Old
destination still reports Young. No source-space lookup is needed merely to
recover erased generation. This does not replace collection selection or space
membership where those are independently required.

Header access remains named: generation and the old allocation's NonMoving state
are readable in both forms; all other normal metadata accessors and setters
assert that the header is not forwarded.
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
Normal-only accessors assert that the header is not forwarded; generation and
the old allocation's NonMoving reads are the exceptions. A normal-only
`make_non_moving()` operation is idempotent and only sets the flag; there is no
operation to clear it on a live allocation.

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
| 0 | 1 | `uint8_t gc_state`: generation, NonMoving, and RC lifecycle |
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
bit 2:     NonMoving
bits 3--4: zero (normal); pointer tag (forwarded)
bit 5:     spare, initially zero
bits 6--7: RC lifecycle (Normal = 0, InZct = 1, Reclaiming = 2, Dead = 3)
```

Generation access loads the actual byte and masks its low two bits. Lifecycle
access loads the same byte and extracts bits 6--7. Updating either normal-state
component preserves the other, NonMoving, and the spare bit; normal pointer-tag
bits stay zero. Constructors initialize Old/Normal, native metadata, and zero RC;
construction policy sets NonMoving before publication where required. Lifecycle
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

## Code Migration Slices

1. **Header protocol (implemented).** Replace the explicit forwarding flag with pointer-tag
   detection, reserve normal `0x18`, decode targets with `~0x03`, and add inline
   `is_non_moving()` / `make_non_moving()` accessors and HeapObject delegates.
   Forwarding rejects non-moving sources and invalid target tags. Preserve all
   field offsets and the eight-byte total size. Existing constructor defaults
   may leave NonMoving clear during this preparatory slice; no collector may
   use it for production admission before construction initialization lands.
2. **Construction initialization.** Set initial NonMoving state centrally before
   object publication for custom-destruction layouts and known immortal/stable
   allocations. Ordinary objects retain the ability to become non-moving on a
   later stable promotion. Audit actual construction paths instead of adding
   per-layout relocation constructors. Native proxy/extension allocation remains
   subject to its separate C API representation review.

Neither slice implements promotion, pinning, nursery routing, or production
forwarding of RC-owned records. Reference-mapping and tracing-lifetime gates
remain in force. Compare Release accessor code and focused before/after
construction/refcount/reclamation measurements for the implementation.

## Implementation Checks

- Assert total size, alignment, and every normal field offset.
- Verify byte loads/comparisons for normal generation access in Release output.
- Exercise all 256 state bytes against `state & 0x18`; 192 classify as potential
  forwarding words, but this does not make invalid tags/generation values valid.
- Round-trip valid tagged targets across all higher low-byte address patterns.
  Bare aligned stack addresses are not necessarily valid VM target encodings.
  Verify exact pointer recovery after masking only bits 0--1 and preservation of
  source generation regardless of source lifecycle/spare bits.
- Verify constructors produce Old/Normal, and generation zero is rejected.
- Verify a Young source forwarding to an Old destination still reports Young.
- Verify movable-to-NonMoving destination forwarding: source remains movable,
  destination reports NonMoving, and the target pointer is recovered exactly.
- Reject forwarding a NonMoving source. Verify idempotent one-way marking,
  generation/lifecycle preservation, and reserved normal pointer-tag bits.
- Verify normal-only accessors and setters reject forwarded headers.
- For the final cutover, prove that layout inspection, source selection, and
  weak repair do not read overwritten normal metadata.
- Test all generation/lifecycle combinations and preservation in both setter
  directions; verify RC accesses now use offset four.
- Keep header representation changes separate from movement activation,
  relocation admission, and RC retirement.

## NonMoving Protocol Validation

The protocol working tree based on `6ebce070` passed 156 focused Debug
header/layout/reclamation/GC tests and 145 applicable focused Release tests.
Full Debug `all check` passed 2,357 tests; full Release passed 2,342. Each full
suite reports three existing runtime-JIT-disabled skips and one disabled test.
Encoding tests cover all discriminator bytes, valid tagged target patterns,
one-way/idempotent NonMoving marking, preservation across generation/lifecycle
and real RC updates, forwarding to a NonMoving destination, invalid target tags,
and rejection of NonMoving sources or mutation after forwarding.

Normal field offsets and sizes are unchanged. Release generation/refcount/
lifecycle probes are identical; materializing a forwarded Boolean adds one
instruction and publication removes one. The frame checker emits no diagnostics.
The before/after Release benchmark executables are byte-identical. The focused
[balanced timing control](gc-header-benchmarks.md#nonmoving-pointer-tag-revision)
records ordinary variation between those identical binaries, not a speedup or
regression claim. Initial construction policy and production movement remain
pending; these checks do not activate either.

## Earlier Transitional Implementation Validation

The results below apply to superseded encodings, not to the NonMoving revision.
The pointer-tag header protocol is implemented; initial construction policy is
still pending. New measurements and validation must be distinguished from these
historical results.

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
