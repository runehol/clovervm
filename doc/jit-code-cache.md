# JIT Code Cache and Publication

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Not started |
| Scope | Stable code and constant-pool allocation, PC-relative reachability, writable and executable views, publication, and initial code lifetime |
| Owning layers | The code cache owns virtual-memory placement, page protection, writable and executable views, and storage lifetime; the machine-code emitter owns sizing and encoding against addresses supplied by the cache; the compiled code object owns the resulting code and `Value`-pool slices |
| Validated against | N/A |
| Supersedes | The contiguous code-plus-pool placement in `doc/jit-machine-code-emission.md` |

This document defines how compiled code and its GC-visible `Value` constant
pool receive stable addresses and how completed machine code becomes executable.
It complements [JIT Machine-Code Emission](jit-machine-code-emission.md), which
defines fragment layout and target encoding, and
[JIT Compiler and IR](jit-compiler-and-ir.md), which defines the compiled-code
and garbage-collection contracts.

The initial implementation uses simple page-rounded code allocations. A later
macOS implementation packs functions into `MAP_JIT` pages using thread-local
JIT write protection. Both implementations expose the same code-cache API and
preserve the same executable addresses.

## Storage and ownership model

A finalized compiled code unit owns two non-moving slices:

```text
CompiledCodeStorage
    CodeSlice
        writable view
        executable address
        pessimistic capacity
        final encoded size

    ValuePoolSlice
        writable, non-executable address
        naturally aligned Value slots
        slot count
```

The two slices share one compiled-code lifetime but not necessarily one mapping
or adjacent addresses. The code cache must place them within the target's
required PC-relative reach. Neither slice may move while the compiled code can
execute. The moving collector may rewrite pool-slot contents but does not move
the pool slice itself.

Code and pool storage always occupy different physical pages. A page acquires a
permanent code or pool role before its first suballocation and never changes
roles during the slab's lifetime. This remains true even on platforms whose
code-write mechanism would technically permit mixed permissions through an
alias. Unused space in a pool page may be shared only by other pool slices;
unused space in a code page may never hold GC-rewritten values.

The compiled code object exposes the pool base and exact slot count to garbage
collection. Pool storage contains only `Value` slots and is aligned to
`sizeof(Value)`. Multiple compiled functions may own packed slices within the
same writable, non-executable pool pages. A function does not receive a whole
pool page merely to preserve page-level protection.

Initial generated code is immortal. Code-cache retirement, slice reuse, and
dependency-aware reclamation are later policies. Until those exist, a direct
outside-unit transfer may target only code or native entry points whose address
remains stable for at least the complete lifetime of the source code unit.

## Code-cache interface

The emitter writes through a writable view but performs all PC-relative
calculations from the executable address:

```text
CodeSlice
    void *write_pointer
    MachineAddress execute_address
    capacity
```

Those addresses may be equal, as in the initial implementation and typical
macOS `MAP_JIT` use, or may be distinct aliases, as required by the intended
Linux dual-mapping implementation. The emitter must never infer executable
addresses from writable pointers.

`write_pointer` is a `void *`, while `execute_address` and the pool slice's
address are opaque `MachineAddress` values. `MachineAddress` supports checked
offset advancement, checked signed byte and aligned displacement between two
machine addresses, offset within an alignment, and extraction of address bits
solely for materializing an indirect transfer target. The aligned queries
support architectural page-relative encodings without exposing general integer
arithmetic. The type has no implicit pointer or integer conversion. The cache
constructs machine addresses from its mappings; target encoders do not
construct them from writable pointers.

Conceptually, finalization requests storage after its pessimistic sizing pass:

```text
CodeCache::allocate(
    pessimistic_code_size,
    value_pool_slot_count,
    reachability_requirement)
        -> allocation result
```

The concrete result is an exception-independent `Result<T, Error>` rather than
Python-exception-bearing `Expected<T>`:

```text
enum CodeCacheError
    NearPlacementUnavailable
    AllocationFailure
    PublicationFailure

Result<PendingCodeAllocation, CodeCacheError>
```

`Result` follows the existing `Expected` value/error and propagation shape but
carries its error explicitly and never reads or writes pending Python exception
state. `CL_TRY` may be generalized through a `propagate_failure` operation so
both result families retain the same concise call-site behavior.

The result distinguishes successful slices, a near-placement rejection that
requests a far-mode retry, and actual storage-allocation failure. Allocation
failure abandons this JIT compilation and continues execution in the
interpreter; it does not set Python pending-exception state or publish a partial
code object. The attempt's pool-load width is already fixed; allocation success
fixes both addresses before the emitter selects address-dependent forms and
performs final PC-relative encoding. The emitter writes no more than the
requested code capacity or pool slot count. Publication records the final code
size, performs required instruction-cache synchronization, and makes the
executable view callable.

Failure of the platform protection transition or another fallible publication
step returns `PublicationFailure`. The pending allocation remains unpublished,
its RAII owner releases it, and execution continues in the interpreter without
setting Python pending-exception state. This is distinct from failed final
instruction revalidation, which is a hard compiler-invariant failure.

`PendingCodeAllocation` is move-only RAII. Its destructor cancels an unpublished
reservation. Publication consumes it and transfers both slices into a
`JitCodeObject`; no unpublished allocation or pool ownership survives a failed
attempt. The concrete C++ API may separate reservation, writable scope,
publication, and ownership transfer, but it must preserve this ordering and
address split.

## Reachability slabs

The cache normally allocates one-MiB reachability slabs. Code pages grow upward
from the beginning and packed pool slices grow downward from the end:

```text
low address
    code pages
        code units grow upward
    unallocated whole pages
    pool pages
        Value slots grow downward
high address
```

A fit check accounts for page roles: the rounded code-page range may not touch
the page containing the lowest pool slot. A pool page remains RW/NX and packs
slots from multiple functions at `sizeof(Value)` alignment. A code page follows
the platform backend's code-write and publication policy and never contains a
pool slot.

A one-MiB slab keeps every eight-byte pool slot within AArch64's signed
`LDR`-literal range of every instruction in the slab: the greatest positive
displacement to a slot start is at most one MiB minus eight bytes. x86-64 RIP-
relative loads have an approximately two-GiB signed range and therefore fit
comfortably in the same placement scheme.

An allocation first tries existing slabs. If none fits, the cache allocates a
new one-MiB slab. A request whose combined pessimistic code and pool storage
cannot fit in such a slab receives `NearPlacementUnavailable`; the driver
destroys that near emitter and re-emits in far mode. The far request receives a
dedicated page-rounded slab sized for that unit, with code at the beginning and
pool pages at the end. Its complete span must satisfy the target's far
reachability contract.

Each slab permits at most one uncommitted frontier reservation at a time. Once
finalization reports the actual code size, commit returns any unused trailing
allocation granules before a later reservation can advance that frontier.
Cancellation returns the complete reservation. Pool addresses do not move when
code slack is recovered.

AArch64 also has the `ADRP` plus `LDR` form with approximately 4 GiB page-
relative reach. The code cache and emitter retain a far-pool path for a code
unit that cannot use near placement. Each AArch64 machine-emission attempt has
an explicit pool mode:

```text
NearLiteral       LDR literal placeholders, 4 bytes per load
FarPageRelative   ADRP + LDR placeholders, 8 bytes per load
```

The JIT driver retains Core IR, first emits and sizes in `NearLiteral` mode,
then requests an
allocation with the near reachability requirement. If the cache cannot satisfy
that placement, it returns a typed near-placement rejection without allocating
or publishing partial code. The driver discards that emitter attempt and
re-emits from the retained Core IR once in forced `FarPageRelative` mode. The
far attempt is independently sizeable and testable from its first instruction;
there is no partially patched near-to-far conversion. Failure to allocate the
far request abandons this compilation and retains interpreted execution.

For each accepted allocation the cache returns a reachability guarantee. The
target backend may then choose a fixed-size pool-load policy before ordinary
encoding:

```text
AArch64 near pool:  LDR literal       4 bytes
AArch64 far pool:   ADRP + LDR        8 bytes
x86-64 pool:        RIP-relative MOV  fixed target-specific size
```

Because the chosen policy fixes pool-load size, loads can use per-fragment
relocations rather than terminating fragments.

## Relocations and fragment layout

The generic emitter distinguishes final-PC rewriting from size-dependent
layout. A fixed-size PC-dependent instruction may appear inside a fragment as
encoded template bytes plus a machine-specific relocation:

```text
CodeFragment
    encoded bytes containing fixed-size placeholders
    RelocationEntry<Relocation> records
        fragment-relative byte offset
        target-specific Relocation
    optional trailing DeferredOperation whose size may vary
```

The third finalization pass copies the template bytes and invokes each
relocation at its actual location. A relocation patches only reserved fields
and never changes fragment size. A variable-size operation remains the sole
trailing deferred operation and ends the fragment for layout purposes.

The generic emitter is parameterized independently by the target's
`DeferredOperation` and `Relocation` types. AArch64 near-pool `LDR` instructions
and x86-64 RIP-relative pool loads use relocations. An AArch64 far-pool `ADRP`
plus `LDR` pair uses one relocation when far-pool mode was selected before
emission.

Every final-copy hook receives a writable destination and the independently
computed executable PC. The former is used only to store instruction bytes;
relocation arithmetic, deferred-operation selection, final encoding, and
reachability checks all use the latter. This is required for Linux code caches
that expose separate RW and RX virtual mappings of the same physical pages.

## Platform memory backend

Slab placement is target-independent. A pluggable platform backend owns mapping
creation, writable scopes, instruction-cache synchronization, publication, and
the code-allocation granularity it can safely reuse:

```text
PlatformCodeMemory
    allocate_slab(size) -> Result<PlatformSlab, CodeCacheError>
    enter_writable(slab) -> writable scope
    publish(code range) -> Result<void, CodeCacheError>
    code_allocation_granularity() -> size_t
```

The standard mapping backend reports the host page size. The macOS `MAP_JIT`
backend reports 16 bytes. Other initial backends, including the planned Linux
dual-mapping backend, conservatively report the host page size; a backend may
adopt 16-byte packing later without changing the emitter or cache interface.
Every code-unit start, pessimistic reservation, and committed final size is
rounded up to the backend's reported granularity. Sixteen-byte machine-code
entry alignment is therefore guaranteed under every backend.

## Tier 1: standard `mmap`

The first executable implementation obtains slabs through standard `mmap`.
Each compiled code unit owns a page-rounded range of previously unpublished
code pages within its slab:

1. reserve the code pages writable and non-executable;
2. emit and validate the complete code unit;
3. synchronize the target instruction cache as required;
4. change only that unit's page range to read-only and executable;
5. publish the entry point;
6. never reopen those pages for writing.

One code unit may occupy several pages. A small unit may leave most of its last
page unused. This is accepted for bring-up because it gives a simple one-way
W^X transition and cannot revoke execute permission from code running on
another thread. Finalization may recover pessimistic slack only in whole pages;
the unused tail of a page containing published code cannot be reused. Pool
pages and not-yet-allocated pages in the same slab are unaffected by the code-
page permission transition.

Pool pages are not part of this transition. They remain writable and non-
executable throughout their lifetime and may contain packed slices belonging
to many code units.

Code or pool allocation failure is a recoverable JIT outcome even during
bring-up. The cache releases any unpublished reservation, the compiler releases
its temporary emitter state and owned pool values, no entry point is installed,
and the triggering execution continues in the interpreter. This is distinct
from near-placement rejection, which requests the single far-mode re-emission,
and from malformed encoding or failed final revalidation, which remain compiler
invariant failures.

## Tier 3 on macOS: packed `MAP_JIT` code

The later macOS implementation allocates code pages with `MAP_JIT` and uses the
platform's thread-local JIT write-protection mechanism. The compiling thread
enters a scoped writable state while emitting through the code slice's writable
view and restores executable state before publication. Other threads may
continue executing already published functions in the same packed pages.

This tier suballocates functions at 16-byte granularity within one code page.
It must not use
process-wide page-permission changes to reopen a page that another thread may
be executing. The `CodeSlice` interface remains unchanged; only the cache's
writable-scope and page-allocation implementation changes.

Pool pages remain ordinary writable, non-executable mappings. They do not use
`MAP_JIT` and are not affected by thread-local code-write scopes.

The initial implementation deliberately skips a portable writable-alias tier.
Other platforms may later supply alias mappings or their native JIT APIs behind
the same cache interface.

## `JitCodeObject` ownership

A non-GC `JitCodeObject` owns the published code and pool slices as one lifetime
unit:

```text
JitCodeObject
    CodeAllocation
    MachineAddress entry
    final code size
    writable Value-pool pointer
    pool MachineAddress
    pool slot count
```

`CodeObject` may embed a nullable atomic `JitCodeObject *`. Compilation fully
constructs the object, initializes every pool slot and metadata record, and
publishes the pointer with release ordering. Entrants load it with acquire
ordering. Initial publication is one-time; generated code is not replaced or
retired. `CodeObject::dealloc` deletes the installed object after the code
object is unreachable.

The `CodeObject` native-layout visitor loads the published pointer and traces
and rewrites exactly the recorded pool slots. Until publication, temporary
`Owned<Value>` instances in the emitter retain those values. Successful
publication transfers their GC-visible ownership to the initialized pool;
cancellation destroys the temporary owners and the pending allocation.

## Publication and concurrency invariants

- executable addresses are final before address-dependent form selection and
  final PC-relative encoding;
- code and pool slices never move while source code may execute;
- no entry point is callable before final encoding and cache synchronization;
- Tier-1 code pages transition from writable to executable exactly once;
- a process-wide permission transition never removes execution from a page that
  another thread may be using;
- a writing thread may not execute JIT code while its platform JIT-write state
  forbids execution;
- pool pages remain writable and non-executable so GC rewriting requires no
  code-page permission transition;
- publication synchronizes all metadata and pool contents before another thread
  can enter the code;
- direct cross-unit targets remain valid for the complete lifetime of every
  source unit that embeds their address.

## Verification

Code-cache tests should cover:

- page-size discovery rather than assuming 4 KiB or 16 KiB pages;
- page-rounded Tier-1 capacity and one-way RW-to-RX publication;
- one-MiB two-frontier slab placement and exact near-range boundaries;
- permanent separation of code and pool page roles, including their frontier
  collision boundary;
- page-granular standard-backend slack recovery and 16-byte `MAP_JIT` slack
  recovery after pessimistic sizing;
- inability to modify published Tier-1 code through the cache API;
- separate code and pool mappings with code non-writable after publication and
  pool storage writable and non-executable;
- multiple function pool slices packed into one physical pool page;
- stable executable and pool addresses across all three emitter passes;
- `write_pointer` and `execute_address` being treated independently;
- near-region boundary checks for AArch64 literal loads;
- far-region boundary checks for AArch64 `ADRP` plus `LDR`;
- forced near-placement rejection followed by one far-mode emission attempt;
- injected code and pool allocation failures leaving no published entry point,
  no leaked reservation or owned `Value`, and no Python pending exception;
- injected publication failure rolling back unpublished storage and retaining
  interpreted execution without a Python pending exception;
- `Result` propagation preserving `CodeCacheError` without touching pending
  Python exception state;
- x86-64 RIP-relative pool reachability;
- relocation patching without fragment creation or size changes;
- deferred-operation and relocation calculations using `execute_address` when
  the writable and executable mappings have different virtual addresses;
- final instruction bytes verified through an independent disassembler;
- simulated moving-GC rewrites changing pool slots without changing code bytes;
- publication ordering under concurrent lookup and entry;
- later macOS tests in which one thread writes an unpublished slice while
  another executes published code in the same `MAP_JIT` page.

## Open implementation details

The design does not yet choose:

- reclamation and retry strategies for fragmentation and region exhaustion;
- code retirement, dependency tracking, and slice reuse;
- the concrete macOS writable-scope API wrapper and entitlement handling;
- publication and cache-synchronization implementations for each target and
  platform;
- the concrete Linux dual-mapping implementation and policies for other
  non-macOS platforms.

Those choices must not leak virtual-memory or platform publication mechanics
into the target assembler or fragment-layout algorithm.

## Related documents

- [JIT Machine-Code Emission](jit-machine-code-emission.md)
- [JIT Compiler and IR](jit-compiler-and-ir.md)
- [JIT Compiler Bring-up Plan](jit-compiler-bring-up-plan.md)
- [Decision Log](decision-log.md)
